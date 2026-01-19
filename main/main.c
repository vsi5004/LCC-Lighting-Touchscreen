/**
 * @file main.c
 * @brief LCC Lighting Scene Controller - Main Entry Point
 * 
 * This application implements an ESP32-S3 based LCC lighting scene controller
 * with a touch LCD user interface for the Waveshare ESP32-S3 Touch LCD 4.3B.
 * 
 * @see docs/SPEC.md for functional requirements
 * @see docs/ARCHITECTURE.md for system design
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include <sys/stat.h>

// Board drivers
#include "ch422g.h"
#include "waveshare_lcd.h"
#include "waveshare_touch.h"
#include "waveshare_sd.h"

// UI
#include "ui_common.h"

// App modules
#include "app/scene_storage.h"
#include "app/lcc_node.h"
#include "app/fade_controller.h"
#include "app/screen_timeout.h"

static const char *TAG = "main";

// Hardware handles
ch422g_handle_t s_ch422g = NULL;
esp_lcd_panel_handle_t s_lcd_panel = NULL;
esp_lcd_touch_handle_t s_touch = NULL;
static waveshare_sd_handle_t s_sd_card = NULL;
static bool s_sd_card_ok = false;

/**
 * @brief Initialize I2C master bus
 */
static esp_err_t init_i2c(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus");

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MASTER_SDA_IO,
        .scl_io_num = CONFIG_I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_I2C_MASTER_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &i2c_conf), TAG, "I2C param config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0), TAG, "I2C driver install failed");

    return ESP_OK;
}

/**
 * @brief Initialize all board hardware
 * 
 * Initialization order is critical:
 * 1. I2C (needed for CH422G)
 * 2. CH422G (needed for SD CS, LCD backlight, touch reset)
 * 3. SD Card (needed for config and scenes)
 * 4. LCD Panel
 * 5. Touch Controller
 */
static esp_err_t init_hardware(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Step 1: Initializing I2C...");
    // 1. Initialize I2C
    ret = init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C initialized successfully");

    ESP_LOGI(TAG, "Step 2: Initializing CH422G...");
    // 2. Initialize CH422G I/O Expander
    ch422g_config_t ch422g_config = {
        .i2c_port = I2C_NUM_0,
        .timeout_ms = 1000,
    };
    ret = ch422g_init(&ch422g_config, &s_ch422g);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CH422G: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "CH422G initialized successfully");

    ESP_LOGI(TAG, "Step 3: Initializing SD Card...");
    // 3. Initialize SD Card
    waveshare_sd_config_t sd_config = {
        .mosi_gpio = CONFIG_SD_MOSI_GPIO,
        .miso_gpio = CONFIG_SD_MISO_GPIO,
        .clk_gpio = CONFIG_SD_CLK_GPIO,
        .mount_point = CONFIG_SD_MOUNT_POINT,
        .ch422g_handle = s_ch422g,
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    ret = waveshare_sd_init(&sd_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        s_sd_card_ok = false;
        // Continue - we'll show error screen after LCD init
    } else {
        ESP_LOGI(TAG, "SD Card initialized successfully");
        s_sd_card_ok = true;
    }

    ESP_LOGI(TAG, "Step 4: Initializing LCD Panel...");
    // 4. Initialize LCD Panel
    waveshare_lcd_config_t lcd_config = {
        .h_res = CONFIG_LCD_H_RES,
        .v_res = CONFIG_LCD_V_RES,
        .pixel_clock_hz = CONFIG_LCD_PIXEL_CLOCK_HZ,
        .num_fb = 2,  // Double buffering
        .bounce_buffer_size_px = CONFIG_LCD_H_RES * CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT,
        .ch422g_handle = s_ch422g,
    };
    ret = waveshare_lcd_init(&lcd_config, &s_lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "LCD Panel initialized successfully");

    ESP_LOGI(TAG, "Step 5: Initializing Touch Controller...");
    // 5. Initialize Touch Controller
    waveshare_touch_config_t touch_config = {
        .i2c_port = I2C_NUM_0,
        .h_res = CONFIG_LCD_H_RES,
        .v_res = CONFIG_LCD_V_RES,
        .ch422g_handle = s_ch422g,
    };
    ret = waveshare_touch_init(&touch_config, &s_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Touch Controller initialized successfully");

    ESP_LOGI(TAG, "Hardware initialization complete");
    return ESP_OK;
}

/**
 * @brief Check for scenes.json and create if it doesn't exist
 */
static void ensure_scenes_json_exists(void)
{
    const char *scenes_path = "/sdcard/scenes.json";
    
    // Check if file exists
    struct stat st;
    if (stat(scenes_path, &st) == 0) {
        ESP_LOGI(TAG, "scenes.json found (%ld bytes)", st.st_size);
        return;
    }
    
    ESP_LOGI(TAG, "scenes.json not found, creating default file...");
    
    // Create a default scenes.json with example scenes
    const char *default_scenes = 
        "{\n"
        "  \"scenes\": [\n"
        "    {\n"
        "      \"name\": \"Example Scene 1\",\n"
        "      \"brightness\": 100,\n"
        "      \"r\": 255,\n"
        "      \"g\": 200,\n"
        "      \"b\": 150,\n"
        "      \"w\": 0\n"
        "    },\n"
        "    {\n"
        "      \"name\": \"Example Scene 2\",\n"
        "      \"brightness\": 75,\n"
        "      \"r\": 100,\n"
        "      \"g\": 150,\n"
        "      \"b\": 255,\n"
        "      \"w\": 50\n"
        "    }\n"
        "  ]\n"
        "}\n";
    
    FILE *file = fopen(scenes_path, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to create scenes.json");
        return;
    }
    
    size_t written = fwrite(default_scenes, 1, strlen(default_scenes), file);
    fclose(file);
    
    if (written == strlen(default_scenes)) {
        ESP_LOGI(TAG, "Created scenes.json with %d bytes", written);
    } else {
        ESP_LOGE(TAG, "Failed to write complete scenes.json");
    }
}

/**
 * @brief Load and display a JPEG image from SD card
 * @param panel LCD panel handle
 * @param filepath Path to JPEG file (e.g., "/sdcard/image.jpg")
 */
static esp_err_t load_and_display_image(esp_lcd_panel_handle_t panel, const char *filepath)
{
    ESP_LOGI(TAG, "Loading image: %s", filepath);
    
    // Open the file
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    ESP_LOGI(TAG, "Image file size: %d bytes", file_size);
    
    // Allocate buffer for JPEG data
    uint8_t *jpeg_buf = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes for JPEG buffer", file_size);
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    
    // Read JPEG file
    size_t read_size = fread(jpeg_buf, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        ESP_LOGE(TAG, "Failed to read file completely");
        free(jpeg_buf);
        return ESP_FAIL;
    }
    
    // Check JPEG header
    if (jpeg_buf[0] != 0xFF || jpeg_buf[1] != 0xD8) {
        ESP_LOGE(TAG, "Invalid JPEG file - missing SOI marker");
        free(jpeg_buf);
        return ESP_FAIL;
    }
    
    // Check for progressive JPEG (SOF2 marker 0xFFC2)
    bool is_progressive = false;
    for (size_t i = 0; i < file_size - 1; i++) {
        if (jpeg_buf[i] == 0xFF && jpeg_buf[i+1] == 0xC2) {
            is_progressive = true;
            break;
        }
    }
    
    if (is_progressive) {
        ESP_LOGE(TAG, "Progressive JPEG not supported by TinyJPEG decoder!");
        ESP_LOGE(TAG, "Please convert your image to baseline JPEG format");
        free(jpeg_buf);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    ESP_LOGI(TAG, "JPEG file loaded, decoding...");
    
    // Allocate output buffer (worst case: RGB565 = 2 bytes per pixel)
    // We'll allocate for max LCD size
    size_t out_buf_size = CONFIG_LCD_H_RES * CONFIG_LCD_V_RES * 2;
    uint8_t *out_buf = heap_caps_malloc(out_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        free(jpeg_buf);
        return ESP_ERR_NO_MEM;
    }
    
    // Allocate working buffer for JPEG decoder (required by TinyJPEG)
    size_t work_buf_size = 3100;  // Default size for JD_FASTDECODE=0
    uint8_t *work_buf = heap_caps_malloc(work_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work_buf) {
        ESP_LOGE(TAG, "Failed to allocate working buffer");
        free(out_buf);
        free(jpeg_buf);
        return ESP_ERR_NO_MEM;
    }
    
    // Decode JPEG
    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = jpeg_buf,
        .indata_size = file_size,
        .outbuf = out_buf,
        .outbuf_size = out_buf_size,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 0,
        },
        .advanced = {
            .working_buffer = work_buf,
            .working_buffer_size = work_buf_size,
        }
    };
    
    esp_jpeg_image_output_t outimg;
    esp_err_t ret = esp_jpeg_decode(&jpeg_cfg, &outimg);
    
    free(work_buf);  // Free working buffer immediately after decode
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
        free(out_buf);
        free(jpeg_buf);
        return ret;
    }
    
    ESP_LOGI(TAG, "JPEG decoded: %dx%d", outimg.width, outimg.height);
    
    // Get framebuffer
    void *fb0 = NULL;
    ret = esp_lcd_rgb_panel_get_frame_buffer(panel, 1, &fb0);
    if (ret != ESP_OK || fb0 == NULL) {
        ESP_LOGE(TAG, "Failed to get framebuffer");
        free(out_buf);
        free(jpeg_buf);
        return ret;
    }
    
    uint16_t *framebuffer = (uint16_t *)fb0;
    uint16_t *img_data = (uint16_t *)out_buf;
    
    // Copy image to framebuffer (center if smaller, crop if larger)
    int lcd_w = CONFIG_LCD_H_RES;
    int lcd_h = CONFIG_LCD_V_RES;
    int img_w = outimg.width;
    int img_h = outimg.height;
    
    // Clear screen to black first
    memset(framebuffer, 0, lcd_w * lcd_h * 2);
    
    // Calculate offset to center image
    int offset_x = (lcd_w > img_w) ? (lcd_w - img_w) / 2 : 0;
    int offset_y = (lcd_h > img_h) ? (lcd_h - img_h) / 2 : 0;
    
    // Copy image data
    int copy_w = (img_w < lcd_w) ? img_w : lcd_w;
    int copy_h = (img_h < lcd_h) ? img_h : lcd_h;
    
    for (int y = 0; y < copy_h; y++) {
        for (int x = 0; x < copy_w; x++) {
            int fb_x = x + offset_x;
            int fb_y = y + offset_y;
            framebuffer[fb_y * lcd_w + fb_x] = img_data[y * img_w + x];
        }
    }
    
    ESP_LOGI(TAG, "Image displayed successfully");
    
    // Cleanup
    free(out_buf);
    free(jpeg_buf);
    
    return ESP_OK;
}

// ============================================================================
// Lighting Task
// ============================================================================

/// Lighting task handle
static TaskHandle_t s_lighting_task = NULL;

/// Lighting task tick interval (ms) - 10ms for smooth fade interpolation
#define LIGHTING_TASK_INTERVAL_MS  10

/**
 * @brief Lighting control task
 * 
 * Runs the fade controller state machine and handles LCC event transmission.
 * Tick interval of 10ms combined with burst transmission of all 5 parameters
 * provides smooth fades (100 steps per second, ~2.5 value change per step
 * for a 10-second 0→255 fade).
 */
static void lighting_task(void *arg)
{
    ESP_LOGI(TAG, "Lighting task started");
    
    TickType_t last_wake = xTaskGetTickCount();
    
    while (1) {
        // Process fade controller
        fade_controller_tick();
        
        // Fixed delay for consistent timing
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LIGHTING_TASK_INTERVAL_MS));
    }
}

/**
 * @brief Show SD card missing error screen
 * 
 * Displays a user-friendly error message when SD card is not detected.
 * Loops forever checking for SD card insertion.
 */
static void show_sd_card_error_screen(void)
{
    ESP_LOGI(TAG, "Showing SD card error screen");
    
    // Initialize LVGL first so we can display the error
    lv_disp_t *disp = NULL;
    lv_indev_t *touch_indev = NULL;
    esp_err_t ret = ui_init(&disp, &touch_indev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL for error screen");
        // Fall back to just logging
        while (1) {
            ESP_LOGE(TAG, "SD Card not detected! Please insert SD card and reboot.");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    
    // Create error screen with LVGL
    ui_lock();
    
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E1E1E), LV_PART_MAIN);  // Dark background
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);  // Ensure background is fully opaque
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrolling
    
    // SD card icon (using warning symbol)
    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF9800), LV_PART_MAIN);  // Orange warning
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -80);
    
    // Main error message
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SD Card Not Detected");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // White
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);
    
    // Instructions
    lv_obj_t *instructions = lv_label_create(scr);
    lv_label_set_text(instructions, 
        "Please insert an SD card with the required\n"
        "configuration files and restart the device.\n\n"
        "Required files:\n"
        "  - nodeid.txt (LCC node ID)\n"
        "  - scenes.json (lighting scenes)");
    lv_obj_set_style_text_font(instructions, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(instructions, lv_color_hex(0xB0B0B0), LV_PART_MAIN);  // Light gray
    lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(instructions, LV_ALIGN_CENTER, 0, 70);
    
    ui_unlock();
    
    ESP_LOGE(TAG, "SD Card not detected - waiting for card insertion");
    
    // Loop forever - user must insert card and restart
    // Note: We don't retry SD card init here because it interferes with the display
    // (CH422G and SPI bus reinitialization can affect LCD operation)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGW(TAG, "SD Card missing - please insert card and restart device");
    }
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    // First log - if this doesn't show, app isn't starting
    printf("=== APP_MAIN STARTING ===\n");
    
    ESP_LOGI(TAG, "LCC Lighting Scene Controller starting...");
    ESP_LOGI(TAG, "ESP-IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Free heap at start: %lu bytes", esp_get_free_heap_size());

    // Initialize NVS (required for some ESP-IDF components)
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // Initialize hardware
    ESP_LOGI(TAG, "Starting hardware initialization...");
    ret = init_hardware();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Hardware initialization failed: %s", esp_err_to_name(ret));
        // TODO: Show error on display or blink LED
        
        // Don't return, stay alive for debugging
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGE(TAG, "Hardware init failed - system halted");
        }
    }

    // Check if SD card is present - show error screen if not
    if (!s_sd_card_ok) {
        show_sd_card_error_screen();
        // This function never returns
    }

    // Ensure scenes.json exists (create default if not)
    ensure_scenes_json_exists();
    
    // Display splash image from SD card (FAT uses 8.3 filenames)
    ret = load_and_display_image(s_lcd_panel, "/sdcard/SPLASH.JPG");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No splash image found, continuing without splash");
    }

    // Show splash for specified duration (FR-001: within 1500ms)
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Initialize LCC/OpenMRN (FR-002)
    // This reads node ID from /sdcard/nodeid.txt and initializes TWAI
    ESP_LOGI(TAG, "Initializing LCC/OpenMRN...");
    lcc_config_t lcc_cfg = LCC_CONFIG_DEFAULT();
    ret = lcc_node_init(&lcc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LCC initialization failed: %s - continuing without LCC", 
                 esp_err_to_name(ret));
        // Continue without LCC - device can still function as standalone UI
    } else {
        ESP_LOGI(TAG, "LCC node initialized - Node ID: %012llX, Base Event: %016llX",
                 (unsigned long long)lcc_node_get_node_id(),
                 (unsigned long long)lcc_node_get_base_event_id());
    }

    // Initialize screen timeout module (power saving)
    ESP_LOGI(TAG, "Initializing screen timeout...");
    screen_timeout_config_t screen_timeout_cfg = {
        .ch422g_handle = s_ch422g,
        .timeout_sec = lcc_node_get_screen_timeout_sec(),
    };
    ret = screen_timeout_init(&screen_timeout_cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Screen timeout init failed: %s - power saving disabled", 
                 esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Screen timeout initialized: %u sec (0=disabled)",
                 screen_timeout_cfg.timeout_sec);
    }

    // Initialize fade controller
    ESP_LOGI(TAG, "Initializing fade controller...");
    ret = fade_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fade controller init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Fade controller initialized");
    }

    // Create lighting task to run fade controller
    ESP_LOGI(TAG, "Starting lighting task...");
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        lighting_task,
        "lighting",
        4096,           // Stack size (per ARCHITECTURE.md)
        NULL,
        4,              // Priority 4 (per ARCHITECTURE.md)
        &s_lighting_task,
        tskNO_AFFINITY  // Run on any core
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create lighting task");
    } else {
        ESP_LOGI(TAG, "Lighting task started");
    }

    // Initialize LVGL
    ESP_LOGI(TAG, "Initializing LVGL...");
    lv_disp_t *disp = NULL;
    lv_indev_t *touch_indev = NULL;
    ret = ui_init(&disp, &touch_indev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL: %s", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            ESP_LOGE(TAG, "LVGL init failed - system halted");
        }
    }
    ESP_LOGI(TAG, "LVGL initialized successfully");

    // Show main UI (FR-010)
    ESP_LOGI(TAG, "Showing main UI...");
    ui_show_main();
    ESP_LOGI(TAG, "Main UI displayed");

    // Load scenes from SD card and populate Scene Selector tab
    ESP_LOGI(TAG, "Loading scenes from SD card...");
    scene_storage_reload_ui();
    ESP_LOGI(TAG, "Scenes loaded");

    // Auto-apply first scene on boot if enabled
    if (lcc_node_get_auto_apply_enabled()) {
        ui_scene_t first_scene;
        if (scene_storage_get_first(&first_scene) == ESP_OK) {
            uint16_t duration_sec = lcc_node_get_auto_apply_duration_sec();
            ESP_LOGI(TAG, "Auto-applying first scene '%s' over %u seconds",
                     first_scene.name, duration_sec);
            
            // Set initial state to all zeros (assume lights are off at boot)
            lighting_state_t initial_state = {
                .brightness = 0,
                .red = 0,
                .green = 0,
                .blue = 0,
                .white = 0
            };
            fade_controller_set_current(&initial_state);
            
            // Start fade to first scene
            fade_params_t params = {
                .target = {
                    .brightness = first_scene.brightness,
                    .red = first_scene.red,
                    .green = first_scene.green,
                    .blue = first_scene.blue,
                    .white = first_scene.white
                },
                .duration_ms = (uint32_t)duration_sec * 1000
            };
            
            esp_err_t fade_ret = fade_controller_start(&params);
            if (fade_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to start auto-apply fade: %s", esp_err_to_name(fade_ret));
            } else {
                // Start progress bar tracking (only if duration > 0)
                if (duration_sec > 0) {
                    ui_lock();
                    ui_scenes_start_progress_tracking();
                    ui_unlock();
                }
            }
        } else {
            ESP_LOGI(TAG, "No scenes available for auto-apply");
        }
    } else {
        ESP_LOGI(TAG, "Auto-apply first scene is disabled");
    }

    ESP_LOGI(TAG, "Initialization complete - entering main loop");

    // Main loop: Run screen timeout tick and report status periodically
    TickType_t last_status_tick = xTaskGetTickCount();
    while (1) {
        // Tick screen timeout every 500ms
        screen_timeout_tick();
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Report status every 10 seconds
        if ((xTaskGetTickCount() - last_status_tick) >= pdMS_TO_TICKS(10000)) {
            last_status_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "Status - Free heap: %lu bytes, LCC: %s, Screen: %s", 
                     esp_get_free_heap_size(),
                     lcc_node_get_status() == LCC_STATUS_RUNNING ? "running" : "not running",
                     screen_timeout_is_screen_on() ? "on" : "off");
        }
    }
}

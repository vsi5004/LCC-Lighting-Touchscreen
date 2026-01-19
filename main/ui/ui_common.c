/**
 * @file ui_common.c
 * @brief Common UI initialization and LVGL setup
 */

#include "ui_common.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// Board drivers
#include "waveshare_lcd.h"
#include "waveshare_touch.h"

// App modules
#include "app/screen_timeout.h"

static const char *TAG = "ui_common";

// LVGL objects
static lv_disp_t *s_disp = NULL;
static lv_indev_t *s_touch_indev = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;

// Hardware handles (from main)
extern esp_lcd_panel_handle_t s_lcd_panel;
extern esp_lcd_touch_handle_t s_touch;

// Forward declarations
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data);
static void lvgl_tick_timer_cb(void *arg);
static void lvgl_task(void *arg);

/**
 * @brief LVGL flush callback - copies framebuffer to LCD
 */
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    int offsetx1 = area->x1;
    int offsety1 = area->y1;
    int offsetx2 = area->x2;
    int offsety2 = area->y2;
    
    // Draw bitmap to LCD
    esp_lcd_panel_draw_bitmap(panel, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    
    lv_disp_flush_ready(drv);
}

/**
 * @brief LVGL touch read callback
 */
static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t touch = (esp_lcd_touch_handle_t)drv->user_data;

    // Read touch data
    esp_lcd_touch_read_data(touch);

    // Get touch data using new API
    esp_lcd_touch_point_data_t point_data;
    uint8_t point_cnt = 0;
    
    esp_err_t ret = esp_lcd_touch_get_data(touch, &point_data, &point_cnt, 1);

    if (ret == ESP_OK && point_cnt > 0) {
        data->point.x = point_data.x;
        data->point.y = point_data.y;
        data->state = LV_INDEV_STATE_PRESSED;
        
        // Notify screen timeout module of touch activity
        screen_timeout_notify_activity();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief LVGL tick timer callback
 */
static void lvgl_tick_timer_cb(void *arg)
{
    lv_tick_inc(UI_LVGL_TICK_PERIOD_MS);
}

/**
 * @brief LVGL task - handles rendering and input
 */
static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");

    while (1) {
        // Lock mutex
        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            uint32_t task_delay_ms = lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
            
            // Clamp delay
            if (task_delay_ms > UI_LVGL_TASK_MAX_DELAY_MS) {
                task_delay_ms = UI_LVGL_TASK_MAX_DELAY_MS;
            } else if (task_delay_ms < UI_LVGL_TASK_MIN_DELAY_MS) {
                task_delay_ms = UI_LVGL_TASK_MIN_DELAY_MS;
            }
            
            vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(UI_LVGL_TASK_MIN_DELAY_MS));
        }
    }
}

/**
 * @brief Initialize LVGL
 */
esp_err_t ui_init(lv_disp_t **disp, lv_indev_t **touch_indev)
{
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_ERR_INVALID_ARG, TAG, "disp is NULL");
    ESP_RETURN_ON_FALSE(touch_indev != NULL, ESP_ERR_INVALID_ARG, TAG, "touch_indev is NULL");
    
    ESP_LOGI(TAG, "Initializing LVGL");

    // Create mutex
    s_lvgl_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lvgl_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create mutex");

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers (in SPIRAM for better performance)
    size_t buffer_size = CONFIG_LCD_H_RES * CONFIG_LCD_RGB_BOUNCE_BUFFER_HEIGHT;
    lv_color_t *buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    
    ESP_RETURN_ON_FALSE(buf1 && buf2, ESP_ERR_NO_MEM, TAG, "Failed to allocate LVGL buffers");

    // Initialize display buffer
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buffer_size);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = CONFIG_LCD_H_RES;
    disp_drv.ver_res = CONFIG_LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = s_lcd_panel;
    
    s_disp = lv_disp_drv_register(&disp_drv);
    ESP_RETURN_ON_FALSE(s_disp != NULL, ESP_FAIL, TAG, "Failed to register display driver");

    // Register touch input driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    indev_drv.user_data = s_touch;
    
    s_touch_indev = lv_indev_drv_register(&indev_drv);
    ESP_RETURN_ON_FALSE(s_touch_indev != NULL, ESP_FAIL, TAG, "Failed to register touch driver");

    // Create tick timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = lvgl_tick_timer_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_RETURN_ON_ERROR(
        esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer),
        TAG, "Failed to create LVGL tick timer"
    );
    ESP_RETURN_ON_ERROR(
        esp_timer_start_periodic(lvgl_tick_timer, UI_LVGL_TICK_PERIOD_MS * 1000),
        TAG, "Failed to start LVGL tick timer"
    );

    // Create LVGL task pinned to CPU1 (CPU0 handles LCD DMA ISRs)
    BaseType_t ret = xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl_task",
        UI_LVGL_TASK_STACK_SIZE_KB * 1024,
        NULL,
        UI_LVGL_TASK_PRIORITY,
        NULL,
        1  // Pin to CPU1
    );
    ESP_RETURN_ON_FALSE(ret == pdPASS, ESP_FAIL, TAG, "Failed to create LVGL task");

    *disp = s_disp;
    *touch_indev = s_touch_indev;

    ESP_LOGI(TAG, "LVGL initialized successfully");
    return ESP_OK;
}

bool ui_lock(void)
{
    if (s_lvgl_mutex == NULL) {
        return false;
    }
    return xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY) == pdTRUE;
}

void ui_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

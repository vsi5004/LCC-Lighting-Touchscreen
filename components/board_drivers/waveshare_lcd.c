/**
 * @file waveshare_lcd.c
 * @brief RGB LCD Driver Implementation for Waveshare ESP32-S3 Touch LCD 4.3B
 */

#include "waveshare_lcd.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "waveshare_lcd";

esp_err_t waveshare_lcd_init(const waveshare_lcd_config_t *config, esp_lcd_panel_handle_t *panel_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(panel_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "panel_handle is NULL");

    ESP_LOGI(TAG, "Initializing RGB LCD panel %dx%d @ %d Hz",
             config->h_res, config->v_res, config->pixel_clock_hz);

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = config->pixel_clock_hz,
            .h_res = config->h_res,
            .v_res = config->v_res,
            // Timing parameters for 800x480 panel
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = config->num_fb > 0 ? config->num_fb : 1,
        .bounce_buffer_size_px = config->bounce_buffer_size_px,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_GPIO_HSYNC,
        .vsync_gpio_num = LCD_GPIO_VSYNC,
        .de_gpio_num = LCD_GPIO_DE,
        .pclk_gpio_num = LCD_GPIO_PCLK,
        .disp_gpio_num = -1,    // Not used, backlight via CH422G
        .data_gpio_nums = {
            LCD_GPIO_DATA0,
            LCD_GPIO_DATA1,
            LCD_GPIO_DATA2,
            LCD_GPIO_DATA3,
            LCD_GPIO_DATA4,
            LCD_GPIO_DATA5,
            LCD_GPIO_DATA6,
            LCD_GPIO_DATA7,
            LCD_GPIO_DATA8,
            LCD_GPIO_DATA9,
            LCD_GPIO_DATA10,
            LCD_GPIO_DATA11,
            LCD_GPIO_DATA12,
            LCD_GPIO_DATA13,
            LCD_GPIO_DATA14,
            LCD_GPIO_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
            .refresh_on_demand = 0,  // Continuous refresh
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_rgb_panel(&panel_config, panel_handle),
        TAG, "Failed to create RGB panel"
    );

    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_init(*panel_handle),
        TAG, "Failed to initialize RGB panel"
    );

    // Turn on backlight via CH422G
    if (config->ch422g_handle != NULL) {
        ESP_RETURN_ON_ERROR(
            ch422g_backlight_on(config->ch422g_handle),
            TAG, "Failed to turn on backlight"
        );
    }

    ESP_LOGI(TAG, "RGB LCD panel initialized successfully");
    return ESP_OK;
}

esp_err_t waveshare_lcd_register_vsync_callback(
    esp_lcd_panel_handle_t panel_handle,
    esp_lcd_rgb_panel_vsync_cb_t callback,
    void *user_ctx)
{
    ESP_RETURN_ON_FALSE(panel_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "panel_handle is NULL");

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = callback,
    };

    return esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, user_ctx);
}

esp_err_t waveshare_lcd_get_frame_buffer(
    esp_lcd_panel_handle_t panel_handle,
    int num_fbs,
    void **fb0,
    void **fb1)
{
    ESP_RETURN_ON_FALSE(panel_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "panel_handle is NULL");
    ESP_RETURN_ON_FALSE(fb0 != NULL, ESP_ERR_INVALID_ARG, TAG, "fb0 is NULL");

    if (num_fbs == 1) {
        return esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 1, fb0);
    } else {
        ESP_RETURN_ON_FALSE(fb1 != NULL, ESP_ERR_INVALID_ARG, TAG, "fb1 is NULL for dual buffer");
        return esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, fb0, fb1);
    }
}

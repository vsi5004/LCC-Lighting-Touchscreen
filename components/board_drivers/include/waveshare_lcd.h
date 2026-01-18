/**
 * @file waveshare_lcd.h
 * @brief RGB LCD Driver for Waveshare ESP32-S3 Touch LCD 4.3B
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "ch422g.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD GPIO Pin Definitions
 */
#define LCD_GPIO_VSYNC      GPIO_NUM_3
#define LCD_GPIO_HSYNC      GPIO_NUM_46
#define LCD_GPIO_DE         GPIO_NUM_5
#define LCD_GPIO_PCLK       GPIO_NUM_7

#define LCD_GPIO_DATA0      GPIO_NUM_14
#define LCD_GPIO_DATA1      GPIO_NUM_38
#define LCD_GPIO_DATA2      GPIO_NUM_18
#define LCD_GPIO_DATA3      GPIO_NUM_17
#define LCD_GPIO_DATA4      GPIO_NUM_10
#define LCD_GPIO_DATA5      GPIO_NUM_39
#define LCD_GPIO_DATA6      GPIO_NUM_0
#define LCD_GPIO_DATA7      GPIO_NUM_45
#define LCD_GPIO_DATA8      GPIO_NUM_48
#define LCD_GPIO_DATA9      GPIO_NUM_47
#define LCD_GPIO_DATA10     GPIO_NUM_21
#define LCD_GPIO_DATA11     GPIO_NUM_1
#define LCD_GPIO_DATA12     GPIO_NUM_2
#define LCD_GPIO_DATA13     GPIO_NUM_42
#define LCD_GPIO_DATA14     GPIO_NUM_41
#define LCD_GPIO_DATA15     GPIO_NUM_40

/**
 * @brief LCD configuration structure
 */
typedef struct {
    int h_res;                      ///< Horizontal resolution
    int v_res;                      ///< Vertical resolution
    int pixel_clock_hz;             ///< Pixel clock frequency
    int num_fb;                     ///< Number of frame buffers (1-3)
    int bounce_buffer_size_px;      ///< Bounce buffer size in pixels
    ch422g_handle_t ch422g_handle;  ///< CH422G handle for backlight control
} waveshare_lcd_config_t;

/**
 * @brief Initialize the RGB LCD panel
 * 
 * @param config Pointer to LCD configuration
 * @param panel_handle Pointer to store the panel handle
 * @return ESP_OK on success
 */
esp_err_t waveshare_lcd_init(const waveshare_lcd_config_t *config, esp_lcd_panel_handle_t *panel_handle);

/**
 * @brief Register VSYNC event callback
 * 
 * @param panel_handle LCD panel handle
 * @param callback Callback function
 * @param user_ctx User context passed to callback
 * @return ESP_OK on success
 */
esp_err_t waveshare_lcd_register_vsync_callback(
    esp_lcd_panel_handle_t panel_handle,
    esp_lcd_rgb_panel_vsync_cb_t callback,
    void *user_ctx
);

/**
 * @brief Get frame buffer(s) from the LCD panel
 * 
 * @param panel_handle LCD panel handle
 * @param num_fbs Number of frame buffers to get
 * @param fb0 Pointer to store first frame buffer
 * @param fb1 Pointer to store second frame buffer (optional)
 * @return ESP_OK on success
 */
esp_err_t waveshare_lcd_get_frame_buffer(
    esp_lcd_panel_handle_t panel_handle,
    int num_fbs,
    void **fb0,
    void **fb1
);

#ifdef __cplusplus
}
#endif

/**
 * @file waveshare_touch.h
 * @brief GT911 Touch Controller Driver for Waveshare ESP32-S3 Touch LCD 4.3B
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "ch422g.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Touch controller I2C address
 */
#define TOUCH_I2C_ADDR      0x5D

/**
 * @brief GPIO4 used for touch reset timing
 */
#define TOUCH_GPIO4         GPIO_NUM_4

/**
 * @brief Touch configuration structure
 */
typedef struct {
    i2c_port_t i2c_port;            ///< I2C port number
    int h_res;                      ///< Horizontal resolution
    int v_res;                      ///< Vertical resolution
    ch422g_handle_t ch422g_handle;  ///< CH422G handle for reset sequence
} waveshare_touch_config_t;

/**
 * @brief Initialize the GT911 touch controller
 * 
 * @param config Pointer to touch configuration
 * @param touch_handle Pointer to store the touch handle
 * @return ESP_OK on success
 */
esp_err_t waveshare_touch_init(const waveshare_touch_config_t *config, esp_lcd_touch_handle_t *touch_handle);

/**
 * @brief Read touch data
 * 
 * @param touch_handle Touch controller handle
 * @return ESP_OK on success
 */
esp_err_t waveshare_touch_read(esp_lcd_touch_handle_t touch_handle);

/**
 * @brief Get touch coordinates
 * 
 * @param touch_handle Touch controller handle
 * @param x Pointer to store X coordinate
 * @param y Pointer to store Y coordinate
 * @param strength Pointer to store touch strength (optional)
 * @param max_points Maximum number of touch points to read
 * @param num_points Pointer to store actual number of touch points
 * @return true if touch detected
 */
bool waveshare_touch_get_xy(
    esp_lcd_touch_handle_t touch_handle,
    uint16_t *x,
    uint16_t *y,
    uint16_t *strength,
    uint8_t max_points,
    uint8_t *num_points
);

#ifdef __cplusplus
}
#endif

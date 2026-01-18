/**
 * @file ch422g.h
 * @brief CH422G I2C I/O Expander Driver for Waveshare ESP32-S3 Touch LCD 4.3B
 * 
 * The CH422G controls:
 * - SD Card CS (directly directly without GPIO pin)
 * - LCD Backlight
 * - Touch Controller Reset
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CH422G I2C addresses
 */
#define CH422G_MODE_ADDR        0x24    ///< Mode configuration register
#define CH422G_OUTPUT_ADDR      0x38    ///< Output register

/**
 * @brief CH422G output bit definitions
 */
#define CH422G_SD_CS_BIT        (1 << 0)    ///< SD Card CS (active low)
#define CH422G_TOUCH_RST_BIT    (1 << 1)    ///< Touch reset
#define CH422G_BACKLIGHT_BIT    (1 << 2)    ///< LCD Backlight

/**
 * @brief Common CH422G output states
 */
#define CH422G_OUTPUT_MODE      0x01    ///< Set output mode
#define CH422G_BL_ON_SD_OFF     0x1E    ///< Backlight ON, SD CS high (inactive)
#define CH422G_BL_OFF_SD_OFF    0x1A    ///< Backlight OFF, SD CS high
#define CH422G_BL_ON_SD_ON      0x0E    ///< Backlight ON, SD CS low (active) - Note: verify bit logic
#define CH422G_SD_CS_LOW        0x0A    ///< SD CS low for card access
#define CH422G_TOUCH_RST_START  0x2C    ///< Assert touch reset
#define CH422G_TOUCH_RST_END    0x2E    ///< Release touch reset

/**
 * @brief CH422G driver configuration
 */
typedef struct {
    i2c_port_t i2c_port;    ///< I2C port number
    int timeout_ms;         ///< I2C transaction timeout
} ch422g_config_t;

/**
 * @brief CH422G driver handle
 */
typedef struct ch422g_dev_t *ch422g_handle_t;

/**
 * @brief Initialize the CH422G driver
 * 
 * @param config Pointer to configuration structure
 * @param handle Pointer to store the driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_init(const ch422g_config_t *config, ch422g_handle_t *handle);

/**
 * @brief Deinitialize the CH422G driver
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_deinit(ch422g_handle_t handle);

/**
 * @brief Set CH422G to output mode
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_set_output_mode(ch422g_handle_t handle);

/**
 * @brief Write to CH422G output register
 * 
 * @param handle Driver handle
 * @param value Output register value
 * @return ESP_OK on success
 */
esp_err_t ch422g_write_output(ch422g_handle_t handle, uint8_t value);

/**
 * @brief Turn LCD backlight on
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_backlight_on(ch422g_handle_t handle);

/**
 * @brief Turn LCD backlight off
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_backlight_off(ch422g_handle_t handle);

/**
 * @brief Enable SD card (pull CS low)
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_sd_card_enable(ch422g_handle_t handle);

/**
 * @brief Disable SD card (pull CS high)
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_sd_card_disable(ch422g_handle_t handle);

/**
 * @brief Execute touch controller reset sequence
 * 
 * @param handle Driver handle
 * @return ESP_OK on success
 */
esp_err_t ch422g_touch_reset(ch422g_handle_t handle);

#ifdef __cplusplus
}
#endif

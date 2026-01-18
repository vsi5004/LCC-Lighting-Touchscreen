/**
 * @file ch422g.c
 * @brief CH422G I2C I/O Expander Driver Implementation
 */

#include "ch422g.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ch422g";

/**
 * @brief CH422G device structure
 */
struct ch422g_dev_t {
    i2c_port_t i2c_port;
    int timeout_ms;
    uint8_t current_output;     ///< Cache of current output state
};

esp_err_t ch422g_init(const ch422g_config_t *config, ch422g_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    struct ch422g_dev_t *dev = calloc(1, sizeof(struct ch422g_dev_t));
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory");

    dev->i2c_port = config->i2c_port;
    dev->timeout_ms = config->timeout_ms > 0 ? config->timeout_ms : 1000;
    dev->current_output = 0;

    // Set output mode
    esp_err_t ret = ch422g_set_output_mode(dev);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    *handle = dev;
    ESP_LOGI(TAG, "CH422G initialized on I2C port %d", config->i2c_port);
    return ESP_OK;
}

esp_err_t ch422g_deinit(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    free(handle);
    return ESP_OK;
}

esp_err_t ch422g_set_output_mode(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    uint8_t cmd = CH422G_OUTPUT_MODE;
    esp_err_t ret = i2c_master_write_to_device(
        handle->i2c_port,
        CH422G_MODE_ADDR,
        &cmd,
        1,
        pdMS_TO_TICKS(handle->timeout_ms)
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to set output mode");

    return ESP_OK;
}

esp_err_t ch422g_write_output(ch422g_handle_t handle, uint8_t value)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    esp_err_t ret = i2c_master_write_to_device(
        handle->i2c_port,
        CH422G_OUTPUT_ADDR,
        &value,
        1,
        pdMS_TO_TICKS(handle->timeout_ms)
    );
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to write output register");

    handle->current_output = value;
    return ESP_OK;
}

esp_err_t ch422g_backlight_on(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_LOGI(TAG, "Backlight ON");
    return ch422g_write_output(handle, CH422G_BL_ON_SD_OFF);
}

esp_err_t ch422g_backlight_off(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_LOGI(TAG, "Backlight OFF");
    return ch422g_write_output(handle, CH422G_BL_OFF_SD_OFF);
}

esp_err_t ch422g_sd_card_enable(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_LOGD(TAG, "SD Card CS LOW (enabled)");
    return ch422g_write_output(handle, CH422G_SD_CS_LOW);
}

esp_err_t ch422g_sd_card_disable(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_LOGD(TAG, "SD Card CS HIGH (disabled)");
    // Restore backlight state when disabling SD
    return ch422g_write_output(handle, CH422G_BL_ON_SD_OFF);
}

esp_err_t ch422g_touch_reset(ch422g_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    ESP_LOGI(TAG, "Executing touch reset sequence");

    // Ensure output mode
    esp_err_t ret = ch422g_set_output_mode(handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to set output mode for touch reset");

    // Assert touch reset
    ret = ch422g_write_output(handle, CH422G_TOUCH_RST_START);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to assert touch reset");

    vTaskDelay(pdMS_TO_TICKS(100));

    // Set GPIO4 LOW (done externally via gpio_set_level if needed)
    // For the Waveshare board, this is handled by the expander

    vTaskDelay(pdMS_TO_TICKS(100));

    // Release touch reset
    ret = ch422g_write_output(handle, CH422G_TOUCH_RST_END);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to release touch reset");

    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Touch reset sequence complete");
    return ESP_OK;
}

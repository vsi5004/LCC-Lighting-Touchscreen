/**
 * @file waveshare_touch.c
 * @brief GT911 Touch Controller Driver Implementation
 */

#include "waveshare_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "waveshare_touch";

/**
 * @brief Execute the specific reset sequence for Waveshare board
 */
static esp_err_t touch_reset_sequence(ch422g_handle_t ch422g, i2c_port_t i2c_port)
{
    ESP_LOGI(TAG, "Executing touch reset sequence");

    // Configure GPIO4 as output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TOUCH_GPIO4),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    // Set CH422G to output mode
    ESP_RETURN_ON_ERROR(ch422g_set_output_mode(ch422g), TAG, "Failed to set CH422G output mode");

    // Assert touch reset via CH422G
    uint8_t write_buf = 0x2C;
    ESP_RETURN_ON_ERROR(
        i2c_master_write_to_device(i2c_port, CH422G_OUTPUT_ADDR, &write_buf, 1, pdMS_TO_TICKS(1000)),
        TAG, "Failed to assert touch reset"
    );
    
    vTaskDelay(pdMS_TO_TICKS(100));

    // Set GPIO4 LOW
    gpio_set_level(TOUCH_GPIO4, 0);
    
    vTaskDelay(pdMS_TO_TICKS(100));

    // Release touch reset
    write_buf = 0x2E;
    ESP_RETURN_ON_ERROR(
        i2c_master_write_to_device(i2c_port, CH422G_OUTPUT_ADDR, &write_buf, 1, pdMS_TO_TICKS(1000)),
        TAG, "Failed to release touch reset"
    );
    
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "Touch reset sequence complete");
    return ESP_OK;
}

esp_err_t waveshare_touch_init(const waveshare_touch_config_t *config, esp_lcd_touch_handle_t *touch_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(touch_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "touch_handle is NULL");
    ESP_RETURN_ON_FALSE(config->ch422g_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ch422g_handle is NULL");

    ESP_LOGI(TAG, "Initializing GT911 touch controller");

    // Execute reset sequence
    ESP_RETURN_ON_ERROR(
        touch_reset_sequence(config->ch422g_handle, config->i2c_port),
        TAG, "Touch reset sequence failed"
    );

    // Create I2C panel IO handle for touch controller
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)config->i2c_port, &tp_io_config, &tp_io_handle),
        TAG, "Failed to create I2C panel IO"
    );

    // Configure GT911 touch controller
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = config->h_res,
        .y_max = config->v_res,
        .rst_gpio_num = -1,     // Reset handled via CH422G
        .int_gpio_num = -1,     // Interrupt not used (polling)
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, touch_handle),
        TAG, "Failed to create GT911 touch controller"
    );

    ESP_LOGI(TAG, "GT911 touch controller initialized (%dx%d)", config->h_res, config->v_res);
    return ESP_OK;
}

esp_err_t waveshare_touch_read(esp_lcd_touch_handle_t touch_handle)
{
    ESP_RETURN_ON_FALSE(touch_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "touch_handle is NULL");
    return esp_lcd_touch_read_data(touch_handle);
}

bool waveshare_touch_get_xy(
    esp_lcd_touch_handle_t touch_handle,
    uint16_t *x,
    uint16_t *y,
    uint16_t *strength,
    uint8_t max_points,
    uint8_t *num_points)
{
    if (touch_handle == NULL || x == NULL || y == NULL || num_points == NULL) {
        return false;
    }

    return esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, num_points, max_points);
}

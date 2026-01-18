/**
 * @file waveshare_sd.h
 * @brief SD Card Driver for Waveshare ESP32-S3 Touch LCD 4.3B
 * 
 * The SD card CS is controlled via CH422G I/O expander, not a direct GPIO.
 */

#pragma once

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "ch422g.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SD card configuration structure
 */
typedef struct {
    int mosi_gpio;                  ///< SPI MOSI GPIO
    int miso_gpio;                  ///< SPI MISO GPIO
    int clk_gpio;                   ///< SPI CLK GPIO
    const char *mount_point;        ///< VFS mount point (e.g., "/sdcard")
    ch422g_handle_t ch422g_handle;  ///< CH422G handle for CS control
    int max_files;                  ///< Maximum number of open files
    bool format_if_mount_failed;    ///< Format card if mount fails
} waveshare_sd_config_t;

/**
 * @brief SD card handle
 */
typedef struct waveshare_sd_t *waveshare_sd_handle_t;

/**
 * @brief Initialize the SD card
 * 
 * @param config Pointer to SD card configuration
 * @param handle Pointer to store the SD card handle
 * @return ESP_OK on success
 */
esp_err_t waveshare_sd_init(const waveshare_sd_config_t *config, waveshare_sd_handle_t *handle);

/**
 * @brief Deinitialize and unmount the SD card
 * 
 * @param handle SD card handle
 * @return ESP_OK on success
 */
esp_err_t waveshare_sd_deinit(waveshare_sd_handle_t handle);

/**
 * @brief Get SD card information
 * 
 * @param handle SD card handle
 * @param card Pointer to store card info
 * @return ESP_OK on success
 */
esp_err_t waveshare_sd_get_info(waveshare_sd_handle_t handle, sdmmc_card_t **card);

/**
 * @brief Check if a file exists on the SD card
 * 
 * @param path Full path to the file
 * @return true if file exists
 */
bool waveshare_sd_file_exists(const char *path);

/**
 * @brief Read entire file into allocated buffer
 * 
 * @param path Full path to the file
 * @param buffer Pointer to store allocated buffer (caller must free)
 * @param size Pointer to store file size
 * @return ESP_OK on success
 */
esp_err_t waveshare_sd_read_file(const char *path, char **buffer, size_t *size);

/**
 * @brief Write data to file atomically (write to temp, then rename)
 * 
 * @param path Full path to the file
 * @param data Data to write
 * @param size Size of data
 * @return ESP_OK on success
 */
esp_err_t waveshare_sd_write_file_atomic(const char *path, const char *data, size_t size);

#ifdef __cplusplus
}
#endif

/**
 * @file waveshare_sd.c
 * @brief SD Card Driver Implementation for Waveshare ESP32-S3 Touch LCD 4.3B
 */

#include "waveshare_sd.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "waveshare_sd";

/**
 * @brief SD card device structure
 */
struct waveshare_sd_t {
    sdmmc_card_t *card;
    sdmmc_host_t host;
    const char *mount_point;
    ch422g_handle_t ch422g_handle;
    bool mounted;
};

esp_err_t waveshare_sd_init(const waveshare_sd_config_t *config, waveshare_sd_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(config->ch422g_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ch422g_handle is NULL");

    ESP_LOGI(TAG, "Initializing SD card");

    // Allocate device structure
    struct waveshare_sd_t *dev = calloc(1, sizeof(struct waveshare_sd_t));
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_NO_MEM, TAG, "Failed to allocate memory");

    dev->ch422g_handle = config->ch422g_handle;
    dev->mount_point = config->mount_point;
    dev->mounted = false;

    // Enable SD card via CH422G (pull CS low)
    esp_err_t ret = ch422g_sd_card_enable(dev->ch422g_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable SD card CS");
        free(dev);
        return ret;
    }

    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_gpio,
        .miso_io_num = config->miso_gpio,
        .sclk_io_num = config->clk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    dev->host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    
    ret = spi_bus_initialize(dev->host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        free(dev);
        return ret;
    }

    // SD card slot configuration
    // Note: CS is controlled via CH422G, not a GPIO pin directly
    // We use a dummy CS pin (-1) since CH422G handles it
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = -1;  // CS controlled by CH422G
    slot_config.host_id = dev->host.slot;

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = config->format_if_mount_failed,
        .max_files = config->max_files > 0 ? config->max_files : 5,
        .allocation_unit_size = 16 * 1024,
    };

    ESP_LOGI(TAG, "Mounting filesystem at %s", config->mount_point);
    
    ret = esp_vfs_fat_sdspi_mount(config->mount_point, &dev->host, &slot_config, &mount_config, &dev->card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem: %s", esp_err_to_name(ret));
        spi_bus_free(dev->host.slot);
        free(dev);
        return ret;
    }

    dev->mounted = true;
    
    // Print card info
    sdmmc_card_print_info(stdout, dev->card);

    *handle = dev;
    ESP_LOGI(TAG, "SD card initialized and mounted at %s", config->mount_point);
    return ESP_OK;
}

esp_err_t waveshare_sd_deinit(waveshare_sd_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    if (handle->mounted) {
        esp_vfs_fat_sdcard_unmount(handle->mount_point, handle->card);
        spi_bus_free(handle->host.slot);
        handle->mounted = false;
    }

    // Disable SD card CS
    if (handle->ch422g_handle != NULL) {
        ch422g_sd_card_disable(handle->ch422g_handle);
    }

    free(handle);
    ESP_LOGI(TAG, "SD card deinitialized");
    return ESP_OK;
}

esp_err_t waveshare_sd_get_info(waveshare_sd_handle_t handle, sdmmc_card_t **card)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(card != NULL, ESP_ERR_INVALID_ARG, TAG, "card is NULL");
    
    *card = handle->card;
    return ESP_OK;
}

bool waveshare_sd_file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

esp_err_t waveshare_sd_read_file(const char *path, char **buffer, size_t *size)
{
    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG, "path is NULL");
    ESP_RETURN_ON_FALSE(buffer != NULL, ESP_ERR_INVALID_ARG, TAG, "buffer is NULL");
    ESP_RETURN_ON_FALSE(size != NULL, ESP_ERR_INVALID_ARG, TAG, "size is NULL");

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    *size = st.st_size;
    *buffer = malloc(*size + 1);
    if (*buffer == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_size = fread(*buffer, 1, *size, f);
    fclose(f);

    if (read_size != *size) {
        free(*buffer);
        *buffer = NULL;
        return ESP_FAIL;
    }

    (*buffer)[*size] = '\0';  // Null terminate
    
    ESP_LOGD(TAG, "Read %zu bytes from %s", *size, path);
    return ESP_OK;
}

esp_err_t waveshare_sd_write_file_atomic(const char *path, const char *data, size_t size)
{
    ESP_RETURN_ON_FALSE(path != NULL, ESP_ERR_INVALID_ARG, TAG, "path is NULL");
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG, "data is NULL");

    // Create temp file path
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    // Write to temp file
    FILE *f = fopen(temp_path, "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create temp file: %s", temp_path);
        return ESP_FAIL;
    }

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        ESP_LOGE(TAG, "Failed to write all data to temp file");
        unlink(temp_path);
        return ESP_FAIL;
    }

    // Delete old file if exists
    if (waveshare_sd_file_exists(path)) {
        unlink(path);
    }

    // Rename temp to final
    if (rename(temp_path, path) != 0) {
        ESP_LOGE(TAG, "Failed to rename temp file to %s", path);
        unlink(temp_path);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Atomically wrote %zu bytes to %s", size, path);
    return ESP_OK;
}

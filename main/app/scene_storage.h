/**
 * @file scene_storage.h
 * @brief Scene storage module - load/save scenes from/to SD card
 */

#pragma once

#include "esp_err.h"
#include "../ui/ui_common.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE_STORAGE_MAX_SCENES    32
#define SCENE_STORAGE_PATH          "/sdcard/scenes.json"

/**
 * @brief Initialize scene storage module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scene_storage_init(void);

/**
 * @brief Load scenes from SD card
 * 
 * @param scenes Output array to store loaded scenes
 * @param max_count Maximum number of scenes to load
 * @param out_count Output: actual number of scenes loaded
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scene_storage_load(ui_scene_t *scenes, size_t max_count, size_t *out_count);

/**
 * @brief Save a new scene to SD card
 * 
 * Appends the scene to the existing scenes.json file.
 * If a scene with the same name exists, it will be updated.
 * 
 * @param name Scene name
 * @param brightness Brightness value (0-255)
 * @param red Red value (0-255)
 * @param green Green value (0-255)
 * @param blue Blue value (0-255)
 * @param white White value (0-255)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scene_storage_save(const char *name, uint8_t brightness, 
                             uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

/**
 * @brief Delete a scene by name
 * 
 * @param name Scene name to delete
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scene_storage_delete(const char *name);

/**
 * @brief Get the number of stored scenes
 * 
 * @return size_t Number of scenes
 */
size_t scene_storage_get_count(void);

/**
 * @brief Get the first scene (for auto-apply on boot)
 * 
 * @param scene Output: the first scene
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if no scenes
 */
esp_err_t scene_storage_get_first(ui_scene_t *scene);

/**
 * @brief Reload scenes and update UI
 * 
 * Convenience function to reload scenes from SD and update the scene list UI
 */
void scene_storage_reload_ui(void);

/**
 * @brief Reload scenes and update UI (no mutex - call from LVGL context only)
 * 
 * Use this when already running inside an LVGL callback to avoid deadlock.
 */
void scene_storage_reload_ui_no_lock(void);

/**
 * @brief Update an existing scene's properties
 * 
 * @param index Scene index (0-based)
 * @param new_name New scene name (can be same as original)
 * @param brightness New brightness value (0-255)
 * @param red New red value (0-255)
 * @param green New green value (0-255)
 * @param blue New blue value (0-255)
 * @param white New white value (0-255)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if index invalid,
 *                   ESP_ERR_INVALID_STATE if name already exists (different scene)
 */
esp_err_t scene_storage_update(size_t index, const char *new_name,
                               uint8_t brightness, uint8_t red, uint8_t green,
                               uint8_t blue, uint8_t white);

/**
 * @brief Move a scene to a new position (reorder)
 * 
 * @param from_index Current position (0-based)
 * @param to_index Target position (0-based)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if indices invalid
 */
esp_err_t scene_storage_reorder(size_t from_index, size_t to_index);

/**
 * @brief Get a scene by index
 * 
 * @param index Scene index (0-based)
 * @param scene Output: scene data
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_ARG if index invalid
 */
esp_err_t scene_storage_get_by_index(size_t index, ui_scene_t *scene);

#ifdef __cplusplus
}
#endif

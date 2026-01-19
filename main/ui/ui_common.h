/**
 * @file ui_common.h
 * @brief Common UI definitions and initialization
 */

#pragma once

#include "lvgl.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LVGL configuration
 */
#define UI_LVGL_TASK_PRIORITY       CONFIG_LVGL_TASK_PRIORITY
#define UI_LVGL_TASK_STACK_SIZE_KB  CONFIG_LVGL_TASK_STACK_SIZE_KB
#define UI_LVGL_TICK_PERIOD_MS      CONFIG_LVGL_TICK_PERIOD_MS
#define UI_LVGL_TASK_MAX_DELAY_MS   CONFIG_LVGL_TASK_MAX_DELAY_MS
#define UI_LVGL_TASK_MIN_DELAY_MS   CONFIG_LVGL_TASK_MIN_DELAY_MS

/**
 * @brief Scene structure for scene selector
 */
typedef struct {
    char name[32];      ///< Scene name
    uint8_t brightness; ///< Brightness value (0-255)
    uint8_t red;        ///< Red value (0-255)
    uint8_t green;      ///< Green value (0-255)
    uint8_t blue;       ///< Blue value (0-255)
    uint8_t white;      ///< White value (0-255)
} ui_scene_t;

/**
 * @brief Initialize LVGL with LCD and touch
 * 
 * @param lcd_panel LCD panel handle from board driver
 * @param touch Touch controller handle from board driver
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ui_init(lv_disp_t **disp, lv_indev_t **touch_indev);

/**
 * @brief Show splash screen
 * 
 * @param disp LVGL display object
 */
void ui_show_splash(lv_disp_t *disp);

/**
 * @brief Show main UI (tabs with Manual and Scenes)
 */
void ui_show_main(void);

/**
 * @brief Lock LVGL mutex (for non-UI task access)
 * 
 * @param timeout_ms Timeout in milliseconds (default: portMAX_DELAY)
 * @return true if locked successfully
 */
bool ui_lock(void);

/**
 * @brief Unlock LVGL mutex
 */
void ui_unlock(void);

// ----- Main Screen Functions -----

/**
 * @brief Create the main screen with tabview
 */
void ui_create_main_screen(void);

/**
 * @brief Get the manual control tab object
 */
lv_obj_t* ui_get_manual_tab(void);

/**
 * @brief Get the scene selector tab object
 */
lv_obj_t* ui_get_scenes_tab(void);

// ----- Manual Control Tab Functions -----

/**
 * @brief Create the manual control tab content (FR-020)
 */
void ui_create_manual_tab(lv_obj_t *parent);

/**
 * @brief Get current manual control values
 */
void ui_manual_get_values(uint8_t *brightness, uint8_t *red, uint8_t *green, 
                          uint8_t *blue, uint8_t *white);

/**
 * @brief Set manual control values (updates sliders)
 */
void ui_manual_set_values(uint8_t brightness, uint8_t red, uint8_t green, 
                          uint8_t blue, uint8_t white);

/**
 * @brief Calculate display RGB from RGBW + brightness (additive light mixing)
 * 
 * For RGBW LEDs:
 * - RGB channels mix additively (R+G=Yellow, R+B=Magenta, G+B=Cyan)
 * - White LED adds equally to all RGB channels
 * - Brightness is a master dimmer applied to all
 * 
 * @param brightness Master brightness (0-255)
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @param w White channel (0-255)
 * @return lv_color_t Display color for preview
 */
lv_color_t ui_calculate_preview_color(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// ----- Scene Selector Tab Functions -----

/**
 * @brief Create the scene selector tab content (FR-040)
 */
void ui_create_scenes_tab(lv_obj_t *parent);

/**
 * @brief Load scenes from SD card and populate the list (FR-040)
 * 
 * @param scenes Array of scene structs
 * @param count Number of scenes
 */
void ui_scenes_load_from_sd(const ui_scene_t *scenes, size_t count);

/**
 * @brief Update transition progress bar (FR-043)
 * 
 * @param percent Progress percentage (0-100)
 */
void ui_scenes_update_progress(uint8_t percent);

/**
 * @brief Start the progress bar tracking for a fade in progress
 * 
 * Shows the progress bar and starts a timer to update it
 * based on the fade controller's progress.
 */
void ui_scenes_start_progress_tracking(void);

/**
 * @brief Get current selected scene index
 */
int ui_scenes_get_selected_index(void);

/**
 * @brief Get current transition duration
 */
uint16_t ui_scenes_get_duration_sec(void);

#ifdef __cplusplus
}
#endif

/**
 * @file screen_timeout.h
 * @brief Screen Backlight Timeout and Power Saving
 * 
 * Provides automatic screen backlight timeout after a period of inactivity,
 * with touch-to-wake functionality to restore the display.
 * 
 * Features:
 * - Configurable timeout duration via LCC CDI
 * - Touch-to-wake restores backlight immediately
 * - Timeout can be disabled (set to 0)
 * - Thread-safe activity notification
 * 
 * @see docs/SPEC.md for power saving requirements
 * @see lcc_config.hxx for CDI configuration
 */

#ifndef SCREEN_TIMEOUT_H_
#define SCREEN_TIMEOUT_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "ch422g.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default screen timeout in seconds (0 = disabled)
 * 
 * Default is 60 seconds (1 minute). Set to 0 to disable timeout.
 */
#define SCREEN_TIMEOUT_DEFAULT_SEC  60

/**
 * @brief Minimum screen timeout in seconds (when enabled)
 */
#define SCREEN_TIMEOUT_MIN_SEC      10

/**
 * @brief Maximum screen timeout in seconds
 */
#define SCREEN_TIMEOUT_MAX_SEC      3600

/**
 * @brief Screen timeout configuration
 */
typedef struct {
    ch422g_handle_t ch422g_handle;  ///< CH422G handle for backlight control
    uint16_t timeout_sec;           ///< Timeout in seconds (0 = disabled)
} screen_timeout_config_t;

/**
 * @brief Initialize the screen timeout module
 * 
 * @param config Configuration structure
 * @return ESP_OK on success
 */
esp_err_t screen_timeout_init(const screen_timeout_config_t *config);

/**
 * @brief Deinitialize the screen timeout module
 */
void screen_timeout_deinit(void);

/**
 * @brief Notify activity to reset timeout timer
 * 
 * Call this function whenever user activity is detected (touch events).
 * If the screen is off, this will turn it back on.
 * Thread-safe: can be called from any task.
 */
void screen_timeout_notify_activity(void);

/**
 * @brief Set the timeout duration
 * 
 * @param timeout_sec Timeout in seconds (0 to disable)
 */
void screen_timeout_set_duration(uint16_t timeout_sec);

/**
 * @brief Get the current timeout duration
 * 
 * @return Timeout in seconds (0 if disabled)
 */
uint16_t screen_timeout_get_duration(void);

/**
 * @brief Check if screen is currently on
 * 
 * @return true if backlight is on, false if off
 */
bool screen_timeout_is_screen_on(void);

/**
 * @brief Manually turn screen on
 * 
 * Also resets the timeout timer.
 */
void screen_timeout_wake(void);

/**
 * @brief Manually turn screen off
 */
void screen_timeout_sleep(void);

/**
 * @brief Process timeout (called periodically from main loop or timer)
 * 
 * Call this function periodically (every 100-1000ms) to check for timeout.
 * Alternatively, the module can create its own timer task.
 */
void screen_timeout_tick(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_TIMEOUT_H_

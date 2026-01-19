/**
 * @file fade_controller.h
 * @brief Lighting Fade Controller
 * 
 * Manages smooth transitions between lighting states with rate-limited
 * LCC event transmission. Implements linear interpolation with fractional
 * accumulation to ensure accurate endpoint delivery.
 * 
 * @see docs/ARCHITECTURE.md §6 for Fade Algorithm specification
 * @see docs/SPEC.md §3 for LCC Event Model
 */

#ifndef FADE_CONTROLLER_H_
#define FADE_CONTROLLER_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lighting parameter indices
 */
typedef enum {
    LIGHT_PARAM_RED = 0,
    LIGHT_PARAM_GREEN = 1,
    LIGHT_PARAM_BLUE = 2,
    LIGHT_PARAM_WHITE = 3,
    LIGHT_PARAM_BRIGHTNESS = 4,
    LIGHT_PARAM_COUNT = 5
} light_param_t;

/**
 * @brief Fade controller state
 */
typedef enum {
    FADE_STATE_IDLE = 0,    ///< No active fade
    FADE_STATE_FADING,      ///< Fade in progress
    FADE_STATE_COMPLETE     ///< Fade just completed (transitions to IDLE on next tick)
} fade_state_t;

/**
 * @brief Lighting state (all 5 parameters)
 */
typedef struct {
    uint8_t brightness;     ///< Master brightness (0-255)
    uint8_t red;            ///< Red channel (0-255)
    uint8_t green;          ///< Green channel (0-255)
    uint8_t blue;           ///< Blue channel (0-255)
    uint8_t white;          ///< White channel (0-255)
} lighting_state_t;

/**
 * @brief Fade parameters for initiating a transition
 */
typedef struct {
    lighting_state_t target;    ///< Target lighting state
    uint32_t duration_ms;       ///< Fade duration in milliseconds (0 = instant)
} fade_params_t;

/**
 * @brief Fade progress information
 */
typedef struct {
    fade_state_t state;         ///< Current fade state
    uint8_t progress_percent;   ///< Progress 0-100%
    uint32_t elapsed_ms;        ///< Elapsed time in ms
    uint32_t total_ms;          ///< Total duration in ms
    lighting_state_t current;   ///< Current lighting values
} fade_progress_t;

/**
 * @brief Initialize the fade controller
 * 
 * Must be called after lcc_node_init() to ensure LCC stack is ready.
 * 
 * @return ESP_OK on success
 */
esp_err_t fade_controller_init(void);

/**
 * @brief Start a fade transition to target state
 * 
 * If a fade is already in progress, it will be cancelled and the new
 * fade will start from the current interpolated values.
 * 
 * @param params Fade parameters (target state and duration)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if params is NULL
 */
esp_err_t fade_controller_start(const fade_params_t *params);

/**
 * @brief Apply lighting state immediately (no fade)
 * 
 * Equivalent to fade_controller_start() with duration_ms = 0.
 * Transmits all 5 parameters with proper rate limiting and ordering.
 * 
 * @param state Lighting state to apply
 * @return ESP_OK on success
 */
esp_err_t fade_controller_apply_immediate(const lighting_state_t *state);

/**
 * @brief Process fade controller tick
 * 
 * Must be called periodically (recommended: every 10-20ms) to drive
 * the fade state machine and transmit LCC events.
 * 
 * This function handles:
 * - Time-based interpolation
 * - Rate-limited event transmission (20ms minimum interval)
 * - Transmission ordering (Brightness first, then R, G, B, W)
 * 
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not initialized
 */
esp_err_t fade_controller_tick(void);

/**
 * @brief Get current fade progress
 * 
 * @param[out] progress Progress information (may be NULL to just check state)
 * @return Current fade state
 */
fade_state_t fade_controller_get_progress(fade_progress_t *progress);

/**
 * @brief Check if a fade is currently active
 * 
 * @return true if fading, false if idle or complete
 */
bool fade_controller_is_active(void);

/**
 * @brief Abort any active fade
 * 
 * Stops the fade immediately at current values. Does not transmit
 * any additional events.
 */
void fade_controller_abort(void);

/**
 * @brief Get current lighting state
 * 
 * Returns the last transmitted/known lighting values.
 * 
 * @param[out] state Current lighting state
 * @return ESP_OK on success
 */
esp_err_t fade_controller_get_current(lighting_state_t *state);

/**
 * @brief Set current lighting state without transmission
 * 
 * Used to initialize the controller with known values (e.g., from saved state).
 * Does not transmit any LCC events.
 * 
 * @param state Lighting state to set as current
 * @return ESP_OK on success
 */
esp_err_t fade_controller_set_current(const lighting_state_t *state);

#ifdef __cplusplus
}
#endif

#endif // FADE_CONTROLLER_H_

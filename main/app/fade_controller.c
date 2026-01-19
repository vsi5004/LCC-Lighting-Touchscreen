/**
 * @file fade_controller.c
 * @brief Lighting Fade Controller Implementation
 * 
 * Implements smooth linear transitions between lighting states with:
 * - Rate-limited LCC event transmission (minimum 20ms between events)
 * - Fractional accumulation for accurate endpoint delivery
 * - Proper transmission order (Brightness first, then R, G, B, W)
 * 
 * @see docs/ARCHITECTURE.md §6 for Fade Algorithm specification
 */

#include "fade_controller.h"
#include "lcc_node.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "fade_ctrl";

/// Minimum interval between LCC event transmissions (ms)
/// Lower values = smoother fades but more CAN bus traffic
#define MIN_TX_INTERVAL_MS  10

/// Transmission order: Brightness first, then RGBW
static const light_param_t TX_ORDER[LIGHT_PARAM_COUNT] = {
    LIGHT_PARAM_BRIGHTNESS,
    LIGHT_PARAM_RED,
    LIGHT_PARAM_GREEN,
    LIGHT_PARAM_BLUE,
    LIGHT_PARAM_WHITE
};

/**
 * @brief Internal fade state
 */
typedef struct {
    bool initialized;
    
    // Current state (last transmitted values)
    lighting_state_t current;
    
    // Fade state machine
    fade_state_t state;
    
    // Fade parameters
    lighting_state_t start;         // Starting values for current fade
    lighting_state_t target;        // Target values
    uint32_t duration_ms;           // Total fade duration
    
    // Timing
    int64_t fade_start_us;          // Timestamp when fade started
    int64_t last_tx_us;             // Timestamp of last transmission
    
    // Interpolation state (fixed-point for accuracy)
    float current_float[LIGHT_PARAM_COUNT];  // Current interpolated values (float)
    
    // Transmission state
    int next_param_index;           // Next parameter to transmit (0-4)
    bool tx_pending;                // True if we have values to transmit
    
} fade_state_internal_t;

static fade_state_internal_t s_fade = {0};

/**
 * @brief Get parameter value from lighting_state_t by index
 */
static uint8_t get_param(const lighting_state_t *state, light_param_t param)
{
    switch (param) {
        case LIGHT_PARAM_RED:        return state->red;
        case LIGHT_PARAM_GREEN:      return state->green;
        case LIGHT_PARAM_BLUE:       return state->blue;
        case LIGHT_PARAM_WHITE:      return state->white;
        case LIGHT_PARAM_BRIGHTNESS: return state->brightness;
        default:                     return 0;
    }
}

/**
 * @brief Set parameter value in lighting_state_t by index
 */
static void set_param(lighting_state_t *state, light_param_t param, uint8_t value)
{
    switch (param) {
        case LIGHT_PARAM_RED:        state->red = value; break;
        case LIGHT_PARAM_GREEN:      state->green = value; break;
        case LIGHT_PARAM_BLUE:       state->blue = value; break;
        case LIGHT_PARAM_WHITE:      state->white = value; break;
        case LIGHT_PARAM_BRIGHTNESS: state->brightness = value; break;
        default: break;
    }
}

/**
 * @brief Transmit a single parameter via LCC
 */
static esp_err_t transmit_param(light_param_t param, uint8_t value)
{
    static const char *param_names[] = {"R", "G", "B", "W", "Brightness"};
    ESP_LOGD(TAG, "TX %s=%d", param_names[param], value);
    
    // Map our param enum to LCC parameter index (they happen to match)
    return lcc_node_send_lighting_event((uint8_t)param, value);
}

esp_err_t fade_controller_init(void)
{
    if (s_fade.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    memset(&s_fade, 0, sizeof(s_fade));
    s_fade.state = FADE_STATE_IDLE;
    s_fade.initialized = true;
    
    ESP_LOGI(TAG, "Fade controller initialized");
    return ESP_OK;
}

esp_err_t fade_controller_start(const fade_params_t *params)
{
    if (!s_fade.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Cancel any active fade
    if (s_fade.state == FADE_STATE_FADING) {
        ESP_LOGI(TAG, "Cancelling active fade");
    }
    
    // Store start and target
    s_fade.start = s_fade.current;
    s_fade.target = params->target;
    s_fade.duration_ms = params->duration_ms;
    
    // Initialize float values from current
    for (int i = 0; i < LIGHT_PARAM_COUNT; i++) {
        s_fade.current_float[i] = (float)get_param(&s_fade.current, (light_param_t)i);
    }
    
    // If duration is 0, apply immediately
    if (params->duration_ms == 0) {
        ESP_LOGI(TAG, "Immediate apply: B=%d R=%d G=%d B=%d W=%d",
                 params->target.brightness, params->target.red,
                 params->target.green, params->target.blue, params->target.white);
        
        s_fade.current = params->target;
        s_fade.state = FADE_STATE_FADING;
        s_fade.tx_pending = true;
        s_fade.next_param_index = 0;
        s_fade.fade_start_us = esp_timer_get_time();
        
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting fade over %lu ms: B=%d->%d R=%d->%d G=%d->%d B=%d->%d W=%d->%d",
             (unsigned long)params->duration_ms,
             s_fade.start.brightness, params->target.brightness,
             s_fade.start.red, params->target.red,
             s_fade.start.green, params->target.green,
             s_fade.start.blue, params->target.blue,
             s_fade.start.white, params->target.white);
    
    s_fade.state = FADE_STATE_FADING;
    s_fade.fade_start_us = esp_timer_get_time();
    s_fade.tx_pending = true;
    s_fade.next_param_index = 0;
    
    return ESP_OK;
}

esp_err_t fade_controller_apply_immediate(const lighting_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    fade_params_t params = {
        .target = *state,
        .duration_ms = 0
    };
    
    return fade_controller_start(&params);
}

esp_err_t fade_controller_tick(void)
{
    if (!s_fade.initialized) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (s_fade.state == FADE_STATE_IDLE) {
        return ESP_OK;
    }
    
    if (s_fade.state == FADE_STATE_COMPLETE) {
        // Transition to idle
        s_fade.state = FADE_STATE_IDLE;
        return ESP_OK;
    }
    
    // FADING state
    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_us = now_us - s_fade.fade_start_us;
    uint32_t elapsed_ms = (uint32_t)(elapsed_us / 1000);
    
    // Calculate progress (0.0 to 1.0)
    float progress;
    if (s_fade.duration_ms == 0) {
        progress = 1.0f;
    } else {
        progress = (float)elapsed_ms / (float)s_fade.duration_ms;
        if (progress > 1.0f) {
            progress = 1.0f;
        }
    }
    
    // Interpolate all channels
    bool values_changed = false;
    for (int i = 0; i < LIGHT_PARAM_COUNT; i++) {
        light_param_t param = (light_param_t)i;
        float start_val = (float)get_param(&s_fade.start, param);
        float target_val = (float)get_param(&s_fade.target, param);
        float new_val = start_val + (target_val - start_val) * progress;
        
        // Check if integer value changed
        uint8_t old_int = (uint8_t)roundf(s_fade.current_float[i]);
        uint8_t new_int = (uint8_t)roundf(new_val);
        
        if (old_int != new_int) {
            values_changed = true;
        }
        
        s_fade.current_float[i] = new_val;
        set_param(&s_fade.current, param, new_int);
    }
    
    // Transmit if values changed, have pending transmissions, 
    // OR fade is complete but we haven't finished transmitting all params
    bool need_finish_tx = (progress >= 1.0f && s_fade.next_param_index != 0);
    if (values_changed || s_fade.tx_pending || need_finish_tx) {
        // Check rate limit (time since last transmission round started)
        if ((now_us - s_fade.last_tx_us) >= (MIN_TX_INTERVAL_MS * 1000)) {
            // Transmit all parameters in one burst for smoother fades
            // CAN bus can handle 5 frames in quick succession
            bool any_sent = false;
            while (s_fade.next_param_index < LIGHT_PARAM_COUNT) {
                light_param_t param = TX_ORDER[s_fade.next_param_index];
                uint8_t value = get_param(&s_fade.current, param);
                
                esp_err_t err = transmit_param(param, value);
                if (err == ESP_OK) {
                    any_sent = true;
                    s_fade.next_param_index++;
                    // Continue to next param - send all 5 in one burst
                } else if (err == ESP_ERR_INVALID_STATE) {
                    // LCC not ready, try again next tick
                    break;
                } else {
                    // Skip failed parameter
                    s_fade.next_param_index++;
                }
            }
            
            if (any_sent) {
                s_fade.last_tx_us = esp_timer_get_time();
            }
            
            // Reset for next cycle if we've transmitted all
            if (s_fade.next_param_index >= LIGHT_PARAM_COUNT) {
                s_fade.next_param_index = 0;
                s_fade.tx_pending = false;
            }
        }
    }
    
    // Check if fade is complete
    if (progress >= 1.0f && !s_fade.tx_pending && s_fade.next_param_index == 0) {
        // Ensure we transmit final values
        bool all_at_target = true;
        for (int i = 0; i < LIGHT_PARAM_COUNT; i++) {
            light_param_t param = (light_param_t)i;
            if (get_param(&s_fade.current, param) != get_param(&s_fade.target, param)) {
                all_at_target = false;
                break;
            }
        }
        
        if (all_at_target) {
            ESP_LOGI(TAG, "Fade complete");
            s_fade.state = FADE_STATE_COMPLETE;
        } else {
            // Need one more round to hit exact targets
            s_fade.current = s_fade.target;
            s_fade.tx_pending = true;
        }
    }
    
    return ESP_OK;
}

fade_state_t fade_controller_get_progress(fade_progress_t *progress)
{
    if (!s_fade.initialized) {
        if (progress) {
            memset(progress, 0, sizeof(*progress));
        }
        return FADE_STATE_IDLE;
    }
    
    if (progress) {
        progress->state = s_fade.state;
        progress->current = s_fade.current;
        progress->total_ms = s_fade.duration_ms;
        
        if (s_fade.state == FADE_STATE_FADING) {
            int64_t elapsed_us = esp_timer_get_time() - s_fade.fade_start_us;
            progress->elapsed_ms = (uint32_t)(elapsed_us / 1000);
            if (progress->elapsed_ms > progress->total_ms) {
                progress->elapsed_ms = progress->total_ms;
            }
            
            if (progress->total_ms > 0) {
                progress->progress_percent = (uint8_t)((progress->elapsed_ms * 100) / progress->total_ms);
            } else {
                progress->progress_percent = 100;
            }
        } else if (s_fade.state == FADE_STATE_COMPLETE) {
            progress->elapsed_ms = progress->total_ms;
            progress->progress_percent = 100;
        } else {
            progress->elapsed_ms = 0;
            progress->progress_percent = 0;
        }
    }
    
    return s_fade.state;
}

bool fade_controller_is_active(void)
{
    return s_fade.initialized && s_fade.state == FADE_STATE_FADING;
}

void fade_controller_abort(void)
{
    if (!s_fade.initialized) {
        return;
    }
    
    if (s_fade.state == FADE_STATE_FADING) {
        ESP_LOGI(TAG, "Fade aborted at B=%d R=%d G=%d B=%d W=%d",
                 s_fade.current.brightness, s_fade.current.red,
                 s_fade.current.green, s_fade.current.blue, s_fade.current.white);
    }
    
    s_fade.state = FADE_STATE_IDLE;
    s_fade.tx_pending = false;
}

esp_err_t fade_controller_get_current(lighting_state_t *state)
{
    if (!s_fade.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *state = s_fade.current;
    return ESP_OK;
}

esp_err_t fade_controller_set_current(const lighting_state_t *state)
{
    if (!s_fade.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_fade.current = *state;
    
    // Update float values
    for (int i = 0; i < LIGHT_PARAM_COUNT; i++) {
        s_fade.current_float[i] = (float)get_param(&s_fade.current, (light_param_t)i);
    }
    
    ESP_LOGI(TAG, "Current state set: B=%d R=%d G=%d B=%d W=%d",
             state->brightness, state->red, state->green, state->blue, state->white);
    
    return ESP_OK;
}

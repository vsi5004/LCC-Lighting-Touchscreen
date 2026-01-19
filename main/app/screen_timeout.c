/**
 * @file screen_timeout.c
 * @brief Screen Backlight Timeout and Power Saving Implementation
 * 
 * Implements automatic screen timeout with touch-to-wake functionality
 * for power saving when the device is idle. Features a smooth 1-second
 * fade-to-black transition before turning off the backlight.
 */

#include "screen_timeout.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "ui/ui_common.h"

static const char *TAG = "screen_timeout";

/// Fade animation duration in milliseconds
#define FADE_DURATION_MS    1000

/// Number of discrete opacity steps for fade animation
/// Using fewer steps that align with frame boundaries reduces banding
/// At 60fps, 1000ms = 60 frames. 20 steps = opacity change every 3 frames
#define FADE_OPACITY_STEPS  20

/// Screen state machine
typedef enum {
    SCREEN_STATE_ACTIVE,        ///< Screen is on and active
    SCREEN_STATE_FADING_OUT,    ///< Fading to black before sleep
    SCREEN_STATE_OFF,           ///< Screen is off (backlight off)
    SCREEN_STATE_FADING_IN,     ///< Fading in after wake
} screen_state_t;

// Forward declarations for animation callbacks
static void fade_out_complete_cb(lv_anim_t *anim);
static void fade_in_complete_cb(lv_anim_t *anim);

/// Module state
static struct {
    ch422g_handle_t ch422g;         ///< CH422G handle for backlight control
    uint16_t timeout_sec;           ///< Timeout duration (0 = disabled)
    int64_t last_activity_us;       ///< Timestamp of last activity (microseconds)
    screen_state_t state;           ///< Current screen state
    bool initialized;               ///< Module initialized flag
    SemaphoreHandle_t mutex;        ///< Thread safety mutex
    lv_obj_t *fade_overlay;         ///< Black overlay for fade effect
    lv_anim_t fade_anim;            ///< Fade animation
    bool pending_wake;              ///< Touch occurred during fade-out or when off
} s_state = {
    .ch422g = NULL,
    .timeout_sec = SCREEN_TIMEOUT_DEFAULT_SEC,
    .last_activity_us = 0,
    .state = SCREEN_STATE_ACTIVE,
    .initialized = false,
    .mutex = NULL,
    .fade_overlay = NULL,
    .pending_wake = false,
};

/**
 * @brief Turn backlight on via CH422G
 */
static esp_err_t backlight_on(void)
{
    if (s_state.ch422g == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ch422g_backlight_on(s_state.ch422g);
}

/**
 * @brief Turn backlight off via CH422G
 */
static esp_err_t backlight_off(void)
{
    if (s_state.ch422g == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ch422g_backlight_off(s_state.ch422g);
}

/**
 * @brief Animation callback - sets overlay opacity using stepped values
 * Uses discrete steps to reduce banding artifacts caused by mid-frame opacity changes.
 * The stepped approach ensures opacity only changes at specific intervals,
 * giving the display time to complete full frames at each opacity level.
 */
static void fade_anim_cb(void *obj, int32_t value)
{
    if (s_state.fade_overlay != NULL) {
        // Quantize to discrete steps to reduce banding
        // This ensures opacity changes happen less frequently, allowing
        // complete frames to render at each opacity level
        int step = (value * FADE_OPACITY_STEPS) / LV_OPA_COVER;
        lv_opa_t stepped_opa = (step * LV_OPA_COVER) / FADE_OPACITY_STEPS;
        lv_obj_set_style_bg_opa(s_state.fade_overlay, stepped_opa, 0);
    }
}

/**
 * @brief Fade-out complete callback
 * Called from LVGL context when fade-out animation finishes
 */
static void fade_out_complete_cb(lv_anim_t *anim)
{
    ESP_LOGI(TAG, "Fade-out complete, turning off backlight");
    
    // Check if a wake was requested during the fade
    if (s_state.pending_wake) {
        s_state.pending_wake = false;
        ESP_LOGI(TAG, "Wake requested during fade-out, waking immediately");
        // Start fade-in instead
        s_state.state = SCREEN_STATE_FADING_IN;
        
        lv_anim_init(&s_state.fade_anim);
        lv_anim_set_var(&s_state.fade_anim, s_state.fade_overlay);
        lv_anim_set_exec_cb(&s_state.fade_anim, fade_anim_cb);
        lv_anim_set_values(&s_state.fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&s_state.fade_anim, FADE_DURATION_MS);
        lv_anim_set_ready_cb(&s_state.fade_anim, fade_in_complete_cb);
        lv_anim_start(&s_state.fade_anim);
        return;
    }
    
    // Turn off backlight
    backlight_off();
    s_state.state = SCREEN_STATE_OFF;
    
    // Hide overlay (it's fully opaque now, but hidden saves resources)
    if (s_state.fade_overlay != NULL) {
        lv_obj_add_flag(s_state.fade_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Fade-in complete callback
 * Called from LVGL context when fade-in animation finishes
 */
static void fade_in_complete_cb(lv_anim_t *anim)
{
    ESP_LOGI(TAG, "Fade-in complete");
    s_state.state = SCREEN_STATE_ACTIVE;
    
    // Hide the fully transparent overlay
    if (s_state.fade_overlay != NULL) {
        lv_obj_add_flag(s_state.fade_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Create the fade overlay object (must be called from LVGL context)
 */
static void create_fade_overlay(void)
{
    if (s_state.fade_overlay != NULL) {
        return;  // Already created
    }
    
    // Create full-screen black overlay on the top layer
    lv_obj_t *layer = lv_layer_top();
    s_state.fade_overlay = lv_obj_create(layer);
    lv_obj_remove_style_all(s_state.fade_overlay);
    lv_obj_set_size(s_state.fade_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_state.fade_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_state.fade_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_state.fade_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_state.fade_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_state.fade_overlay, LV_OBJ_FLAG_HIDDEN);
    
    ESP_LOGI(TAG, "Fade overlay created");
}

/**
 * @brief Start fade-out animation (must be called from LVGL context)
 */
static void start_fade_out(void)
{
    if (s_state.fade_overlay == NULL) {
        create_fade_overlay();
    }
    
    ESP_LOGI(TAG, "Starting fade-out animation");
    s_state.state = SCREEN_STATE_FADING_OUT;
    s_state.pending_wake = false;
    
    // Show overlay and start animation
    lv_obj_clear_flag(s_state.fade_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(s_state.fade_overlay, LV_OPA_TRANSP, 0);
    
    lv_anim_init(&s_state.fade_anim);
    lv_anim_set_var(&s_state.fade_anim, s_state.fade_overlay);
    lv_anim_set_exec_cb(&s_state.fade_anim, fade_anim_cb);
    lv_anim_set_values(&s_state.fade_anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&s_state.fade_anim, FADE_DURATION_MS);
    lv_anim_set_ready_cb(&s_state.fade_anim, fade_out_complete_cb);
    lv_anim_start(&s_state.fade_anim);
}

/**
 * @brief Start fade-in animation (must be called from LVGL context)
 */
static void start_fade_in(void)
{
    if (s_state.fade_overlay == NULL) {
        create_fade_overlay();
    }
    
    ESP_LOGI(TAG, "Starting fade-in animation");
    s_state.state = SCREEN_STATE_FADING_IN;
    
    // Ensure backlight is on
    backlight_on();
    
    // Show overlay at full opacity and fade out
    lv_obj_clear_flag(s_state.fade_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(s_state.fade_overlay, LV_OPA_COVER, 0);
    
    lv_anim_init(&s_state.fade_anim);
    lv_anim_set_var(&s_state.fade_anim, s_state.fade_overlay);
    lv_anim_set_exec_cb(&s_state.fade_anim, fade_anim_cb);
    lv_anim_set_values(&s_state.fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&s_state.fade_anim, FADE_DURATION_MS);
    lv_anim_set_ready_cb(&s_state.fade_anim, fade_in_complete_cb);
    lv_anim_start(&s_state.fade_anim);
}

esp_err_t screen_timeout_init(const screen_timeout_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_state.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    s_state.ch422g = config->ch422g_handle;
    s_state.timeout_sec = config->timeout_sec;
    s_state.last_activity_us = esp_timer_get_time();
    s_state.state = SCREEN_STATE_ACTIVE;
    s_state.initialized = true;
    s_state.fade_overlay = NULL;
    s_state.pending_wake = false;
    
    // Create overlay in LVGL context
    if (ui_lock()) {
        create_fade_overlay();
        ui_unlock();
    }
    
    ESP_LOGI(TAG, "Initialized with timeout=%u sec (0=disabled), fade=%dms", 
             s_state.timeout_sec, FADE_DURATION_MS);
    
    return ESP_OK;
}

void screen_timeout_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    // Delete overlay in LVGL context
    if (ui_lock()) {
        if (s_state.fade_overlay != NULL) {
            lv_anim_del(s_state.fade_overlay, NULL);
            lv_obj_del(s_state.fade_overlay);
            s_state.fade_overlay = NULL;
        }
        ui_unlock();
    }
    
    if (s_state.mutex != NULL) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }
    
    s_state.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

void screen_timeout_notify_activity(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_state.last_activity_us = esp_timer_get_time();
        
        switch (s_state.state) {
            case SCREEN_STATE_OFF:
                // Wake screen with fade-in - set flag for tick() to handle
                ESP_LOGI(TAG, "Touch detected - waking screen");
                s_state.pending_wake = true;
                break;
                
            case SCREEN_STATE_FADING_OUT:
                // Abort fade-out, will transition to fade-in
                ESP_LOGI(TAG, "Touch during fade-out - will wake");
                s_state.pending_wake = true;
                break;
                
            case SCREEN_STATE_FADING_IN:
            case SCREEN_STATE_ACTIVE:
                // Already on or waking, just reset timer
                break;
        }
        
        xSemaphoreGive(s_state.mutex);
    }
}

void screen_timeout_set_duration(uint16_t timeout_sec)
{
    if (!s_state.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        uint16_t old_timeout = s_state.timeout_sec;
        s_state.timeout_sec = timeout_sec;
        
        // Clamp to valid range (0 or min-max)
        if (s_state.timeout_sec != 0 && s_state.timeout_sec < SCREEN_TIMEOUT_MIN_SEC) {
            s_state.timeout_sec = SCREEN_TIMEOUT_MIN_SEC;
        } else if (s_state.timeout_sec > SCREEN_TIMEOUT_MAX_SEC) {
            s_state.timeout_sec = SCREEN_TIMEOUT_MAX_SEC;
        }
        
        if (old_timeout != s_state.timeout_sec) {
            ESP_LOGI(TAG, "Timeout changed: %u -> %u sec", old_timeout, s_state.timeout_sec);
        }
        
        // Reset timer when duration changes
        s_state.last_activity_us = esp_timer_get_time();
        
        xSemaphoreGive(s_state.mutex);
    }
}

uint16_t screen_timeout_get_duration(void)
{
    uint16_t timeout = 0;
    
    if (s_state.initialized && 
        xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        timeout = s_state.timeout_sec;
        xSemaphoreGive(s_state.mutex);
    }
    
    return timeout;
}

bool screen_timeout_is_screen_on(void)
{
    bool on = true;
    
    if (s_state.initialized && 
        xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        on = (s_state.state == SCREEN_STATE_ACTIVE || 
              s_state.state == SCREEN_STATE_FADING_IN ||
              s_state.state == SCREEN_STATE_FADING_OUT);
        xSemaphoreGive(s_state.mutex);
    }
    
    return on;
}

void screen_timeout_wake(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_state.last_activity_us = esp_timer_get_time();
        
        if (s_state.state == SCREEN_STATE_OFF || 
            s_state.state == SCREEN_STATE_FADING_OUT) {
            ESP_LOGI(TAG, "Manual wake");
            s_state.pending_wake = true;
        }
        
        xSemaphoreGive(s_state.mutex);
    }
}

void screen_timeout_sleep(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_state.state == SCREEN_STATE_ACTIVE) {
            ESP_LOGI(TAG, "Manual sleep - starting fade-out");
            // Force timeout on next tick by setting last activity to 0
            s_state.last_activity_us = 0;
        }
        
        xSemaphoreGive(s_state.mutex);
    }
}

void screen_timeout_tick(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Handle pending wake when screen is off (needs LVGL context)
        if (s_state.pending_wake && s_state.state == SCREEN_STATE_OFF) {
            s_state.pending_wake = false;
            xSemaphoreGive(s_state.mutex);
            
            // Start fade-in in LVGL context
            if (ui_lock()) {
                start_fade_in();
                ui_unlock();
            }
            return;
        }
        
        // Skip if timeout is disabled
        if (s_state.timeout_sec == 0) {
            xSemaphoreGive(s_state.mutex);
            return;
        }
        
        // Only check timeout when in ACTIVE state
        if (s_state.state != SCREEN_STATE_ACTIVE) {
            xSemaphoreGive(s_state.mutex);
            return;
        }
        
        // Check if timeout has elapsed
        int64_t now_us = esp_timer_get_time();
        int64_t elapsed_us = now_us - s_state.last_activity_us;
        int64_t timeout_us = (int64_t)s_state.timeout_sec * 1000000LL;
        
        if (elapsed_us >= timeout_us) {
            ESP_LOGI(TAG, "Timeout elapsed (%u sec) - starting fade-out", 
                     s_state.timeout_sec);
            xSemaphoreGive(s_state.mutex);
            
            // Start fade-out in LVGL context
            if (ui_lock()) {
                start_fade_out();
                ui_unlock();
            }
            return;
        }
        
        xSemaphoreGive(s_state.mutex);
    }
}

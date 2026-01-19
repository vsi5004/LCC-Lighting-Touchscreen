/**
 * @file ui_scenes.c
 * @brief Scene Selector Tab UI with Card Carousel
 * 
 * Implements FR-040 to FR-043:
 * - FR-040: Display swipeable scene carousel loaded from SD
 * - FR-041: Transition duration slider: 0–300 s
 * - FR-042: Apply performs linear fade to target scene
 * - FR-043: Progress bar reflects transition completion
 */

#include "ui_common.h"
#include "../app/scene_storage.h"
#include "../app/fade_controller.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_scenes";

// Card dimensions
#define CARD_WIDTH      240
#define CARD_HEIGHT     260
#define CARD_GAP        20
#define CAROUSEL_HEIGHT 260

// Scene selector state
static struct {
    int current_scene_index;
    uint16_t transition_duration_sec;
    bool transition_in_progress;
    bool fade_started;            // True once we've seen FADE_STATE_FADING
    bool pending_progress_start;  // Request from external task to start progress tracking
    char pending_delete_name[32];  // Scene name pending deletion
} s_scenes_state = {
    .current_scene_index = 0,
    .transition_duration_sec = 10,
    .transition_in_progress = false,
    .fade_started = false,
    .pending_progress_start = false,
    .pending_delete_name = ""
};

// Cached scenes for card access
static ui_scene_t s_cached_scenes[SCENE_STORAGE_MAX_SCENES];
static size_t s_cached_scene_count = 0;

// Card objects array for selection highlighting
static lv_obj_t *s_scene_cards[SCENE_STORAGE_MAX_SCENES];

// UI Objects
static lv_obj_t *s_carousel = NULL;
static lv_obj_t *s_slider_duration = NULL;
static lv_obj_t *s_label_duration = NULL;
static lv_obj_t *s_btn_apply = NULL;
static lv_obj_t *s_progress_bar = NULL;
static lv_obj_t *s_label_no_scenes = NULL;

// Progress bar update timer
static lv_timer_t *s_progress_timer = NULL;

// Delete confirmation modal
static lv_obj_t *s_delete_modal = NULL;

// Edit scene modal state
static struct {
    lv_obj_t *modal;
    lv_obj_t *name_textarea;
    lv_obj_t *keyboard;
    lv_obj_t *slider_brightness;
    lv_obj_t *slider_red;
    lv_obj_t *slider_green;
    lv_obj_t *slider_blue;
    lv_obj_t *slider_white;
    lv_obj_t *label_brightness;
    lv_obj_t *label_red;
    lv_obj_t *label_green;
    lv_obj_t *label_blue;
    lv_obj_t *label_white;
    lv_obj_t *color_preview;
    lv_obj_t *btn_move_left;
    lv_obj_t *btn_move_right;
    lv_obj_t *label_order_index;
    int scene_index;
    uint8_t brightness;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
} s_edit_state = {0};

/**
 * @brief Get ordinal suffix for a number (1st, 2nd, 3rd, 4th, etc.)
 */
static const char* get_ordinal_suffix(int n)
{
    if (n % 100 >= 11 && n % 100 <= 13) {
        return "th";
    }
    switch (n % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

/**
 * @brief Update the scene order index label in edit modal
 */
static void update_order_index_label(void)
{
    if (s_edit_state.label_order_index) {
        int pos = s_edit_state.scene_index + 1;  // 1-based for display
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%s", pos, get_ordinal_suffix(pos));
        lv_label_set_text(s_edit_state.label_order_index, buf);
    }
}

/**
 * @brief Update card selection visual - highlight selected card with blue border
 * Note: Cards have no shadows for scroll performance optimization
 */
static void update_card_selection(int selected_index)
{
    for (size_t i = 0; i < s_cached_scene_count; i++) {
        if (s_scene_cards[i]) {
            if ((int)i == selected_index) {
                // Selected: Material Blue border, thicker
                lv_obj_set_style_border_color(s_scene_cards[i], lv_color_make(33, 150, 243), LV_PART_MAIN);
                lv_obj_set_style_border_width(s_scene_cards[i], 4, LV_PART_MAIN);
            } else {
                // Unselected: light gray border
                lv_obj_set_style_border_color(s_scene_cards[i], lv_color_make(224, 224, 224), LV_PART_MAIN);
                lv_obj_set_style_border_width(s_scene_cards[i], 2, LV_PART_MAIN);
            }
        }
    }
}

/**
 * @brief Update duration label
 */
static void update_duration_label(uint16_t seconds)
{
    char buf[42];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "Transition Duration: %d sec", seconds);
    } else {
        snprintf(buf, sizeof(buf), "Transition Duration: %d min %d sec", seconds / 60, seconds % 60);
    }
    lv_label_set_text(s_label_duration, buf);
}

/**
 * @brief Duration slider event handler (FR-041)
 */
static void duration_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    s_scenes_state.transition_duration_sec = (uint16_t)value;
    update_duration_label(s_scenes_state.transition_duration_sec);
}

/**
 * @brief Progress bar update timer callback (FR-043)
 * 
 * Called periodically to update the progress bar during fades.
 * Also handles pending progress start requests from external tasks.
 */
static void progress_timer_cb(lv_timer_t *timer)
{
    // Check for pending progress start request from external task
    if (s_scenes_state.pending_progress_start) {
        s_scenes_state.pending_progress_start = false;
        s_scenes_state.transition_in_progress = true;
        s_scenes_state.fade_started = false;
        if (s_progress_bar) {
            lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
        }
        ESP_LOGI(TAG, "Progress tracking started from pending request");
    }
    
    // If we're not tracking a transition, nothing to do
    if (!s_scenes_state.transition_in_progress) {
        return;
    }
    
    fade_progress_t progress;
    fade_state_t state = fade_controller_get_progress(&progress);
    
    if (state == FADE_STATE_FADING) {
        // Mark that we've seen the fade actually start
        s_scenes_state.fade_started = true;
        // Update progress bar value (0-100)
        if (s_progress_bar) {
            lv_bar_set_value(s_progress_bar, progress.progress_percent, LV_ANIM_OFF);
        }
    } else if (s_scenes_state.fade_started) {
        // Only hide if we previously saw FADING state (now IDLE or COMPLETE)
        if (s_progress_bar) {
            lv_bar_set_value(s_progress_bar, 100, LV_ANIM_OFF);
            lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        s_scenes_state.transition_in_progress = false;
        s_scenes_state.fade_started = false;
        
        ESP_LOGI(TAG, "Fade complete, progress bar hidden");
    }
}

/**
 * @brief Start progress bar updates (called from within LVGL context)
 */
static void start_progress_updates(void)
{
    // Show the progress bar and reset value
    if (s_progress_bar) {
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    }
    s_scenes_state.transition_in_progress = true;
    s_scenes_state.fade_started = false;  // Will be set true when we see FADING
}

/**
 * @brief Start the progress bar tracking for a fade in progress (public API)
 * 
 * This is called from main.c (outside LVGL task context), so we just set
 * a pending flag that the progress timer will pick up on its next tick.
 */
void ui_scenes_start_progress_tracking(void)
{
    // Set pending flag - the persistent timer will pick this up
    s_scenes_state.pending_progress_start = true;
    ESP_LOGI(TAG, "Progress tracking requested (pending)");
}

/**
 * @brief Apply button event handler (FR-042)
 */
static void apply_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Apply button pressed");
    
    if (s_cached_scene_count > 0 && s_scenes_state.current_scene_index < (int)s_cached_scene_count) {
        ui_scene_t *scene = &s_cached_scenes[s_scenes_state.current_scene_index];
        ESP_LOGI(TAG, "Applying scene '%s': B=%d R=%d G=%d B=%d W=%d, Duration=%d sec",
                 scene->name, scene->brightness, scene->red, scene->green,
                 scene->blue, scene->white, s_scenes_state.transition_duration_sec);
        
        // Start fade to target scene
        fade_params_t params = {
            .target = {
                .brightness = scene->brightness,
                .red = scene->red,
                .green = scene->green,
                .blue = scene->blue,
                .white = scene->white
            },
            .duration_ms = (uint32_t)s_scenes_state.transition_duration_sec * 1000
        };
        
        esp_err_t ret = fade_controller_start(&params);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start fade: %s", esp_err_to_name(ret));
        } else {
            // Show progress bar and start updates (FR-043)
            // Only show progress bar if duration > 0
            if (s_scenes_state.transition_duration_sec > 0) {
                s_scenes_state.transition_in_progress = true;
                start_progress_updates();
            }
        }
    } else {
        ESP_LOGW(TAG, "No scene selected");
    }
}

/**
 * @brief Close delete confirmation modal
 */
static void close_delete_modal(void)
{
    if (s_delete_modal) {
        lv_obj_del(s_delete_modal);
        s_delete_modal = NULL;
    }
    s_scenes_state.pending_delete_name[0] = '\0';
}

/**
 * @brief Delete confirm button callback
 */
static void delete_confirm_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Delete confirmed for scene: %s", s_scenes_state.pending_delete_name);
    
    // Delete from SD card
    esp_err_t ret = scene_storage_delete(s_scenes_state.pending_delete_name);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Scene deleted successfully");
        // Refresh the carousel - already in LVGL context, use no-lock version
        scene_storage_reload_ui_no_lock();
    } else {
        ESP_LOGE(TAG, "Failed to delete scene: %s", esp_err_to_name(ret));
    }
    
    close_delete_modal();
}

/**
 * @brief Delete cancel button callback
 */
static void delete_cancel_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Delete cancelled");
    close_delete_modal();
}

/**
 * @brief Show delete confirmation modal
 */
static void show_delete_modal(const char *scene_name)
{
    strncpy(s_scenes_state.pending_delete_name, scene_name, sizeof(s_scenes_state.pending_delete_name) - 1);
    s_scenes_state.pending_delete_name[sizeof(s_scenes_state.pending_delete_name) - 1] = '\0';
    
    // Create modal background (semi-transparent overlay)
    s_delete_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_delete_modal, 800, 480);
    lv_obj_center(s_delete_modal);
    lv_obj_set_style_bg_color(s_delete_modal, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_delete_modal, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_delete_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_delete_modal, 0, LV_PART_MAIN);
    
    // Create dialog box
    lv_obj_t *dialog = lv_obj_create(s_delete_modal);
    lv_obj_set_size(dialog, 450, 250);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dialog, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 20, LV_PART_MAIN);
    
    // Warning icon and title
    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, LV_SYMBOL_WARNING " Delete Scene?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(244, 67, 54), LV_PART_MAIN);  // Material Red
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // Scene name
    lv_obj_t *name_label = lv_label_create(dialog);
    char buf[64];
    snprintf(buf, sizeof(buf), "\"%s\"", scene_name);
    lv_label_set_text(name_label, buf);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 50);
    
    // Warning message
    lv_obj_t *msg_label = lv_label_create(dialog);
    lv_label_set_text(msg_label, "This action cannot be undone.");
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(msg_label, lv_color_make(117, 117, 117), LV_PART_MAIN);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 85);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(dialog);
    lv_obj_set_size(btn_container, 400, 70);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Cancel button
    lv_obj_t *btn_cancel = lv_btn_create(btn_container);
    lv_obj_set_size(btn_cancel, 160, 55);
    lv_obj_add_event_cb(btn_cancel, delete_cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(158, 158, 158), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 8, LV_PART_MAIN);
    
    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(cancel_label);
    
    // Delete button
    lv_obj_t *btn_delete = lv_btn_create(btn_container);
    lv_obj_set_size(btn_delete, 160, 55);
    lv_obj_add_event_cb(btn_delete, delete_confirm_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_delete, lv_color_make(244, 67, 54), LV_PART_MAIN);  // Material Red
    lv_obj_set_style_radius(btn_delete, 8, LV_PART_MAIN);
    
    lv_obj_t *delete_label = lv_label_create(btn_delete);
    lv_label_set_text(delete_label, LV_SYMBOL_TRASH " Delete");
    lv_obj_set_style_text_font(delete_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(delete_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(delete_label);
}

// ============================================================================
// Edit Scene Modal (FR-044 to FR-047)
// ============================================================================

/**
 * @brief Close the edit scene modal
 */
static void close_edit_modal(void)
{
    if (s_edit_state.modal) {
        lv_obj_del(s_edit_state.modal);
        memset(&s_edit_state, 0, sizeof(s_edit_state));
    }
}

/**
 * @brief Update the edit modal color preview
 */
static void update_edit_color_preview(void)
{
    if (s_edit_state.color_preview) {
        lv_color_t color = ui_calculate_preview_color(
            s_edit_state.brightness, s_edit_state.red,
            s_edit_state.green, s_edit_state.blue, s_edit_state.white);
        lv_obj_set_style_bg_color(s_edit_state.color_preview, color, LV_PART_MAIN);
    }
}

/**
 * @brief Update edit slider label text
 */
static void update_edit_slider_label(lv_obj_t *label, const char *name, uint8_t value)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%s: %d", name, value);
    lv_label_set_text(label, buf);
}

/**
 * @brief Edit modal slider event handler
 */
static void edit_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    if (slider == s_edit_state.slider_brightness) {
        s_edit_state.brightness = (uint8_t)value;
        update_edit_slider_label(s_edit_state.label_brightness, "Bright", s_edit_state.brightness);
    } else if (slider == s_edit_state.slider_red) {
        s_edit_state.red = (uint8_t)value;
        update_edit_slider_label(s_edit_state.label_red, "Red", s_edit_state.red);
    } else if (slider == s_edit_state.slider_green) {
        s_edit_state.green = (uint8_t)value;
        update_edit_slider_label(s_edit_state.label_green, "Green", s_edit_state.green);
    } else if (slider == s_edit_state.slider_blue) {
        s_edit_state.blue = (uint8_t)value;
        update_edit_slider_label(s_edit_state.label_blue, "Blue", s_edit_state.blue);
    } else if (slider == s_edit_state.slider_white) {
        s_edit_state.white = (uint8_t)value;
        update_edit_slider_label(s_edit_state.label_white, "White", s_edit_state.white);
    }
    
    update_edit_color_preview();
}

/**
 * @brief Edit modal cancel button callback
 */
static void edit_cancel_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Edit cancelled");
    close_edit_modal();
}

/**
 * @brief Edit modal preview button callback - sends current values to lighting
 */
static void edit_preview_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Preview: B=%d R=%d G=%d B=%d W=%d",
             s_edit_state.brightness, s_edit_state.red, s_edit_state.green,
             s_edit_state.blue, s_edit_state.white);
    
    lighting_state_t state = {
        .brightness = s_edit_state.brightness,
        .red = s_edit_state.red,
        .green = s_edit_state.green,
        .blue = s_edit_state.blue,
        .white = s_edit_state.white
    };
    
    esp_err_t ret = fade_controller_apply_immediate(&state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply preview: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Edit modal save button callback
 */
static void edit_save_btn_cb(lv_event_t *e)
{
    const char *new_name = lv_textarea_get_text(s_edit_state.name_textarea);
    
    if (!new_name || strlen(new_name) == 0) {
        ESP_LOGW(TAG, "Scene name is empty, not saving");
        return;
    }
    
    ESP_LOGI(TAG, "Saving edited scene at index %d: '%s' B=%d R=%d G=%d B=%d W=%d",
             s_edit_state.scene_index, new_name, s_edit_state.brightness,
             s_edit_state.red, s_edit_state.green, s_edit_state.blue, s_edit_state.white);
    
    esp_err_t ret = scene_storage_update(
        s_edit_state.scene_index, new_name,
        s_edit_state.brightness, s_edit_state.red, s_edit_state.green,
        s_edit_state.blue, s_edit_state.white);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Scene updated successfully");
        close_edit_modal();
        // Refresh UI - already in LVGL context, use no-lock version
        scene_storage_reload_ui_no_lock();
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Scene name already exists");
        // Could show error message, but for now just log
    } else {
        ESP_LOGE(TAG, "Failed to update scene: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Edit modal move left button callback
 */
static void edit_move_left_btn_cb(lv_event_t *e)
{
    if (s_edit_state.scene_index <= 0) {
        return;  // Already at leftmost position
    }
    
    size_t new_index = s_edit_state.scene_index - 1;
    ESP_LOGI(TAG, "Moving scene from %d to %d", s_edit_state.scene_index, (int)new_index);
    
    esp_err_t ret = scene_storage_reorder(s_edit_state.scene_index, new_index);
    if (ret == ESP_OK) {
        s_edit_state.scene_index = new_index;
        // Update move button states
        if (s_edit_state.btn_move_left) {
            if (new_index == 0) {
                lv_obj_add_state(s_edit_state.btn_move_left, LV_STATE_DISABLED);
            }
        }
        if (s_edit_state.btn_move_right) {
            lv_obj_clear_state(s_edit_state.btn_move_right, LV_STATE_DISABLED);
        }
        // Update order index label
        update_order_index_label();
        // Refresh UI - already in LVGL context, use no-lock version
        scene_storage_reload_ui_no_lock();
    }
}

/**
 * @brief Edit modal move right button callback
 */
static void edit_move_right_btn_cb(lv_event_t *e)
{
    if (s_edit_state.scene_index >= (int)s_cached_scene_count - 1) {
        return;  // Already at rightmost position
    }
    
    size_t new_index = s_edit_state.scene_index + 1;
    ESP_LOGI(TAG, "Moving scene from %d to %d", s_edit_state.scene_index, (int)new_index);
    
    esp_err_t ret = scene_storage_reorder(s_edit_state.scene_index, new_index);
    if (ret == ESP_OK) {
        s_edit_state.scene_index = new_index;
        // Update move button states
        if (s_edit_state.btn_move_right) {
            if (new_index >= s_cached_scene_count - 1) {
                lv_obj_add_state(s_edit_state.btn_move_right, LV_STATE_DISABLED);
            }
        }
        if (s_edit_state.btn_move_left) {
            lv_obj_clear_state(s_edit_state.btn_move_left, LV_STATE_DISABLED);
        }
        // Update order index label
        update_order_index_label();
        // Refresh UI - already in LVGL context, use no-lock version
        scene_storage_reload_ui_no_lock();
    }
}

/**
 * @brief Edit modal textarea event handler for keyboard
 */
static void edit_textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    
    if (code == LV_EVENT_FOCUSED) {
        if (s_edit_state.keyboard) {
            lv_keyboard_set_textarea(s_edit_state.keyboard, ta);
            lv_obj_clear_flag(s_edit_state.keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (s_edit_state.keyboard) {
            lv_obj_add_flag(s_edit_state.keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_READY) {
        // Enter pressed on keyboard - hide keyboard
        if (s_edit_state.keyboard) {
            lv_obj_add_flag(s_edit_state.keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Create a slider with label for edit modal
 */
static void create_edit_slider(lv_obj_t *parent, const char *name, uint8_t initial_value,
                               lv_obj_t **out_slider, lv_obj_t **out_label, lv_coord_t y_pos)
{
    // Label
    *out_label = lv_label_create(parent);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s: %d", name, initial_value);
    lv_label_set_text(*out_label, buf);
    lv_obj_set_style_text_font(*out_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(*out_label, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_align(*out_label, LV_ALIGN_TOP_LEFT, 10, y_pos);
    
    // Slider
    *out_slider = lv_slider_create(parent);
    lv_slider_set_range(*out_slider, 0, 255);
    lv_slider_set_value(*out_slider, initial_value, LV_ANIM_OFF);
    lv_obj_set_size(*out_slider, 340, 15);
    lv_obj_align(*out_slider, LV_ALIGN_TOP_LEFT, 120, y_pos);
    lv_obj_add_event_cb(*out_slider, edit_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Style slider
    lv_obj_set_style_bg_color(*out_slider, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*out_slider, lv_color_make(33, 150, 243), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*out_slider, lv_color_make(33, 150, 243), LV_PART_KNOB);
}

/**
 * @brief Show edit scene modal (FR-044)
 */
static void show_edit_scene_modal(int scene_index)
{
    if (scene_index < 0 || scene_index >= (int)s_cached_scene_count) {
        ESP_LOGE(TAG, "Invalid scene index for edit: %d", scene_index);
        return;
    }
    
    // Load current scene values
    ui_scene_t *scene = &s_cached_scenes[scene_index];
    s_edit_state.scene_index = scene_index;
    s_edit_state.brightness = scene->brightness;
    s_edit_state.red = scene->red;
    s_edit_state.green = scene->green;
    s_edit_state.blue = scene->blue;
    s_edit_state.white = scene->white;
    
    ESP_LOGI(TAG, "Opening edit modal for scene '%s' at index %d", scene->name, scene_index);
    
    // Create modal background (semi-transparent overlay)
    s_edit_state.modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_edit_state.modal, 800, 480);
    lv_obj_center(s_edit_state.modal);
    lv_obj_set_style_bg_color(s_edit_state.modal, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_edit_state.modal, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_edit_state.modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_edit_state.modal, 0, LV_PART_MAIN);
    
    // Create dialog box
    lv_obj_t *dialog = lv_obj_create(s_edit_state.modal);
    lv_obj_set_size(dialog, 750, 435);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(dialog, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dialog, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 15, LV_PART_MAIN);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title
    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, LV_SYMBOL_EDIT " Edit Scene");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Scene order section (top right)
    // "Scene order" label
    lv_obj_t *order_title = lv_label_create(dialog);
    lv_label_set_text(order_title, "Scene order");
    lv_obj_set_style_text_font(order_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(order_title, lv_color_make(97, 97, 97), LV_PART_MAIN);
    lv_obj_align(order_title, LV_ALIGN_TOP_RIGHT, -58, 10);
    
    // Move left button
    s_edit_state.btn_move_left = lv_btn_create(dialog);
    lv_obj_set_size(s_edit_state.btn_move_left, 50, 40);
    lv_obj_align(s_edit_state.btn_move_left, LV_ALIGN_TOP_RIGHT, -150, 30);
    lv_obj_add_event_cb(s_edit_state.btn_move_left, edit_move_left_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(s_edit_state.btn_move_left, lv_color_make(33, 150, 243), LV_PART_MAIN);
    lv_obj_set_style_radius(s_edit_state.btn_move_left, 6, LV_PART_MAIN);
    if (scene_index == 0) {
        lv_obj_add_state(s_edit_state.btn_move_left, LV_STATE_DISABLED);
    }
    
    lv_obj_t *left_label = lv_label_create(s_edit_state.btn_move_left);
    lv_label_set_text(left_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(left_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(left_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(left_label);
    
    // Order index label (between buttons)
    s_edit_state.label_order_index = lv_label_create(dialog);
    lv_obj_set_style_text_font(s_edit_state.label_order_index, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_edit_state.label_order_index, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_align(s_edit_state.label_order_index, LV_ALIGN_TOP_RIGHT, -80, 38);
    update_order_index_label();  // Set initial value
    
    // Move right button
    s_edit_state.btn_move_right = lv_btn_create(dialog);
    lv_obj_set_size(s_edit_state.btn_move_right, 50, 40);
    lv_obj_align(s_edit_state.btn_move_right, LV_ALIGN_TOP_RIGHT, 0, 30);
    lv_obj_add_event_cb(s_edit_state.btn_move_right, edit_move_right_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(s_edit_state.btn_move_right, lv_color_make(33, 150, 243), LV_PART_MAIN);
    lv_obj_set_style_radius(s_edit_state.btn_move_right, 6, LV_PART_MAIN);
    if (scene_index >= (int)s_cached_scene_count - 1) {
        lv_obj_add_state(s_edit_state.btn_move_right, LV_STATE_DISABLED);
    }
    
    lv_obj_t *right_label = lv_label_create(s_edit_state.btn_move_right);
    lv_label_set_text(right_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(right_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(right_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(right_label);
    
    // Name input row
    lv_obj_t *name_label = lv_label_create(dialog);
    lv_label_set_text(name_label, "Name:");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_make(97, 97, 97), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 10, 55);
    
    s_edit_state.name_textarea = lv_textarea_create(dialog);
    lv_textarea_set_one_line(s_edit_state.name_textarea, true);
    lv_textarea_set_text(s_edit_state.name_textarea, scene->name);
    lv_obj_set_size(s_edit_state.name_textarea, 280, 40);
    lv_obj_align(s_edit_state.name_textarea, LV_ALIGN_TOP_LEFT, 80, 45);
    lv_obj_set_style_text_font(s_edit_state.name_textarea, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_edit_state.name_textarea, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_edit_state.name_textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_edit_state.name_textarea, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_edit_state.name_textarea, edit_textarea_event_cb, LV_EVENT_ALL, NULL);
    
    // Color preview circle (right side)
    s_edit_state.color_preview = lv_obj_create(dialog);
    lv_obj_set_size(s_edit_state.color_preview, 150, 150);
    lv_obj_align(s_edit_state.color_preview, LV_ALIGN_TOP_RIGHT, -30, 100);
    lv_obj_set_style_radius(s_edit_state.color_preview, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(s_edit_state.color_preview, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    update_edit_color_preview();
    
    // Preview button (below color preview circle)
    lv_obj_t *btn_preview = lv_btn_create(dialog);
    lv_obj_set_size(btn_preview, 150, 45);
    lv_obj_align(btn_preview, LV_ALIGN_TOP_RIGHT, -30, 260);
    lv_obj_add_event_cb(btn_preview, edit_preview_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_preview, lv_color_make(255, 152, 0), LV_PART_MAIN);  // Material Orange
    lv_obj_set_style_radius(btn_preview, 8, LV_PART_MAIN);
    
    lv_obj_t *preview_label = lv_label_create(btn_preview);
    lv_label_set_text(preview_label, LV_SYMBOL_PLAY " Preview");
    lv_obj_set_style_text_font(preview_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(preview_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(preview_label);
    
    // Sliders container (left side)
    lv_obj_t *sliders_container = lv_obj_create(dialog);
    lv_obj_set_size(sliders_container, 480, 350);
    lv_obj_align(sliders_container, LV_ALIGN_TOP_LEFT, 0, 100);
    lv_obj_set_style_bg_opa(sliders_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sliders_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sliders_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sliders_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create sliders
    create_edit_slider(sliders_container, "Bright", s_edit_state.brightness,
                       &s_edit_state.slider_brightness, &s_edit_state.label_brightness, 5);
    create_edit_slider(sliders_container, "Red", s_edit_state.red,
                       &s_edit_state.slider_red, &s_edit_state.label_red, 55);
    create_edit_slider(sliders_container, "Green", s_edit_state.green,
                       &s_edit_state.slider_green, &s_edit_state.label_green, 105);
    create_edit_slider(sliders_container, "Blue", s_edit_state.blue,
                       &s_edit_state.slider_blue, &s_edit_state.label_blue, 155);
    create_edit_slider(sliders_container, "White", s_edit_state.white,
                       &s_edit_state.slider_white, &s_edit_state.label_white, 205);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(dialog);
    lv_obj_set_size(btn_container, 650, 60);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Cancel button
    lv_obj_t *btn_cancel = lv_btn_create(btn_container);
    lv_obj_set_size(btn_cancel, 200, 50);
    lv_obj_add_event_cb(btn_cancel, edit_cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(158, 158, 158), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 8, LV_PART_MAIN);
    
    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(cancel_label);
    
    // Save button
    lv_obj_t *btn_save = lv_btn_create(btn_container);
    lv_obj_set_size(btn_save, 200, 50);
    lv_obj_add_event_cb(btn_save, edit_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_save, lv_color_make(76, 175, 80), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_save, 8, LV_PART_MAIN);
    
    lv_obj_t *save_label = lv_label_create(btn_save);
    lv_label_set_text(save_label, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(save_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(save_label);
    
    // Create keyboard at bottom of modal (hidden initially)
    s_edit_state.keyboard = lv_keyboard_create(s_edit_state.modal);
    lv_obj_set_size(s_edit_state.keyboard, 800, 200);
    lv_obj_align(s_edit_state.keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_edit_state.keyboard, s_edit_state.name_textarea);
    lv_obj_add_flag(s_edit_state.keyboard, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Edit button click handler on card
 */
static void card_edit_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int scene_index = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (scene_index >= 0 && scene_index < (int)s_cached_scene_count) {
        ESP_LOGI(TAG, "Edit button pressed for scene index %d", scene_index);
        show_edit_scene_modal(scene_index);
    }
}

/**
 * @brief Delete button click handler on card
 */
static void card_delete_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int scene_index = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (scene_index >= 0 && scene_index < (int)s_cached_scene_count) {
        const char *scene_name = s_cached_scenes[scene_index].name;
        ESP_LOGI(TAG, "Delete button pressed for scene: %s (index %d)", scene_name, scene_index);
        show_delete_modal(scene_name);
    }
}

/**
 * @brief Card tap handler - selects the scene
 */
static void card_click_cb(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(card);
    
    s_scenes_state.current_scene_index = index;
    ESP_LOGI(TAG, "Scene card selected: %d", index);
    
    // Update visual selection
    update_card_selection(index);
    
    // Scroll to center this card
    if (s_carousel) {
        lv_coord_t scroll_x = index * (CARD_WIDTH + CARD_GAP);
        lv_obj_scroll_to_x(s_carousel, scroll_x, LV_ANIM_ON);
    }
}

/**
 * @brief Carousel scroll end handler - update selected scene based on centered card
 */
static void carousel_scroll_end_cb(lv_event_t *e)
{
    if (!s_carousel || s_cached_scene_count == 0) return;
    
    lv_coord_t scroll_x = lv_obj_get_scroll_x(s_carousel);
    int card_index = (scroll_x + CARD_WIDTH / 2) / (CARD_WIDTH + CARD_GAP);
    
    if (card_index < 0) card_index = 0;
    if (card_index >= (int)s_cached_scene_count) card_index = s_cached_scene_count - 1;
    
    if (card_index != s_scenes_state.current_scene_index) {
        s_scenes_state.current_scene_index = card_index;
        ESP_LOGI(TAG, "Carousel scroll ended, selected scene: %d", card_index);
    }
    
    // Always update visual selection after scroll ends
    update_card_selection(card_index);
}

/**
 * @brief Create a scene card
 */
static lv_obj_t* create_scene_card(lv_obj_t *parent, const ui_scene_t *scene, int index)
{
    // Card container (no shadows for smooth scroll performance)
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_WIDTH, CARD_HEIGHT);
    lv_obj_set_style_bg_color(card, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_make(224, 224, 224), LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 15, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    // Store scene index for selection
    lv_obj_set_user_data(card, (void*)(intptr_t)index);
    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, NULL);
    
    // Edit button (top-left corner)
    lv_obj_t *btn_edit = lv_btn_create(card);
    lv_obj_set_size(btn_edit, 36, 36);
    lv_obj_align(btn_edit, LV_ALIGN_TOP_LEFT, -5, -5);
    lv_obj_set_style_bg_color(btn_edit, lv_color_make(33, 150, 243), LV_PART_MAIN);  // Material Blue
    lv_obj_set_style_radius(btn_edit, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    
    // Store scene index in edit button for callback
    lv_obj_set_user_data(btn_edit, (void*)(intptr_t)index);
    lv_obj_add_event_cb(btn_edit, card_edit_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *edit_icon = lv_label_create(btn_edit);
    lv_label_set_text(edit_icon, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_font(edit_icon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(edit_icon, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(edit_icon);
    
    // Delete button (top-right corner)
    lv_obj_t *btn_delete = lv_btn_create(card);
    lv_obj_set_size(btn_delete, 36, 36);
    lv_obj_align(btn_delete, LV_ALIGN_TOP_RIGHT, 5, -5);
    lv_obj_set_style_bg_color(btn_delete, lv_color_make(244, 67, 54), LV_PART_MAIN);  // Material Red
    lv_obj_set_style_radius(btn_delete, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    
    // Store scene index in delete button for callback (same as card)
    lv_obj_set_user_data(btn_delete, (void*)(intptr_t)index);
    lv_obj_add_event_cb(btn_delete, card_delete_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *trash_icon = lv_label_create(btn_delete);
    lv_label_set_text(trash_icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(trash_icon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(trash_icon, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(trash_icon);
    
    // Color preview circle (shows approximate light color)
    lv_obj_t *color_circle = lv_obj_create(card);
    lv_obj_set_size(color_circle, 80, 80);
    lv_obj_align(color_circle, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_radius(color_circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_color_t preview_color = ui_calculate_preview_color(
        scene->brightness, scene->red, scene->green, scene->blue, scene->white);
    lv_obj_set_style_bg_color(color_circle, preview_color, LV_PART_MAIN);
    lv_obj_clear_flag(color_circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    
    // Scene name (below color circle)
    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, scene->name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(name_label, CARD_WIDTH - 50);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 140);
    
    // RGBW values (smaller font)
    char values_buf[80];
    snprintf(values_buf, sizeof(values_buf), "Brightness:%d\nR:%d G:%d B:%d W:%d",
             scene->brightness, scene->red, scene->green, scene->blue, scene->white);
    
    lv_obj_t *values_label = lv_label_create(card);
    lv_label_set_text(values_label, values_buf);
    lv_obj_set_style_text_font(values_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(values_label, lv_color_make(117, 117, 117), LV_PART_MAIN);
    lv_obj_set_style_text_align(values_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(values_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    return card;
}

/**
 * @brief Create the scene selector tab content (FR-040)
 */
void ui_create_scenes_tab(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating scene selector tab");

    // Calculate padding to center cards: (carousel_width - card_width) / 2
    lv_coord_t center_pad = (760 - CARD_WIDTH) / 2;

    // Create horizontal scrolling carousel container (FR-040)
    s_carousel = lv_obj_create(parent);
    lv_obj_set_size(s_carousel, 760, CAROUSEL_HEIGHT);
    lv_obj_align(s_carousel, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_opa(s_carousel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_carousel, 0, LV_PART_MAIN);
    // Use left/right padding to center first/last cards and constrain scroll
    lv_obj_set_style_pad_left(s_carousel, center_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_carousel, center_pad, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_carousel, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_carousel, 10, LV_PART_MAIN);
    
    // Enable horizontal scrolling with snap
    lv_obj_set_scroll_dir(s_carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(s_carousel, LV_SCROLLBAR_MODE_OFF);
    
    // Flex layout for cards
    lv_obj_set_flex_flow(s_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_carousel, CARD_GAP, LV_PART_MAIN);
    
    // Add scroll end event to update selected scene
    lv_obj_add_event_cb(s_carousel, carousel_scroll_end_cb, LV_EVENT_SCROLL_END, NULL);

    // Placeholder "No scenes" label (will be replaced when scenes are loaded)
    s_label_no_scenes = lv_label_create(s_carousel);
    lv_label_set_text(s_label_no_scenes, "No scenes\n\nSave a scene from Manual Control");
    lv_obj_set_style_text_font(s_label_no_scenes, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_no_scenes, lv_color_make(158, 158, 158), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_label_no_scenes, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // Create transition duration slider (FR-041)
    // Position below carousel with proper spacing
    s_label_duration = lv_label_create(parent);
    update_duration_label(s_scenes_state.transition_duration_sec);
    lv_obj_set_style_text_font(s_label_duration, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_duration, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_align(s_label_duration, LV_ALIGN_BOTTOM_LEFT, 20, -70);
    
    s_slider_duration = lv_slider_create(parent);
    lv_slider_set_range(s_slider_duration, 0, 300);  // 0 to 300 seconds (FR-041)
    lv_slider_set_value(s_slider_duration, s_scenes_state.transition_duration_sec, LV_ANIM_OFF);
    lv_obj_set_size(s_slider_duration, 350, 20);
    lv_obj_align(s_slider_duration, LV_ALIGN_BOTTOM_LEFT, 20, -25);
    lv_obj_add_event_cb(s_slider_duration, duration_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Style the duration slider - Material Blue
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(33, 150, 243), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(33, 150, 243), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_slider_duration, 0, LV_PART_MAIN);

    // Create progress bar (FR-043) - positioned between carousel and apply button
    s_progress_bar = lv_bar_create(parent);
    lv_obj_set_size(s_progress_bar, 350, 15);
    lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_RIGHT, -20, -85);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    
    // Style the progress bar - Material Green
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(76, 175, 80), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, 8, LV_PART_INDICATOR);
    
    // Initially hide progress bar
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    // Create Apply button (FR-042) - at bottom right
    s_btn_apply = lv_btn_create(parent);
    lv_obj_set_size(s_btn_apply, 350, 70);
    lv_obj_align(s_btn_apply, LV_ALIGN_BOTTOM_RIGHT, -20, -5);
    lv_obj_add_event_cb(s_btn_apply, apply_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label_apply = lv_label_create(s_btn_apply);
    lv_label_set_text(label_apply, LV_SYMBOL_PLAY " Apply Scene");
    lv_obj_set_style_text_font(label_apply, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_center(label_apply);
    
    // Style Apply button - Material Green
    lv_obj_set_style_bg_color(s_btn_apply, lv_color_make(76, 175, 80), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_btn_apply, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label_apply, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_btn_apply, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_btn_apply, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_apply, 8, LV_PART_MAIN);

    // Create persistent timer for progress bar updates (runs every 100ms)
    // This timer handles both internal and external fade tracking
    s_progress_timer = lv_timer_create(progress_timer_cb, 100, NULL);

    ESP_LOGI(TAG, "Scene selector tab created");
}

/**
 * @brief Load scenes from SD card and populate the carousel (FR-040)
 * 
 * @param scenes Array of scene structs
 * @param count Number of scenes
 */
void ui_scenes_load_from_sd(const ui_scene_t *scenes, size_t count)
{
    if (!s_carousel) {
        ESP_LOGE(TAG, "Carousel not initialized");
        return;
    }

    // Clear card array
    memset(s_scene_cards, 0, sizeof(s_scene_cards));

    // Cache scenes for later access
    s_cached_scene_count = (count > SCENE_STORAGE_MAX_SCENES) ? SCENE_STORAGE_MAX_SCENES : count;
    if (scenes && count > 0) {
        memcpy(s_cached_scenes, scenes, s_cached_scene_count * sizeof(ui_scene_t));
    }

    // Clear existing carousel content
    lv_obj_clean(s_carousel);

    if (count == 0) {
        // Show "no scenes" message
        s_label_no_scenes = lv_label_create(s_carousel);
        lv_label_set_text(s_label_no_scenes, "No scenes\n\nSave a scene from Manual Control");
        lv_obj_set_style_text_font(s_label_no_scenes, &lv_font_montserrat_28, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_label_no_scenes, lv_color_make(158, 158, 158), LV_PART_MAIN);
        lv_obj_set_style_text_align(s_label_no_scenes, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        s_scenes_state.current_scene_index = 0;
    } else {
        // Create cards for each scene and store in array
        // (Carousel uses left/right padding to center first/last cards)
        for (size_t i = 0; i < count; i++) {
            s_scene_cards[i] = create_scene_card(s_carousel, &scenes[i], i);
        }
        
        // Reset to first scene and update selection visual
        s_scenes_state.current_scene_index = 0;
        update_card_selection(0);
        
        ESP_LOGI(TAG, "Loaded %d scene cards", count);
    }
}

/**
 * @brief Update transition progress bar (FR-043)
 * 
 * @param percent Progress percentage (0-100)
 */
void ui_scenes_update_progress(uint8_t percent)
{
    if (!s_progress_bar) {
        return;
    }

    if (percent > 0 && percent < 100) {
        // Show progress bar during transition
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
        s_scenes_state.transition_in_progress = true;
    } else {
        // Hide progress bar when complete or not started
        lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
        s_scenes_state.transition_in_progress = false;
    }
}

/**
 * @brief Get current selected scene index
 */
int ui_scenes_get_selected_index(void)
{
    return s_scenes_state.current_scene_index;
}

/**
 * @brief Get current transition duration
 */
uint16_t ui_scenes_get_duration_sec(void)
{
    return s_scenes_state.transition_duration_sec;
}

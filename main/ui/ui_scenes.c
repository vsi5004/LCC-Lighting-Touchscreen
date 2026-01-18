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
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_scenes";

// Card dimensions
#define CARD_WIDTH      280
#define CARD_HEIGHT     280
#define CARD_GAP        20
#define CAROUSEL_HEIGHT 300

// Scene selector state
static struct {
    int current_scene_index;
    uint16_t transition_duration_sec;
    bool transition_in_progress;
    char pending_delete_name[32];  // Scene name pending deletion
} s_scenes_state = {
    .current_scene_index = 0,
    .transition_duration_sec = 10,
    .transition_in_progress = false,
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

// Delete confirmation modal
static lv_obj_t *s_delete_modal = NULL;

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
    char buf[32];
    if (seconds < 60) {
        snprintf(buf, sizeof(buf), "Duration: %d sec", seconds);
    } else {
        snprintf(buf, sizeof(buf), "Duration: %d min %d sec", seconds / 60, seconds % 60);
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
    }
    
    // TODO: Perform linear fade to target scene (FR-042)
    // This will call into the lighting_task to start the fade
    // The progress bar should be updated by a callback (FR-043)
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
        // Refresh the carousel
        scene_storage_reload_ui();
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
    
    // Delete button (top-right corner)
    lv_obj_t *btn_delete = lv_btn_create(card);
    lv_obj_set_size(btn_delete, 45, 45);
    lv_obj_align(btn_delete, LV_ALIGN_TOP_RIGHT, 5, -5);
    lv_obj_set_style_bg_color(btn_delete, lv_color_make(244, 67, 54), LV_PART_MAIN);  // Material Red
    lv_obj_set_style_radius(btn_delete, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    
    // Store scene index in delete button for callback (same as card)
    lv_obj_set_user_data(btn_delete, (void*)(intptr_t)index);
    lv_obj_add_event_cb(btn_delete, card_delete_btn_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *trash_icon = lv_label_create(btn_delete);
    lv_label_set_text(trash_icon, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(trash_icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(trash_icon, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(trash_icon);
    
    // Scene name (large, centered)
    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, scene->name);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(name_label, CARD_WIDTH - 60);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 50);
    
    // RGBW values (smaller font)
    char values_buf[80];
    snprintf(values_buf, sizeof(values_buf), "Brightness: %d\nR:%d  G:%d  B:%d  W:%d",
             scene->brightness, scene->red, scene->green, scene->blue, scene->white);
    
    lv_obj_t *values_label = lv_label_create(card);
    lv_label_set_text(values_label, values_buf);
    lv_obj_set_style_text_font(values_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(values_label, lv_color_make(117, 117, 117), LV_PART_MAIN);
    lv_obj_set_style_text_align(values_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(values_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    
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
    lv_obj_align(s_carousel, LV_ALIGN_TOP_MID, 0, 10);
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
    s_label_duration = lv_label_create(parent);
    update_duration_label(s_scenes_state.transition_duration_sec);
    lv_obj_set_style_text_font(s_label_duration, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label_duration, lv_color_hex(0x0000), LV_PART_MAIN);
    lv_obj_align(s_label_duration, LV_ALIGN_BOTTOM_LEFT, 20, -100);
    
    s_slider_duration = lv_slider_create(parent);
    lv_slider_set_range(s_slider_duration, 0, 300);  // 0 to 300 seconds (FR-041)
    lv_slider_set_value(s_slider_duration, s_scenes_state.transition_duration_sec, LV_ANIM_OFF);
    lv_obj_set_size(s_slider_duration, 350, 25);
    lv_obj_align(s_slider_duration, LV_ALIGN_BOTTOM_LEFT, 20, -60);
    lv_obj_add_event_cb(s_slider_duration, duration_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Style the duration slider - Material Blue
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(33, 150, 243), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider_duration, lv_color_make(33, 150, 243), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_slider_duration, 0, LV_PART_MAIN);

    // Create progress bar (FR-043)
    s_progress_bar = lv_bar_create(parent);
    lv_obj_set_size(s_progress_bar, 350, 25);
    lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_RIGHT, -20, -100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    
    // Style the progress bar - Material Green
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(76, 175, 80), LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_progress_bar, 0, LV_PART_MAIN);
    
    // Initially hide progress bar
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    // Create Apply button (FR-042)
    s_btn_apply = lv_btn_create(parent);
    lv_obj_set_size(s_btn_apply, 350, 55);
    lv_obj_align(s_btn_apply, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
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

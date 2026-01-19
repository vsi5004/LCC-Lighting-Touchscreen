/**
 * @file ui_manual.c
 * @brief Manual Control Tab UI
 * 
 * Implements FR-020 to FR-023:
 * - FR-020: Provide sliders for Brightness, R, G, B, W
 * - FR-021: No CAN traffic until Update is pressed
 * - FR-022: Update transmits all parameters respecting rate limits
 * - FR-023: Save Scene opens modal dialog with Save and Cancel
 */

#include "ui_common.h"
#include "../app/scene_storage.h"
#include "../app/fade_controller.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_manual";

// Manual control state
static struct {
    uint8_t brightness;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
} s_manual_state = {
    .brightness = 255,
    .red = 255,
    .green = 255,
    .blue = 255,
    .white = 255
};

// UI Objects
static lv_obj_t *s_slider_brightness = NULL;
static lv_obj_t *s_slider_red = NULL;
static lv_obj_t *s_slider_green = NULL;
static lv_obj_t *s_slider_blue = NULL;
static lv_obj_t *s_slider_white = NULL;

static lv_obj_t *s_label_brightness = NULL;
static lv_obj_t *s_label_red = NULL;
static lv_obj_t *s_label_green = NULL;
static lv_obj_t *s_label_blue = NULL;
static lv_obj_t *s_label_white = NULL;

static lv_obj_t *s_btn_update = NULL;
static lv_obj_t *s_btn_save_scene = NULL;
static lv_obj_t *s_color_preview = NULL;

// Save Scene modal objects
static lv_obj_t *s_save_modal = NULL;
static lv_obj_t *s_save_textarea = NULL;
static lv_obj_t *s_save_keyboard = NULL;

/**
 * @brief Close the save scene modal
 */
static void close_save_modal(void)
{
    if (s_save_modal) {
        lv_obj_del(s_save_modal);
        s_save_modal = NULL;
        s_save_textarea = NULL;
        s_save_keyboard = NULL;
    }
}

/**
 * @brief Save button callback in modal
 */
static void modal_save_btn_cb(lv_event_t *e)
{
    const char *scene_name = lv_textarea_get_text(s_save_textarea);
    
    if (scene_name && strlen(scene_name) > 0) {
        ESP_LOGI(TAG, "Saving scene: '%s' with B:%d R:%d G:%d B:%d W:%d",
                 scene_name, s_manual_state.brightness, s_manual_state.red,
                 s_manual_state.green, s_manual_state.blue, s_manual_state.white);
        
        // Save to SD card
        esp_err_t ret = scene_storage_save(scene_name, s_manual_state.brightness,
                                           s_manual_state.red, s_manual_state.green,
                                           s_manual_state.blue, s_manual_state.white);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Scene saved successfully");
            // Refresh the Scene Selector UI - already in LVGL context, use no-lock version
            scene_storage_reload_ui_no_lock();
        } else {
            ESP_LOGE(TAG, "Failed to save scene: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "Scene name is empty, not saving");
    }
    
    close_save_modal();
}

/**
 * @brief Cancel button callback in modal
 */
static void modal_cancel_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Save scene cancelled");
    close_save_modal();
}

/**
 * @brief Textarea event handler for keyboard
 */
static void textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    
    if (code == LV_EVENT_FOCUSED) {
        if (s_save_keyboard) {
            lv_keyboard_set_textarea(s_save_keyboard, ta);
            lv_obj_clear_flag(s_save_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        if (s_save_keyboard) {
            lv_obj_add_flag(s_save_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_READY) {
        // Enter pressed on keyboard - trigger save
        modal_save_btn_cb(e);
    }
}

/**
 * @brief Create and show the Save Scene modal dialog (FR-023)
 */
static void show_save_scene_modal(void)
{
    // Create modal background (semi-transparent overlay)
    s_save_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_save_modal, 800, 480);
    lv_obj_center(s_save_modal);
    lv_obj_set_style_bg_color(s_save_modal, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_save_modal, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_save_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_save_modal, 0, LV_PART_MAIN);
    
    // Create dialog box
    lv_obj_t *dialog = lv_obj_create(s_save_modal);
    lv_obj_set_size(dialog, 500, 320);
    lv_obj_align(dialog, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(dialog, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_radius(dialog, 12, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(dialog, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(dialog, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dialog, 20, LV_PART_MAIN);
    
    // Title
    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "Save Scene");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(33, 33, 33), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
    
    // Scene name label
    lv_obj_t *name_label = lv_label_create(dialog);
    lv_label_set_text(name_label, "Scene Name:");
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_make(97, 97, 97), LV_PART_MAIN);
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 50);
    
    // Text input for scene name
    s_save_textarea = lv_textarea_create(dialog);
    lv_textarea_set_one_line(s_save_textarea, true);
    lv_textarea_set_placeholder_text(s_save_textarea, "Enter scene name...");
    lv_obj_set_size(s_save_textarea, 440, 50);
    lv_obj_align(s_save_textarea, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_obj_set_style_text_font(s_save_textarea, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_save_textarea, lv_color_make(189, 189, 189), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_save_textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_save_textarea, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(s_save_textarea, textarea_event_cb, LV_EVENT_ALL, NULL);
    
    // Current values display
    char values_buf[64];
    snprintf(values_buf, sizeof(values_buf), "B:%d  R:%d  G:%d  B:%d  W:%d",
             s_manual_state.brightness, s_manual_state.red, s_manual_state.green,
             s_manual_state.blue, s_manual_state.white);
    lv_obj_t *values_label = lv_label_create(dialog);
    lv_label_set_text(values_label, values_buf);
    lv_obj_set_style_text_font(values_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(values_label, lv_color_make(117, 117, 117), LV_PART_MAIN);
    lv_obj_align(values_label, LV_ALIGN_TOP_LEFT, 0, 140);
    
    // Button container
    lv_obj_t *btn_container = lv_obj_create(dialog);
    lv_obj_set_size(btn_container, 440, 70);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_container, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Cancel button
    lv_obj_t *btn_cancel = lv_btn_create(btn_container);
    lv_obj_set_size(btn_cancel, 180, 55);
    lv_obj_add_event_cb(btn_cancel, modal_cancel_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_make(158, 158, 158), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 8, LV_PART_MAIN);
    
    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE " Cancel");
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(cancel_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(cancel_label);
    
    // Save button
    lv_obj_t *btn_save = lv_btn_create(btn_container);
    lv_obj_set_size(btn_save, 180, 55);
    lv_obj_add_event_cb(btn_save, modal_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(btn_save, lv_color_make(76, 175, 80), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_save, 8, LV_PART_MAIN);
    
    lv_obj_t *save_label = lv_label_create(btn_save);
    lv_label_set_text(save_label, LV_SYMBOL_OK " Save");
    lv_obj_set_style_text_font(save_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(save_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(save_label);
    
    // Create keyboard at bottom of modal (full width, taller)
    s_save_keyboard = lv_keyboard_create(s_save_modal);
    lv_obj_set_size(s_save_keyboard, 800, 240);
    lv_obj_align(s_save_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_save_keyboard, s_save_textarea);
    lv_obj_add_flag(s_save_keyboard, LV_OBJ_FLAG_HIDDEN);  // Hidden until textarea focused
    
    // Focus the textarea to show keyboard
    lv_obj_add_state(s_save_textarea, LV_STATE_FOCUSED);
}

/**
 * @brief Calculate display RGB from RGBW + brightness (additive light mixing)
 * 
 * For RGBW LEDs:
 * - RGB channels define the hue/color
 * - White LED blends towards white, but doesn't completely wash out color
 * - Brightness acts as intensity using gamma curve for perceptual accuracy
 * 
 * @param brightness Master brightness (0-255)
 * @param r Red channel (0-255)
 * @param g Green channel (0-255)
 * @param b Blue channel (0-255)
 * @param w White channel (0-255)
 * @return lv_color_t Display color for preview
 */
lv_color_t ui_calculate_preview_color(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    // White LED blends color towards white, but preserves some hue
    // This keeps color visible even at high white values
    // blend_factor = w / 320 means max 80% blend towards white at w=255
    // For each channel: result = color + (255 - color) * blend_factor
    uint16_t full_r = r + ((255 - r) * w) / 320;
    uint16_t full_g = g + ((255 - g) * w) / 320;
    uint16_t full_b = b + ((255 - b) * w) / 320;
    
    // Clamp to 255 (shouldn't be needed with /512 but for safety)
    if (full_r > 255) full_r = 255;
    if (full_g > 255) full_g = 255;
    if (full_b > 255) full_b = 255;
    
    // Apply brightness as intensity using square root for perceptual linearity
    // This keeps colors visible at lower brightness (like alpha/opacity)
    // brightness=0 -> 0%, brightness=64 -> 50%, brightness=255 -> 100%
    uint32_t b_normalized = (brightness * 255);  // 0-65025
    uint32_t intensity = 0;
    if (b_normalized > 0) {
        // Integer square root using Newton-Raphson
        uint32_t x = b_normalized;
        uint32_t y = x;
        while (y > (x / y)) {
            y = (y + x / y) / 2;
        }
        intensity = y;
    }
    // intensity is now 0-255 with gamma 0.5 curve applied
    
    uint16_t final_r = (full_r * intensity) / 255;
    uint16_t final_g = (full_g * intensity) / 255;
    uint16_t final_b = (full_b * intensity) / 255;
    
    return lv_color_make((uint8_t)final_r, (uint8_t)final_g, (uint8_t)final_b);
}

/**
 * @brief Update the color preview circle
 */
static void update_color_preview(void)
{
    if (s_color_preview) {
        lv_color_t color = ui_calculate_preview_color(
            s_manual_state.brightness, s_manual_state.red,
            s_manual_state.green, s_manual_state.blue, s_manual_state.white);
        lv_obj_set_style_bg_color(s_color_preview, color, LV_PART_MAIN);
    }
}

/**
 * @brief Update label text with current value
 */
static void update_slider_label(lv_obj_t *label, const char *name, uint8_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s: %d", name, value);
    lv_label_set_text(label, buf);
}

/**
 * @brief Slider value changed event handler
 */
static void slider_event_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    // Update the corresponding state value and label
    if (slider == s_slider_brightness) {
        s_manual_state.brightness = (uint8_t)value;
        update_slider_label(s_label_brightness, "Brightness", s_manual_state.brightness);
    } else if (slider == s_slider_red) {
        s_manual_state.red = (uint8_t)value;
        update_slider_label(s_label_red, "Red", s_manual_state.red);
    } else if (slider == s_slider_green) {
        s_manual_state.green = (uint8_t)value;
        update_slider_label(s_label_green, "Green", s_manual_state.green);
    } else if (slider == s_slider_blue) {
        s_manual_state.blue = (uint8_t)value;
        update_slider_label(s_label_blue, "Blue", s_manual_state.blue);
    } else if (slider == s_slider_white) {
        s_manual_state.white = (uint8_t)value;
        update_slider_label(s_label_white, "White", s_manual_state.white);
    }
    
    // Update color preview circle
    update_color_preview();
}

/**
 * @brief Update button event handler (FR-022)
 */
static void apply_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Apply button pressed");
    ESP_LOGI(TAG, "Values - Brightness: %d, R: %d, G: %d, B: %d, W: %d",
             s_manual_state.brightness, s_manual_state.red, s_manual_state.green,
             s_manual_state.blue, s_manual_state.white);
    
    // Apply immediately (no fade from manual control)
    lighting_state_t state = {
        .brightness = s_manual_state.brightness,
        .red = s_manual_state.red,
        .green = s_manual_state.green,
        .blue = s_manual_state.blue,
        .white = s_manual_state.white
    };
    
    esp_err_t ret = fade_controller_apply_immediate(&state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply lighting: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Save Scene button event handler (FR-023)
 */
static void save_scene_btn_event_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Save Scene button pressed");
    show_save_scene_modal();
}

/**
 * @brief Create a labeled slider
 */
static lv_obj_t* create_labeled_slider(lv_obj_t *parent, const char *label_text, 
                                       uint8_t initial_value, lv_obj_t **out_label,
                                       lv_coord_t y_pos)
{
    // Create label
    lv_obj_t *label = lv_label_create(parent);
    char buf[32];
    snprintf(buf, sizeof(buf), "%s: %d", label_text, initial_value);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x0000), LV_PART_MAIN);  // Black text
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, y_pos);
    
    // Create slider (with increased spacing from label)
    lv_obj_t *slider = lv_slider_create(parent);
    lv_slider_set_range(slider, 0, 255);
    lv_slider_set_value(slider, initial_value, LV_ANIM_OFF);
    lv_obj_set_size(slider, 420, 20);  // Taller slider for easier touch
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 20, y_pos + 40);  // Increased from 30 to 40
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    
    // Style the slider - Material Blue with darker grey background
    lv_obj_set_style_bg_color(slider, lv_color_make(189, 189, 189), LV_PART_MAIN);  // Darker gray #BDBDBD
    lv_obj_set_style_bg_color(slider, lv_color_make(33, 150, 243), LV_PART_INDICATOR);  // Material Blue #2196F3
    lv_obj_set_style_bg_color(slider, lv_color_make(33, 150, 243), LV_PART_KNOB);  // Material Blue #2196F3
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);  // Larger knob
    
    if (out_label) {
        *out_label = label;
    }
    
    return slider;
}

/**
 * @brief Create the manual control tab content (FR-020)
 */
void ui_create_manual_tab(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Creating manual control tab");

    // Create sliders (FR-020) - positioned on left 2/3 of screen
    s_slider_brightness = create_labeled_slider(parent, "Brightness", s_manual_state.brightness, 
                                                 &s_label_brightness, 5);
    s_slider_red = create_labeled_slider(parent, "Red", s_manual_state.red, 
                                          &s_label_red, 80);
    s_slider_green = create_labeled_slider(parent, "Green", s_manual_state.green, 
                                            &s_label_green, 155);
    s_slider_blue = create_labeled_slider(parent, "Blue", s_manual_state.blue, 
                                           &s_label_blue, 230);
    s_slider_white = create_labeled_slider(parent, "White", s_manual_state.white, 
                                            &s_label_white, 305);

    // Create color preview circle on right side
    s_color_preview = lv_obj_create(parent);
    lv_obj_set_size(s_color_preview, 140, 140);
    lv_obj_align(s_color_preview, LV_ALIGN_TOP_RIGHT, -60, 20);
    lv_obj_set_style_radius(s_color_preview, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(s_color_preview, LV_OBJ_FLAG_SCROLLABLE);
    
    // Set initial preview color
    update_color_preview();

    // Create buttons on right third, below color preview
    // Apply button (FR-021, FR-022)
    s_btn_update = lv_btn_create(parent);
    lv_obj_set_size(s_btn_update, 220, 60);
    lv_obj_align(s_btn_update, LV_ALIGN_TOP_RIGHT, -20, 200);
    lv_obj_add_event_cb(s_btn_update, apply_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label_apply = lv_label_create(s_btn_update);
    lv_label_set_text(label_apply, LV_SYMBOL_PLAY " Apply");
    lv_obj_set_style_text_font(label_apply, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_center(label_apply);
    
    // Style Apply button - Material Green
    lv_obj_set_style_bg_color(s_btn_update, lv_color_make(76, 175, 80), LV_PART_MAIN);  // Material Green #4CAF50
    lv_obj_set_style_bg_opa(s_btn_update, LV_OPA_COVER, LV_PART_MAIN);  // Ensure full opacity
    lv_obj_set_style_text_color(label_apply, lv_color_make(255, 255, 255), LV_PART_MAIN);  // White
    lv_obj_set_style_shadow_width(s_btn_update, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_btn_update, LV_OPA_30, LV_PART_MAIN);

    // Save Scene button (FR-023) - below Apply button
    s_btn_save_scene = lv_btn_create(parent);
    lv_obj_set_size(s_btn_save_scene, 220, 60);
    lv_obj_align(s_btn_save_scene, LV_ALIGN_TOP_RIGHT, -20, 280);
    lv_obj_add_event_cb(s_btn_save_scene, save_scene_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *label_save = lv_label_create(s_btn_save_scene);
    lv_label_set_text(label_save, LV_SYMBOL_SAVE " Save Scene");
    lv_obj_set_style_text_font(label_save, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_center(label_save);
    
    // Style Save Scene button - Material Blue
    lv_obj_set_style_bg_color(s_btn_save_scene, lv_color_make(33, 150, 243), LV_PART_MAIN);  // Material Blue #2196F3
    lv_obj_set_style_bg_opa(s_btn_save_scene, LV_OPA_COVER, LV_PART_MAIN);  // Ensure full opacity
    lv_obj_set_style_text_color(label_save, lv_color_make(255, 255, 255), LV_PART_MAIN);  // White
    lv_obj_set_style_shadow_width(s_btn_save_scene, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_btn_save_scene, LV_OPA_30, LV_PART_MAIN);

    ESP_LOGI(TAG, "Manual control tab created");
}

/**
 * @brief Get current manual control values
 */
void ui_manual_get_values(uint8_t *brightness, uint8_t *red, uint8_t *green, 
                          uint8_t *blue, uint8_t *white)
{
    if (brightness) *brightness = s_manual_state.brightness;
    if (red) *red = s_manual_state.red;
    if (green) *green = s_manual_state.green;
    if (blue) *blue = s_manual_state.blue;
    if (white) *white = s_manual_state.white;
}

/**
 * @brief Set manual control values (updates sliders)
 */
void ui_manual_set_values(uint8_t brightness, uint8_t red, uint8_t green, 
                          uint8_t blue, uint8_t white)
{
    ui_lock();

    s_manual_state.brightness = brightness;
    s_manual_state.red = red;
    s_manual_state.green = green;
    s_manual_state.blue = blue;
    s_manual_state.white = white;

    if (s_slider_brightness) {
        lv_slider_set_value(s_slider_brightness, brightness, LV_ANIM_OFF);
        update_slider_label(s_label_brightness, "Brightness", brightness);
    }
    if (s_slider_red) {
        lv_slider_set_value(s_slider_red, red, LV_ANIM_OFF);
        update_slider_label(s_label_red, "Red", red);
    }
    if (s_slider_green) {
        lv_slider_set_value(s_slider_green, green, LV_ANIM_OFF);
        update_slider_label(s_label_green, "Green", green);
    }
    if (s_slider_blue) {
        lv_slider_set_value(s_slider_blue, blue, LV_ANIM_OFF);
        update_slider_label(s_label_blue, "Blue", blue);
    }
    if (s_slider_white) {
        lv_slider_set_value(s_slider_white, white, LV_ANIM_OFF);
        update_slider_label(s_label_white, "White", white);
    }
    
    // Update color preview circle
    update_color_preview();

    ui_unlock();
}

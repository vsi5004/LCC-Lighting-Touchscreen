/**
 * @file ui_main.c
 * @brief Main UI Screen with Tabview (Manual Control and Scene Selector)
 * 
 * Implements FR-010: Provide two tabs: Manual Control and Scene Selector
 */

#include "ui_common.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_main";

// UI Objects
static lv_obj_t *s_tabview = NULL;
static lv_obj_t *s_tab_manual = NULL;
static lv_obj_t *s_tab_scenes = NULL;

/**
 * @brief Create the main screen with tabview
 */
void ui_create_main_screen(void)
{
    ESP_LOGI(TAG, "Creating main screen with tabview");

    ui_lock();

    // Get the active screen
    lv_obj_t *scr = lv_scr_act();

    // Clear the screen
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    // Create tabview
    s_tabview = lv_tabview_create(scr, LV_DIR_TOP, 60);
    
    // Set tabview style - RGB565-safe colors
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_tabview, lv_color_hex(0x000000), LV_PART_MAIN);
    
    // Style tab buttons (top bar)
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(s_tabview);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_28, LV_PART_MAIN);
    
    // Unselected tabs: light grey background with dimmer white text
    lv_obj_set_style_bg_color(tab_btns, lv_color_make(158, 158, 158), LV_PART_MAIN);  // Grey #9E9E9E
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_btns, lv_color_make(220, 220, 220), LV_PART_MAIN);  // Dimmer white
    lv_obj_set_style_text_color(tab_btns, lv_color_make(220, 220, 220), LV_PART_ITEMS | LV_STATE_DEFAULT);  // Dimmer white
    
    // Selected tab: Material Blue with bright white text
    lv_obj_set_style_bg_color(tab_btns, lv_color_make(33, 150, 243), LV_PART_ITEMS | LV_STATE_CHECKED);  // Material Blue #2196F3
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, lv_color_make(255, 255, 255), LV_PART_ITEMS | LV_STATE_CHECKED);  // Bright white

    // Add tabs - Scene Selector first (FR-010)
    s_tab_scenes = lv_tabview_add_tab(s_tabview, "Scene Selector");
    s_tab_manual = lv_tabview_add_tab(s_tabview, "Manual Control");

    // Set tab styles - light gray background
    lv_obj_set_style_bg_color(s_tab_scenes, lv_color_make(245, 245, 245), LV_PART_MAIN);  // #F5F5F5
    lv_obj_set_style_bg_color(s_tab_manual, lv_color_make(245, 245, 245), LV_PART_MAIN);

    // Create content for each tab
    ui_create_scenes_tab(s_tab_scenes);
    ui_create_manual_tab(s_tab_manual);

    ESP_LOGI(TAG, "Main screen created");

    ui_unlock();
}

/**
 * @brief Get the manual control tab object
 */
lv_obj_t* ui_get_manual_tab(void)
{
    return s_tab_manual;
}

/**
 * @brief Get the scene selector tab object
 */
lv_obj_t* ui_get_scenes_tab(void)
{
    return s_tab_scenes;
}

/**
 * @brief Show the main screen
 */
void ui_show_main(void)
{
    ESP_LOGI(TAG, "Showing main screen");
    ui_create_main_screen();
}

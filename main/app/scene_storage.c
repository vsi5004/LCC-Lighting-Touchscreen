/**
 * @file scene_storage.c
 * @brief Scene storage implementation - load/save scenes from/to SD card
 */

#include "scene_storage.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "scene_storage";

// Cached scenes
static ui_scene_t s_scenes[SCENE_STORAGE_MAX_SCENES];
static size_t s_scene_count = 0;

/**
 * @brief Initialize scene storage module
 */
esp_err_t scene_storage_init(void)
{
    ESP_LOGI(TAG, "Initializing scene storage");
    
    // Load scenes from SD card
    size_t count = 0;
    esp_err_t ret = scene_storage_load(s_scenes, SCENE_STORAGE_MAX_SCENES, &count);
    if (ret == ESP_OK) {
        s_scene_count = count;
        ESP_LOGI(TAG, "Loaded %d scenes from SD card", s_scene_count);
    } else {
        ESP_LOGW(TAG, "Failed to load scenes: %s", esp_err_to_name(ret));
        s_scene_count = 0;
    }
    
    return ESP_OK;
}

/**
 * @brief Load scenes from SD card
 */
esp_err_t scene_storage_load(ui_scene_t *scenes, size_t max_count, size_t *out_count)
{
    if (!scenes || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *out_count = 0;
    
    // Check if file exists (also check for .tmp as fallback from failed rename)
    struct stat st;
    const char *file_path = SCENE_STORAGE_PATH;
    
    if (stat(SCENE_STORAGE_PATH, &st) != 0) {
        // Try fallback to .tmp file (from previous failed atomic write)
        if (stat("/sdcard/scenes.tmp", &st) == 0) {
            file_path = "/sdcard/scenes.tmp";
            ESP_LOGW(TAG, "Using fallback scenes.tmp");
            // Try to fix it by renaming
            rename("/sdcard/scenes.tmp", SCENE_STORAGE_PATH);
        } else {
            ESP_LOGW(TAG, "scenes.json not found");
            return ESP_ERR_NOT_FOUND;
        }
    }
    
    // Read file
    FILE *file = fopen(file_path, "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open scenes.json");
        return ESP_FAIL;
    }
    
    // Allocate buffer for file content
    char *json_buf = malloc(st.st_size + 1);
    if (!json_buf) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate memory for JSON");
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(json_buf, 1, st.st_size, file);
    fclose(file);
    json_buf[read_size] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse scenes.json: %s", cJSON_GetErrorPtr());
        return ESP_FAIL;
    }
    
    // Get scenes array
    cJSON *scenes_array = cJSON_GetObjectItem(root, "scenes");
    if (!cJSON_IsArray(scenes_array)) {
        ESP_LOGE(TAG, "scenes.json: 'scenes' is not an array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    // Parse each scene
    size_t count = 0;
    cJSON *scene_obj = NULL;
    cJSON_ArrayForEach(scene_obj, scenes_array) {
        if (count >= max_count) {
            ESP_LOGW(TAG, "Scene limit reached (%d), ignoring remaining scenes", max_count);
            break;
        }
        
        cJSON *name = cJSON_GetObjectItem(scene_obj, "name");
        cJSON *brightness = cJSON_GetObjectItem(scene_obj, "brightness");
        cJSON *r = cJSON_GetObjectItem(scene_obj, "r");
        cJSON *g = cJSON_GetObjectItem(scene_obj, "g");
        cJSON *b = cJSON_GetObjectItem(scene_obj, "b");
        cJSON *w = cJSON_GetObjectItem(scene_obj, "w");
        
        if (!cJSON_IsString(name) || !cJSON_IsNumber(brightness) ||
            !cJSON_IsNumber(r) || !cJSON_IsNumber(g) || 
            !cJSON_IsNumber(b) || !cJSON_IsNumber(w)) {
            ESP_LOGW(TAG, "Skipping invalid scene at index %d", count);
            continue;
        }
        
        // Copy scene data
        strncpy(scenes[count].name, name->valuestring, sizeof(scenes[count].name) - 1);
        scenes[count].name[sizeof(scenes[count].name) - 1] = '\0';
        scenes[count].brightness = (uint8_t)brightness->valueint;
        scenes[count].red = (uint8_t)r->valueint;
        scenes[count].green = (uint8_t)g->valueint;
        scenes[count].blue = (uint8_t)b->valueint;
        scenes[count].white = (uint8_t)w->valueint;
        
        ESP_LOGI(TAG, "Loaded scene '%s': B=%d R=%d G=%d B=%d W=%d",
                 scenes[count].name, scenes[count].brightness,
                 scenes[count].red, scenes[count].green,
                 scenes[count].blue, scenes[count].white);
        
        count++;
    }
    
    cJSON_Delete(root);
    *out_count = count;
    
    // Update cache
    memcpy(s_scenes, scenes, count * sizeof(ui_scene_t));
    s_scene_count = count;
    
    return ESP_OK;
}

/**
 * @brief Save a new scene to SD card
 */
esp_err_t scene_storage_save(const char *name, uint8_t brightness,
                             uint8_t red, uint8_t green, uint8_t blue, uint8_t white)
{
    if (!name || strlen(name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Saving scene '%s': B=%d R=%d G=%d B=%d W=%d",
             name, brightness, red, green, blue, white);
    
    // Load existing scenes
    ui_scene_t scenes[SCENE_STORAGE_MAX_SCENES];
    size_t count = 0;
    scene_storage_load(scenes, SCENE_STORAGE_MAX_SCENES, &count);
    
    // Check if scene with same name exists (update) or add new
    int existing_idx = -1;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(scenes[i].name, name) == 0) {
            existing_idx = i;
            break;
        }
    }
    
    if (existing_idx >= 0) {
        // Update existing scene
        scenes[existing_idx].brightness = brightness;
        scenes[existing_idx].red = red;
        scenes[existing_idx].green = green;
        scenes[existing_idx].blue = blue;
        scenes[existing_idx].white = white;
        ESP_LOGI(TAG, "Updated existing scene at index %d", existing_idx);
    } else {
        // Add new scene
        if (count >= SCENE_STORAGE_MAX_SCENES) {
            ESP_LOGE(TAG, "Scene limit reached, cannot add new scene");
            return ESP_ERR_NO_MEM;
        }
        strncpy(scenes[count].name, name, sizeof(scenes[count].name) - 1);
        scenes[count].name[sizeof(scenes[count].name) - 1] = '\0';
        scenes[count].brightness = brightness;
        scenes[count].red = red;
        scenes[count].green = green;
        scenes[count].blue = blue;
        scenes[count].white = white;
        count++;
        ESP_LOGI(TAG, "Added new scene at index %d", (int)(count - 1));
    }
    
    // Build JSON
    cJSON *root = cJSON_CreateObject();
    cJSON *scenes_array = cJSON_CreateArray();
    
    for (size_t i = 0; i < count; i++) {
        cJSON *scene_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(scene_obj, "name", scenes[i].name);
        cJSON_AddNumberToObject(scene_obj, "brightness", scenes[i].brightness);
        cJSON_AddNumberToObject(scene_obj, "r", scenes[i].red);
        cJSON_AddNumberToObject(scene_obj, "g", scenes[i].green);
        cJSON_AddNumberToObject(scene_obj, "b", scenes[i].blue);
        cJSON_AddNumberToObject(scene_obj, "w", scenes[i].white);
        cJSON_AddItemToArray(scenes_array, scene_obj);
    }
    
    cJSON_AddItemToObject(root, "scenes", scenes_array);
    
    // Write directly to scenes.json (FAT doesn't support atomic rename well)
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return ESP_FAIL;
    }
    
    FILE *file = fopen(SCENE_STORAGE_PATH, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open scenes.json for writing");
        free(json_str);
        return ESP_FAIL;
    }
    
    size_t json_len = strlen(json_str);
    size_t written = fwrite(json_str, 1, json_len, file);
    fflush(file);  // Ensure data is written to SD card
    fclose(file);
    free(json_str);
    
    if (written != json_len) {
        ESP_LOGE(TAG, "Failed to write complete JSON (wrote %d of %d)", written, json_len);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Wrote %d bytes to %s", json_len, SCENE_STORAGE_PATH);
    
    // Update cache
    memcpy(s_scenes, scenes, count * sizeof(ui_scene_t));
    s_scene_count = count;
    
    ESP_LOGI(TAG, "Scene saved successfully, total scenes: %d", count);
    return ESP_OK;
}

/**
 * @brief Delete a scene by name
 */
esp_err_t scene_storage_delete(const char *name)
{
    if (!name || strlen(name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Load existing scenes
    ui_scene_t scenes[SCENE_STORAGE_MAX_SCENES];
    size_t count = 0;
    scene_storage_load(scenes, SCENE_STORAGE_MAX_SCENES, &count);
    
    // Find and remove scene
    int found_idx = -1;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(scenes[i].name, name) == 0) {
            found_idx = i;
            break;
        }
    }
    
    if (found_idx < 0) {
        ESP_LOGW(TAG, "Scene '%s' not found", name);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Shift remaining scenes
    for (size_t i = found_idx; i < count - 1; i++) {
        scenes[i] = scenes[i + 1];
    }
    count--;
    
    // Build and save JSON (same as save function)
    cJSON *root = cJSON_CreateObject();
    cJSON *scenes_array = cJSON_CreateArray();
    
    for (size_t i = 0; i < count; i++) {
        cJSON *scene_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(scene_obj, "name", scenes[i].name);
        cJSON_AddNumberToObject(scene_obj, "brightness", scenes[i].brightness);
        cJSON_AddNumberToObject(scene_obj, "r", scenes[i].red);
        cJSON_AddNumberToObject(scene_obj, "g", scenes[i].green);
        cJSON_AddNumberToObject(scene_obj, "b", scenes[i].blue);
        cJSON_AddNumberToObject(scene_obj, "w", scenes[i].white);
        cJSON_AddItemToArray(scenes_array, scene_obj);
    }
    
    cJSON_AddItemToObject(root, "scenes", scenes_array);
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (!json_str) {
        return ESP_FAIL;
    }
    
    FILE *file = fopen(SCENE_STORAGE_PATH, "w");
    if (!file) {
        free(json_str);
        return ESP_FAIL;
    }
    
    fwrite(json_str, 1, strlen(json_str), file);
    fclose(file);
    free(json_str);
    
    // Update cache
    memcpy(s_scenes, scenes, count * sizeof(ui_scene_t));
    s_scene_count = count;
    
    ESP_LOGI(TAG, "Scene '%s' deleted, remaining: %d", name, count);
    return ESP_OK;
}

/**
 * @brief Get the number of stored scenes
 */
size_t scene_storage_get_count(void)
{
    return s_scene_count;
}

/**
 * @brief Get the first scene (for auto-apply on boot)
 */
esp_err_t scene_storage_get_first(ui_scene_t *scene)
{
    if (!scene) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_scene_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    *scene = s_scenes[0];
    return ESP_OK;
}

/**
 * @brief Reload scenes and update UI
 */
void scene_storage_reload_ui(void)
{
    ESP_LOGI(TAG, "scene_storage_reload_ui called");
    
    ui_scene_t scenes[SCENE_STORAGE_MAX_SCENES];
    size_t count = 0;
    
    esp_err_t ret = scene_storage_load(scenes, SCENE_STORAGE_MAX_SCENES, &count);
    ESP_LOGI(TAG, "scene_storage_load returned %s, count=%d", esp_err_to_name(ret), count);
    
    // Lock LVGL before modifying UI (LVGL is not thread-safe)
    ui_lock();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calling ui_scenes_load_from_sd with %d scenes", count);
        ui_scenes_load_from_sd(scenes, count);
        ESP_LOGI(TAG, "UI updated with %d scenes", count);
    } else {
        ESP_LOGW(TAG, "Failed to reload scenes for UI: %s", esp_err_to_name(ret));
        ui_scenes_load_from_sd(NULL, 0);
    }
    
    ui_unlock();
}

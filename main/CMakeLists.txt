idf_component_register(
    SRCS 
        "main.c"
        "app/scene_storage.c"
        "app/lcc_node.cpp"
        "app/fade_controller.c"
        "app/screen_timeout.c"
        "ui/ui_common.c"
        "ui/ui_main.c"
        "ui/ui_manual.c"
        "ui/ui_scenes.c"
    INCLUDE_DIRS 
        "."
        "app"
        "ui"
    REQUIRES
        driver
        esp_lcd
        esp_timer
        fatfs
        nvs_flash
        board_drivers
        espressif__esp_jpeg
        lvgl__lvgl
        json
        OpenMRN
)

# Set C++ standard for OpenMRN compatibility
set_source_files_properties(
    "app/lcc_node.cpp"
    PROPERTIES COMPILE_FLAGS "-std=gnu++14 -fno-strict-aliasing"
)

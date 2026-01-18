# Board Drivers Component for Waveshare ESP32-S3 Touch LCD 4.3B
idf_component_register(
    SRCS
        "ch422g.c"
        "waveshare_lcd.c"
        "waveshare_touch.c"
        "waveshare_sd.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        driver
        esp_lcd
        esp_lcd_touch_gt911
        fatfs
        sdmmc
)

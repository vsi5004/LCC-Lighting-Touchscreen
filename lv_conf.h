/**
 * @file lv_conf.h
 * @brief LVGL configuration for ESP32-S3 LCC Lighting Controller
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color settings */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Font rendering quality */
#define LV_FONT_SUBPX 0
#define LV_FONT_FMT_TXT_LARGE 0

/* Memory settings */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64 * 1024U)
#define LV_MEM_ADR 0

/* Display settings */
#define LV_DISP_DEF_REFR_PERIOD 16  /* ~60 FPS for smooth scrolling */

/* Input device settings */
#define LV_INDEV_DEF_READ_PERIOD 10  /* Fast touch response */
#define LV_INDEV_DEF_SCROLL_THROW 5   /* Low momentum (default is 10) */
#define LV_INDEV_DEF_SCROLL_LIMIT 30  /* Scroll limit percentage (lower = less sensitive) */

/* Feature usage */
#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW 1
#define LV_USE_BLEND_MODES 1
#define LV_USE_OPA_SCALE 1
#define LV_USE_IMG_TRANSFORM 1

/* GPU settings */
#define LV_USE_GPU_SDL 0

/* Logging */
#define LV_USE_LOG 1
#if LV_USE_LOG
#define LV_LOG_LEVEL LV_LOG_LEVEL_INFO
#define LV_LOG_PRINTF 0
#endif

/* Font settings */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_32

/* Widget usage */
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TABLE 1
#define LV_USE_TABVIEW 1

/* Theme */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0

/* Layout */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* Other settings */
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_USE_ASSERT_STYLE 0

#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA 1
#define LV_ENABLE_GC 0

/* Tick settings */
#define LV_TICK_CUSTOM 0

#endif /* LV_CONF_H */

/**
 * @file lv_conf.h
 * @brief LVGL configuration for CrowPanel Advanced 7" ESP32-P4
 * Based on Elecrow's official examples and ESP32-P4 BSP
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

/*====================
   Color settings
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/*====================
   Memory settings
 *====================*/
#define LV_MEM_SIZE (64 * 1024U)          /* 64KB static memory */
#define LV_MEM_POOL_EXPAND_SIZE (512 * 1024U) /* Expand to 512KB from PSRAM */
#define LV_USE_PSRAM 1
#define LV_MEM_CUSTOM 1
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

/*====================
   Display settings
 *====================*/
#define LV_HOR_RES_MAX 1024
#define LV_VER_RES_MAX 600
#define LV_DPI_DEF 130

/*====================
   Input device settings
 *====================*/
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_INDEV_POINT_MARKER 0

/*====================
   Feature usage
 *====================*/
#define LV_USE_FONT_COMPRESSED 1
#define LV_USE_FONT_SUBPX 0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_FONT_MONO 1

/*====================
   Font usage
 *====================*/
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   Widget usage
 *====================*/
#define LV_USE_ARC 1
#define LV_USE_ANIMIMG 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CALENDAR 1
#define LV_USE_CANVAS 1
#define LV_USE_CHART 1
#define LV_USE_CHECKBOX 1
#define LV_USE_DROPDOWN 1
#define LV_USE_IMG 1
#define LV_USE_IMGBTN 1
#define LV_USE_KEYBOARD 1
#define LV_USE_LABEL 1
#define LV_USE_LED 1
#define LV_USE_LINE 1
#define LV_USE_LIST 1
#define LV_USE_MENU 1
#define LV_USE_MSGBOX 1
#define LV_USE_ROLLER 1
#define LV_USE_SLIDER 1
#define LV_USE_SPINBOX 1
#define LV_USE_SPINNER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 1
#define LV_USE_TABVIEW 1
#define LV_USE_TEXTAREA 1
#define LV_USE_TILEVIEW 1
#define LV_USE_WIN 1

/*====================
   Extensions
 *====================*/
#define LV_USE_PERF_MONITOR 1
#define LV_USE_MEM_MONITOR 1
#define LV_USE_DEMO_RENDER 0

/*====================
   Draw and cache
 *====================*/
#define LV_DRAW_COMPLEX 1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 8

/*====================
   Logging
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_INFO
#define LV_LOG_PRINTF 1

/*====================
   Assert
 *====================*/
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1
#define LV_USE_ASSERT_MEM_INTEGRITY 0


#ifdef __cplusplus
}
#endif

#endif /* LV_CONF_H */
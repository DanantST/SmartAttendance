/**
 * @file ui_theme.c
 * @brief Theme management for light and dark modes
 */

#include "ui_theme.h"
#include "ui_main.h"
#include "lvgl.h"
#include "config.h"

/* Theme colors */
static const lv_color_t dark_bg = lv_color_hex(0x121212);
static const lv_color_t dark_surface = lv_color_hex(0x1E1E1E);
static const lv_color_t dark_primary = lv_color_hex(0x3A6EA5);
static const lv_color_t dark_text = lv_color_hex(0xFFFFFF);
static const lv_color_t dark_text_secondary = lv_color_hex(0xAAAAAA);

static const lv_color_t light_bg = lv_color_hex(0xF5F5F5);
static const lv_color_t light_surface = lv_color_hex(0xFFFFFF);
static const lv_color_t light_primary = lv_color_hex(0x3A6EA5);
static const lv_color_t light_text = lv_color_hex(0x212121);
static const lv_color_t light_text_secondary = lv_color_hex(0x757575);

/*============================================================================
   Theme Application
 *===========================================================================*/

void ui_theme_apply_light(void) {
    /* Set main background */
    lv_obj_set_style_bg_color(lv_scr_act(), light_bg, 0);
    
    /* Status bar */
    /* Handled by ui_main.c */
    
    /* Buttons */
    lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, light_primary);
    lv_style_set_radius(&style_btn, 12);
    lv_style_set_shadow_width(&style_btn, 4);
    
    /* Cards */
    lv_style_t style_card;
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, light_surface);
    lv_style_set_radius(&style_card, 16);
    lv_style_set_shadow_width(&style_card, 8);
    
    /* Text */
    lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, light_text);
}

void ui_theme_apply_dark(void) {
    /* Set main background */
    lv_obj_set_style_bg_color(lv_scr_act(), dark_bg, 0);
    
    /* Status bar */
    /* Handled by ui_main.c */
    
    /* Buttons */
    lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_bg_color(&style_btn, dark_primary);
    lv_style_set_radius(&style_btn, 12);
    lv_style_set_shadow_width(&style_btn, 4);
    
    /* Cards */
    lv_style_t style_card;
    lv_style_init(&style_card);
    lv_style_set_bg_color(&style_card, dark_surface);
    lv_style_set_radius(&style_card, 16);
    lv_style_set_shadow_width(&style_card, 8);
    
    /* Text */
    lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, dark_text);
}
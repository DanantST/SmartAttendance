#ifndef UI_THEME_H
#define UI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lvgl.h"

void ui_theme_apply_light(void);
void ui_theme_apply_dark(void);

lv_color_t ui_theme_get_bg_color(void);
lv_color_t ui_theme_get_surface_color(void);
lv_color_t ui_theme_get_text_color(void);
lv_color_t ui_theme_get_text_muted_color(void);
lv_color_t ui_theme_get_header_color(void);
#ifdef __cplusplus
}
#endif

#endif
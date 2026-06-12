/**
 * @file ui_settings.h
 * @brief Settings screen with brightness, theme, Wi-Fi, etc.
 */

#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lvgl.h"

/**
 * @brief Show settings screen
 */
void ui_show_settings_screen(void);

/**
 * @brief Close settings screen and return to main
 */
void ui_close_settings_screen(void);

/**
 * @brief Get current brightness setting (0-100)
 * @return brightness percent
 */
int ui_get_brightness(void);

/**
 * @brief Set brightness (updates hardware)
 * @param percent brightness (0-100)
 */
void ui_set_brightness(int percent);


#ifdef __cplusplus
}
#endif

#endif /* UI_SETTINGS_H */
/**
 * @file ui_components.h
 * @brief Reusable UI components (keyboard, dropdown, cards)
 */

#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lvgl.h"

/**
 * @brief Create a dropdown notification panel
 * @param parent parent object
 * @param title panel title
 * @param messages array of message strings
 * @param count number of messages
 * @return panel object
 */
lv_obj_t* ui_create_notification_dropdown(lv_obj_t* parent, const char* title, 
                                           const char** messages, int count);

/**
 * @brief Create a popup keyboard with custom layout
 * @param parent parent object
 * @param textarea textarea to attach keyboard to
 * @param mode keyboard mode (text, number, special)
 * @return keyboard object
 */
lv_obj_t* ui_create_popup_keyboard(lv_obj_t* parent, lv_obj_t* textarea, 
                                    lv_keyboard_mode_t mode);

/**
 * @brief Create a dropdown settings bar
 * @param parent parent object
 * @param options list of option strings
 * @param count number of options
 * @param selected_callback callback when option selected
 * @return dropdown object
 */
lv_obj_t* ui_create_settings_dropdown(lv_obj_t* parent, const char** options, 
                                       int count, void (*selected_callback)(int));

/**
 * @brief Create a modern card with shadow
 * @param parent parent object
 * @param width card width
 * @param height card height
 * @param content function to populate card content
 * @return card object
 */
lv_obj_t* ui_create_modern_card(lv_obj_t* parent, int width, int height,
                                 void (*content)(lv_obj_t* card));


#ifdef __cplusplus
}
#endif

#endif /* UI_COMPONENTS_H */
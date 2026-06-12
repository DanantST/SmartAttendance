/**
 * @file ui_components.c
 * @brief Reusable UI component implementations
 */

#include "ui_components.h"
#include "ui_theme.h"
#include "esp_log.h"
#include "config.h"
#include <cstring>

static const char* TAG __attribute__((unused)) = "UI_COMP";

/* Static callback for dropdown events */
static void dropdown_selected_cb(lv_event_t *e) {
    lv_obj_t* dropdown = (lv_obj_t*)lv_event_get_target(e);
    int selected = lv_dropdown_get_selected(dropdown);
    void (*cb)(int) = (void (*)(int))lv_event_get_user_data(e);
    if (cb) cb(selected);
}

lv_obj_t* ui_create_notification_dropdown(lv_obj_t* parent, const char* title,
                                           const char** messages, int count) {
    /* Create panel */
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 300, 400);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_shadow_width(panel, 10, 0);
    
    /* Title */
    lv_obj_t* title_label = lv_label_create(panel);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 15, 10);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    
    /* Separator */
    lv_obj_t* sep = lv_obj_create(panel);
    lv_obj_set_size(sep, 270, 1);
    lv_obj_set_pos(sep, 15, 40);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    
    /* Message list */
    lv_obj_t* list = lv_list_create(panel);
    lv_obj_set_size(list, 270, 340);
    lv_obj_set_pos(list, 15, 45);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x333333), 0);
    lv_obj_set_style_radius(list, 8, 0);
    
    for (int i = 0; i < count && i < 10; i++) {
        lv_obj_t* btn = lv_list_add_btn(list, NULL, messages[i]);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_width(btn, 0, 0);
    }
    
    return panel;
}

lv_obj_t* ui_create_popup_keyboard(lv_obj_t* parent, lv_obj_t* textarea,
                                    lv_keyboard_mode_t mode) {
    lv_obj_t* keyboard = lv_keyboard_create(parent);
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_keyboard_set_mode(keyboard, mode);
    return keyboard;
}

lv_obj_t* ui_create_settings_dropdown(lv_obj_t* parent, const char** options,
                                       int count, void (*selected_callback)(int)) {
    lv_obj_t* dropdown = lv_dropdown_create(parent);
    
    /* Build options string */
    char options_str[512] = {0};
    for (int i = 0; i < count; i++) {
        strcat(options_str, options[i]);
        if (i < count - 1) strcat(options_str, "\n");
    }
    
    lv_dropdown_set_options(dropdown, options_str);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(0x3A6EA5), 0);
    lv_obj_set_style_text_color(dropdown, lv_color_white(), 0);
    
    if (selected_callback) {
        lv_obj_add_event_cb(dropdown, dropdown_selected_cb, LV_EVENT_VALUE_CHANGED, (void*)selected_callback);
    }
    
    return dropdown;
}

lv_obj_t* ui_create_modern_card(lv_obj_t* parent, int width, int height,
                                 void (*content)(lv_obj_t* card)) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_ofs_x(card, 0, 0);
    lv_obj_set_style_shadow_ofs_y(card, 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    
    if (content) {
        content(card);
    }
    
    return card;
}
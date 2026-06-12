/**
 * @file ui_user_manager.cpp
 * @brief User management screen implementation
 */

#include "ui_user_manager.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "database/db_manager.h"
#include "recognition/recognizer.h"
#include "lvgl.h"
#include "esp_log.h"
#include "config.h"
#include <string.h>

static const char* TAG = "UI_USER";

static lv_obj_t* s_user_screen = NULL;
static lv_obj_t* s_user_list = NULL;

static void create_user_manager_screen(void);
static void delete_user_event_handler(lv_event_t* e);
static void close_btn_event_handler(lv_event_t* e);

void ui_show_user_manager(void) {
    if (s_user_screen) {
        lv_scr_load(s_user_screen);
        return;
    }
    create_user_manager_screen();
}

void ui_close_user_manager(void) {
    if (!s_user_screen) return;
    s_user_screen = NULL;
    s_user_list = NULL;
    ui_show_settings_screen();
}

static void create_user_manager_screen(void) {
    s_user_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_user_screen);
    lv_obj_set_style_bg_color(s_user_screen, lv_color_hex(0x121212), 0);
    
    /* Header */
    lv_obj_t* header = lv_obj_create(s_user_screen);
    lv_obj_set_size(header, DISPLAY_WIDTH, 50);
    lv_obj_set_pos(header, 0, 40);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    
    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "User Management");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 20, 0);
    
    lv_obj_t* close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_add_event_cb(close_btn, close_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_center(close_icon);
    
    /* User List */
    s_user_list = lv_obj_create(s_user_screen);
    lv_obj_set_size(s_user_list, DISPLAY_WIDTH - 40, 440);
    lv_obj_set_pos(s_user_list, 20, 95);
    lv_obj_set_style_bg_opa(s_user_list, 0, 0);
    lv_obj_set_style_border_width(s_user_list, 0, 0);
    lv_obj_set_flex_flow(s_user_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_user_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_user_list, LV_SCROLLBAR_MODE_AUTO);
    
    /* Populate List */
    user_t* users = NULL;
    int count = 0;
    if (db_get_all_users(&users, &count) == ESP_OK && users && count > 0) {
        for (int i = 0; i < count; i++) {
            lv_obj_t* item = ui_create_card(s_user_list, DISPLAY_WIDTH - 60, 110);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(item, 20, 0);
            lv_obj_set_style_pad_ver(item, 10, 0);
            
            // Info Column
            lv_obj_t* info_col = lv_obj_create(item);
            lv_obj_set_size(info_col, DISPLAY_WIDTH - 200, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(info_col, 0, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            
            // Row 1: Name and Role Badge
            lv_obj_t* row1 = lv_obj_create(info_col);
            lv_obj_set_size(row1, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row1, 0, 0);
            lv_obj_set_style_border_width(row1, 0, 0);
            lv_obj_set_style_pad_all(row1, 0, 0);
            lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            
            lv_obj_t* name = lv_label_create(row1);
            lv_label_set_text(name, users[i].name);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(name, lv_color_white(), 0);
            
            lv_obj_t* badge = lv_label_create(row1);
            char role_buf[32];
            snprintf(role_buf, sizeof(role_buf), "  %s  ", users[i].role);
            if (role_buf[2] >= 'a' && role_buf[2] <= 'z') role_buf[2] -= 32;
            lv_label_set_text(badge, role_buf);
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(badge, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            if (strcmp(users[i].role, "admin") == 0) {
                lv_obj_set_style_bg_color(badge, lv_color_hex(0xFF9900), 0);
            } else if (strcmp(users[i].role, "lecturer") == 0) {
                lv_obj_set_style_bg_color(badge, lv_color_hex(0x3B82F6), 0);
            } else {
                lv_obj_set_style_bg_color(badge, lv_color_hex(0x10B981), 0);
            }
            lv_obj_set_style_radius(badge, 6, 0);
            lv_obj_set_style_margin_left(badge, 10, 0);
            
            // Row 2: ID and Phone number
            lv_obj_t* row2 = lv_obj_create(info_col);
            lv_obj_set_size(row2, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row2, 0, 0);
            lv_obj_set_style_border_width(row2, 0, 0);
            lv_obj_set_style_pad_all(row2, 0, 0);
            lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_margin_top(row2, 4, 0);
            
            char details_text[128] = {0};
            if (strcmp(users[i].role, "student") == 0) {
                snprintf(details_text, sizeof(details_text), "ID: %s  |  Phone: %s", 
                         users[i].student_id[0] ? users[i].student_id : "N/A",
                         users[i].phone_number[0] ? users[i].phone_number : "N/A");
            } else {
                snprintf(details_text, sizeof(details_text), "Phone: %s  |  Telegram ID: %s", 
                         users[i].phone_number[0] ? users[i].phone_number : "N/A",
                         users[i].telegram_id[0] ? users[i].telegram_id : "Not Linked");
            }
            lv_obj_t* details = lv_label_create(row2);
            lv_label_set_text(details, details_text);
            lv_obj_set_style_text_font(details, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(details, lv_color_hex(0xBBBBBB), 0);
            
            // Row 3: Enrolled Courses
            lv_obj_t* row3 = lv_obj_create(info_col);
            lv_obj_set_size(row3, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row3, 0, 0);
            lv_obj_set_style_border_width(row3, 0, 0);
            lv_obj_set_style_pad_all(row3, 0, 0);
            lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_margin_top(row3, 4, 0);
            
            char **courses = NULL;
            int course_count = 0;
            char courses_text[256] = "Courses: None";
            if (db_get_user_courses(users[i].id, &courses, &course_count) == ESP_OK && courses && course_count > 0) {
                strcpy(courses_text, "Courses: ");
                for (int c = 0; c < course_count; c++) {
                    strcat(courses_text, courses[c]);
                    if (c < course_count - 1) {
                        strcat(courses_text, ", ");
                    }
                    free(courses[c]);
                }
                free(courses);
            }
            
            lv_obj_t* course_lbl = lv_label_create(row3);
            lv_label_set_text(course_lbl, courses_text);
            lv_obj_set_style_text_font(course_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(course_lbl, lv_color_hex(0x3B82F6), 0);
            
            // Delete button on the right
            lv_obj_t* del_btn = lv_btn_create(item);
            lv_obj_set_size(del_btn, 45, 45);
            lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xCC3333), 0);
            lv_obj_set_user_data(del_btn, (void*)(uintptr_t)users[i].id);
            lv_obj_add_event_cb(del_btn, delete_user_event_handler, LV_EVENT_CLICKED, NULL);
            
            lv_obj_t* del_icon = lv_label_create(del_btn);
            lv_label_set_text(del_icon, LV_SYMBOL_TRASH);
            lv_obj_center(del_icon);
        }
        free(users);
    } else {
        lv_obj_t* empty_label = lv_label_create(s_user_list);
        lv_label_set_text(empty_label, "No users enrolled.");
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);
    }
    
    lv_scr_load(s_user_screen);
}

static void delete_user_event_handler(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uint32_t user_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
    
    ESP_LOGI(TAG, "Deleting user ID: %u", (unsigned int)user_id);
    
    if (db_delete_user(user_id) == ESP_OK) {
        /* Refresh recognizer cache */
        recognizer_load_cache();
        ui_show_notification(NOTIFY_SUCCESS, "User Management", "User deleted successfully", 2000);
        
        /* Refresh list */
        lv_obj_delete_async(lv_obj_get_parent(btn));
        if (lv_obj_get_child_count(s_user_list) == 0) {
            lv_obj_t* empty_label = lv_label_create(s_user_list);
            lv_label_set_text(empty_label, "No users enrolled.");
            lv_obj_set_style_text_color(empty_label, lv_color_hex(0x888888), 0);
        }
    } else {
        ui_show_notification(NOTIFY_ERROR, "User Management", "Failed to delete user", 2000);
    }
}

static void close_btn_event_handler(lv_event_t* e) {
    ui_close_user_manager();
}

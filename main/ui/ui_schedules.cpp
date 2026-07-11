/**
 * @file ui_schedules.cpp
 * @brief Schedules screen implementation
 */

#include "ui_schedules.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "database/db_manager.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include "config.h"

static const char* TAG = "UI_SCHEDULES";

/* Schedules screen objects */
static lv_obj_t* s_schedules_screen = NULL;

/* Forward declarations */
static void create_schedules_screen(void);
static void close_btn_event(lv_event_t* e);

void ui_show_schedules_screen(void) {
    if (s_schedules_screen) {
        lv_scr_load(s_schedules_screen);
        return;
    }
    create_schedules_screen();
}

void ui_close_schedules_screen(void) {
    if (!s_schedules_screen) return;
    if (lv_scr_act() == s_schedules_screen) {
        ui_return_to_main();
        return;
    }
    lv_obj_del(s_schedules_screen);
    s_schedules_screen = NULL;
}

static void create_schedules_screen(void) {
    s_schedules_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_schedules_screen);
    lv_obj_set_style_bg_color(s_schedules_screen, ui_theme_get_bg_color(), 0);
    lv_scr_load(s_schedules_screen);

    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_schedules_screen);
    lv_obj_set_size(title_bar, DISPLAY_WIDTH, 40);
    lv_obj_set_pos(title_bar, 0, 40);
    lv_obj_set_style_bg_color(title_bar, ui_theme_get_header_color(), 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 0, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Upcoming Timetable");
    lv_obj_set_pos(title_label, 20, 12);
    lv_obj_set_style_text_color(title_label, ui_theme_get_text_color(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);

    /* Close button */
    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_set_pos(close_btn, DISPLAY_WIDTH - 50, 5);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_radius(close_btn, 20, 0);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_add_event_cb(close_btn, close_btn_event, LV_EVENT_CLICKED, NULL);

    /* Scrollable content area */
    lv_obj_t* content = lv_obj_create(s_schedules_screen);
    lv_obj_set_size(content, DISPLAY_WIDTH, 370);
    lv_obj_set_pos(content, 0, 95);
    lv_obj_set_style_bg_color(content, ui_theme_get_bg_color(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 15, 0);

    /* Query schedules from database */
    db_schedule_t *schedules = NULL;
    int count = 0;
    esp_err_t ret = db_get_future_schedules(&schedules, &count);

    if (ret == ESP_OK && count > 0) {
        ESP_LOGI(TAG, "Displaying %d future schedules", count);
        for (int i = 0; i < count; i++) {
            db_schedule_t *s = &schedules[i];

            /* Create Card for schedule */
            lv_obj_t* card = ui_create_card(content, DISPLAY_WIDTH - 40, 110);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_all(card, 15, 0);

            /* Course Title & Code Label */
            lv_obj_t* course_lbl = lv_label_create(card);
            char course_text[128];
            snprintf(course_text, sizeof(course_text), "📚  %s - %s", s->course_code, s->course_name);
            lv_label_set_text(course_lbl, course_text);
            lv_obj_set_style_text_color(course_lbl, lv_color_hex(0x00A8FF), 0);
            lv_obj_set_style_text_font(course_lbl, &lv_font_montserrat_16, 0);

            /* Lecturer Name Label */
            lv_obj_t* lecturer_lbl = lv_label_create(card);
            char lecturer_text[128];
            snprintf(lecturer_text, sizeof(lecturer_text), "👤  Lecturer: %s", s->lecturer_name);
            lv_label_set_text(lecturer_lbl, lecturer_text);
            lv_obj_set_style_text_color(lecturer_lbl, ui_theme_get_text_color(), 0);
            lv_obj_set_style_text_font(lecturer_lbl, &lv_font_montserrat_12, 0);

            /* Time and Date Label */
            lv_obj_t* time_lbl = lv_label_create(card);
            char time_text[128] = "📅  Date & Time: Unknown";
            
            time_t start_time = (time_t)s->start_time;
            time_t end_time = (time_t)s->end_time;
            struct tm *tm_start = localtime(&start_time);
            
            /* Since localtime returns a static shared structure, copy tm_start's contents
               first before calling localtime on end_time. */
            if (tm_start) {
                struct tm tm_start_copy = *tm_start;
                struct tm *tm_end = localtime(&end_time);
                if (tm_end) {
                    snprintf(time_text, sizeof(time_text), "📅  Date: %02d/%02d/%04d    Time: %02d:%02d - %02d:%02d",
                             tm_start_copy.tm_mday, tm_start_copy.tm_mon + 1, tm_start_copy.tm_year + 1900,
                             tm_start_copy.tm_hour, tm_start_copy.tm_min,
                             tm_end->tm_hour, tm_end->tm_min);
                }
            }
            
            lv_label_set_text(time_lbl, time_text);
            lv_obj_set_style_text_color(time_lbl, ui_theme_get_text_muted_color(), 0);
            lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_12, 0);
        }
        
        free(schedules);
    } else {
        ESP_LOGI(TAG, "No upcoming schedules found");
        lv_obj_t* empty_card = ui_create_card(content, DISPLAY_WIDTH - 40, 100);
        
        lv_obj_t* empty_lbl = lv_label_create(empty_card);
        lv_label_set_text(empty_lbl, "No upcoming schedules found.\nClasses scheduled via the Telegram bot will appear here.");
        lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(empty_lbl, ui_theme_get_text_muted_color(), 0);
        lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(empty_lbl);
    }
}

static void close_btn_event(lv_event_t* e) {
    ui_close_schedules_screen();
}

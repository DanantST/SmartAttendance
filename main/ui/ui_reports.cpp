/**
 * @file ui_reports.c
 * @brief Reports screen implementation
 */

#include "ui_reports.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "database/db_manager.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include "config.h"

static const char* TAG __attribute__((unused)) = "UI_REPORTS";

/* Reports screen objects */
static lv_obj_t* s_reports_screen  = NULL;
static lv_obj_t* s_course_dropdown = NULL;
static lv_obj_t* s_report_textarea = NULL;
static lv_obj_t* s_generate_btn    = NULL;
static lv_obj_t* s_export_btn      = NULL;

/* Date picker — three dropdowns replace the old lv_calendar */
static lv_obj_t* s_day_dd   = NULL;
static lv_obj_t* s_month_dd = NULL;
static lv_obj_t* s_year_dd  = NULL;

/* Course ID tracking for dropdown mapping */
static int* s_course_ids = NULL;
static int  s_course_count = 0;

/* Forward declarations */
static void create_reports_screen(void);
static void generate_btn_event(lv_event_t* e);
static void export_btn_event(lv_event_t* e);
static void close_btn_event(lv_event_t* e);

void ui_show_reports_screen(void) {
    if (s_reports_screen) {
        lv_scr_load(s_reports_screen);
        return;
    }
    create_reports_screen();
}

void ui_close_reports_screen(void) {
    if (!s_reports_screen) return;
    
    if (s_course_ids) {
        free(s_course_ids);
        s_course_ids = NULL;
    }
    s_course_count = 0;

    if (lv_scr_act() == s_reports_screen) {
        ui_return_to_main();
        return;
    }
    lv_obj_del(s_reports_screen);
    s_reports_screen  = NULL;
    s_course_dropdown = NULL;
    s_report_textarea = NULL;
    s_generate_btn    = NULL;
    s_export_btn      = NULL;
    s_day_dd          = NULL;
    s_month_dd        = NULL;
    s_year_dd         = NULL;
}

static void create_reports_screen(void) {
    s_reports_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_reports_screen);
    lv_obj_set_style_bg_color(s_reports_screen, ui_theme_get_bg_color(), 0);
    lv_scr_load(s_reports_screen);

    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_reports_screen);
    lv_obj_set_size(title_bar, DISPLAY_WIDTH, 40);
    lv_obj_set_pos(title_bar, 0, 40);
    lv_obj_set_style_bg_color(title_bar, ui_theme_get_header_color(), 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 0, 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_remove_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Attendance Reports");
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_text_color(title_label, ui_theme_get_text_color(), 0);

    /* Close button */
    lv_obj_t* close_btn = lv_btn_create(title_bar);
    lv_obj_set_size(close_btn, 32, 32);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_radius(close_btn, 16, 0);

    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_add_event_cb(close_btn, close_btn_event, LV_EVENT_CLICKED, NULL);

    /* Scrollable content area */
    lv_obj_t* content = lv_obj_create(s_reports_screen);
    lv_obj_set_size(content, DISPLAY_WIDTH, 450);
    lv_obj_set_pos(content, 0, 90);
    lv_obj_set_style_bg_color(content, ui_theme_get_bg_color(), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* ── Course selection card ── */
    lv_obj_t* course_card = ui_create_card(content, DISPLAY_WIDTH - 40, 110);

    lv_obj_t* course_label = lv_label_create(course_card);
    lv_label_set_text(course_label, "Select Course");
    lv_obj_set_pos(course_label, 15, 15);

    s_course_dropdown = lv_dropdown_create(course_card);
    lv_obj_set_size(s_course_dropdown, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_course_dropdown, 15, 50);
    lv_dropdown_set_options(s_course_dropdown, "Loading courses...");

    /* ── Date picker card — three dropdowns (Day / Month / Year) ── */
    lv_obj_t* date_card = ui_create_card(content, DISPLAY_WIDTH - 40, 100);

    lv_obj_t* date_title = lv_label_create(date_card);
    lv_label_set_text(date_title, "Date");
    lv_obj_set_pos(date_title, 15, 12);
    lv_obj_set_style_text_color(date_title, ui_theme_get_text_color(), 0);

    /* Day dropdown — 1..31 */
    s_day_dd = lv_dropdown_create(date_card);
    lv_obj_set_size(s_day_dd, 80, 40);
    lv_obj_set_pos(s_day_dd, 15, 48);
    {
        char opts[256] = "";
        for (int d = 1; d <= 31; d++) {
            char tmp[8];
            snprintf(tmp, sizeof(tmp), d < 31 ? "%02d\n" : "%02d", d);
            strcat(opts, tmp);
        }
        lv_dropdown_set_options(s_day_dd, opts);
        /* Default: today */
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        if (t && t->tm_year + 1900 >= 2024) lv_dropdown_set_selected(s_day_dd, t->tm_mday - 1);
    }

    /* Month dropdown — Jan..Dec */
    s_month_dd = lv_dropdown_create(date_card);
    lv_obj_set_size(s_month_dd, 110, 40);
    lv_obj_set_pos(s_month_dd, 105, 48);
    lv_dropdown_set_options(s_month_dd,
        "January\nFebruary\nMarch\nApril\nMay\nJune\n"
        "July\nAugust\nSeptember\nOctober\nNovember\nDecember");
    {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        if (t && t->tm_year + 1900 >= 2024) lv_dropdown_set_selected(s_month_dd, t->tm_mon);
    }

    /* Year dropdown — 2025..2030 */
    s_year_dd = lv_dropdown_create(date_card);
    lv_obj_set_size(s_year_dd, 100, 40);
    lv_obj_set_pos(s_year_dd, 225, 48);
    lv_dropdown_set_options(s_year_dd, "2025\n2026\n2027\n2028\n2029\n2030");
    {
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        if (t && t->tm_year + 1900 >= 2025 && t->tm_year + 1900 <= 2030)
            lv_dropdown_set_selected(s_year_dd, t->tm_year + 1900 - 2025);
    }

    /* Buttons */
    s_generate_btn = ui_create_button(content, "Generate Report", 200, 50);
    lv_obj_add_event_cb(s_generate_btn, generate_btn_event, LV_EVENT_CLICKED, NULL);

    s_export_btn = ui_create_button(content, "Export CSV", 150, 50);
    lv_obj_add_flag(s_export_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_export_btn, export_btn_event, LV_EVENT_CLICKED, NULL);

    /* Report display area */
    s_report_textarea = lv_textarea_create(content);
    lv_obj_set_size(s_report_textarea, DISPLAY_WIDTH - 40, 200);
    lv_textarea_set_placeholder_text(s_report_textarea, "Report will appear here...");
    lv_textarea_set_text(s_report_textarea, "");
    lv_obj_set_style_bg_color(s_report_textarea, ui_theme_get_surface_color(), 0);
    lv_obj_set_style_text_color(s_report_textarea, ui_theme_get_text_color(), 0);

    /* Fetch and populate courses from database */
    int* c_ids = NULL;
    char** c_names = NULL;
    int c_count = 0;
    if (db_get_all_courses_with_ids(&c_ids, &c_names, &c_count) == ESP_OK && c_count > 0) {
        ui_reports_populate_courses(c_ids, (const char**)c_names, c_count);
        for (int i = 0; i < c_count; i++) {
            free(c_names[i]);
        }
        free(c_names);
        free(c_ids);
    } else {
        const char* fallback[] = {"All Courses"};
        ui_reports_populate_courses(NULL, fallback, 0);
    }
}

static void generate_btn_event(lv_event_t* e) {
    /* Read selected course */
    int selected_idx = lv_dropdown_get_selected(s_course_dropdown);
    int target_course_id = 0; /* 0 = All Courses */
    if (selected_idx > 0 && s_course_ids && (selected_idx - 1) < s_course_count) {
        target_course_id = s_course_ids[selected_idx - 1];
    }

    /* Read date from the three dropdowns */
    int day   = (int)lv_dropdown_get_selected(s_day_dd) + 1;   /* 1-31 */
    int month = (int)lv_dropdown_get_selected(s_month_dd) + 1; /* 1-12 */
    int year  = (int)lv_dropdown_get_selected(s_year_dd) + 2025;
    ESP_LOGI(TAG, "Generate report: course_id=%d date=%04d-%02d-%02d", target_course_id, year, month, day);

    /* Convert selected date to unix-day boundary */
    struct tm day_start;
    memset(&day_start, 0, sizeof(day_start));
    day_start.tm_year = year - 1900;
    day_start.tm_mon  = month - 1;
    day_start.tm_mday = day;
    time_t ts_start = mktime(&day_start);

    char *report_str = NULL;
    esp_err_t ret = db_get_attendance_report(&report_str,
                                             target_course_id,
                                             (int)ts_start);
    if (ret == ESP_OK && report_str) {
        lv_textarea_set_text(s_report_textarea, report_str);
        lv_obj_clear_flag(s_export_btn, LV_OBJ_FLAG_HIDDEN);
        free(report_str);
    } else {
        lv_textarea_set_text(s_report_textarea,
            "No records found for the selected filter.");
    }
}

static void export_btn_event(lv_event_t* e) {
    /* Generate CSV and save to SD card */
    const char* text = lv_textarea_get_text(s_report_textarea);
    if (strlen(text) == 0) return;
    
    /* Open file on SD card */
    FILE* f = fopen("/sdcard/attendance_report.csv", "w");
    if (f) {
        fprintf(f, "%s", text);
        fclose(f);
        ui_show_notification(NOTIFY_SUCCESS, "Export", "Report saved to SD card", 2000);
    } else {
        ui_show_notification(NOTIFY_ERROR, "Export", "Failed to save report", 2000);
    }
}

static void close_btn_event(lv_event_t* e) {
    ui_close_reports_screen();
}

void ui_reports_populate_courses(const int* ids, const char** courses, int count) {
    if (!s_course_dropdown) return;
    
    if (s_course_ids) {
        free(s_course_ids);
        s_course_ids = NULL;
    }
    s_course_count = count;

    if (count > 0 && ids) {
        s_course_ids = (int*)malloc(count * sizeof(int));
        if (s_course_ids) {
            memcpy(s_course_ids, ids, count * sizeof(int));
        }
    }

    size_t buf_size = 64;
    for (int i = 0; i < count; i++) {
        buf_size += strlen(courses[i]) + 2;
    }

    char* options = (char*)malloc(buf_size);
    if (!options) return;

    strcpy(options, "All Courses");
    for (int i = 0; i < count; i++) {
        strcat(options, "\n");
        strcat(options, courses[i]);
    }
    lv_dropdown_set_options(s_course_dropdown, options);
    free(options);
}

void ui_reports_show_data(const char* data, size_t len) {
    if (!s_report_textarea) return;
    lv_textarea_set_text(s_report_textarea, data);
    lv_obj_clear_flag(s_export_btn, LV_OBJ_FLAG_HIDDEN);
}
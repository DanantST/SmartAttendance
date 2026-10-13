/**
 * @file ui_user_manager.cpp
 * @brief User management screen implementation
 *
 * Deadlock-safe design
 * --------------------
 * ABBA deadlock risk: the detection task holds s_db_mutex while waiting for
 * s_lvgl_mux; any LVGL callback that calls DB functions will deadlock.
 *
 * All DB work is offloaded to background FreeRTOS tasks:
 *   - user_mgr_populate_task : populates the user list (original fix)
 *   - delete_user_task       : handles user deletion (BUG-3 fix)
 *   - reenroll_user_task     : handles re-enrollment (BUG-3 fix)
 *
 * Event handlers extract user_id and spawn the appropriate task immediately;
 * they perform NO DB calls and return to the LVGL event loop instantly.
 *
 * BUG-1 fix: every per-card LVGL allocation failure calls lv_obj_delete(item)
 * before continue, preventing dangling partial widget trees that cause
 * Store Access Faults in lv_draw_add_task.
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
#include "esp_heap_caps.h"
#include "ble/ble_registration.h"
#include "ui_enrollment.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "UI_USER";

static lv_obj_t* s_user_screen = NULL;
static lv_obj_t* s_user_list   = NULL;

/* ---- forward declarations ---- */
static void create_user_manager_screen(void);
static void delete_user_event_handler(lv_event_t* e);
static void reenroll_user_event_handler(lv_event_t* e);
static void close_btn_event_handler(lv_event_t* e);
static void user_mgr_populate_task(void* arg);
static void delete_user_task(void* arg);
static void reenroll_user_task(void* arg);

/* Context structs for background action tasks (BUG-3) */
typedef struct { uint32_t user_id; } delete_ctx_t;
typedef struct { uint32_t user_id; } reenroll_ctx_t;

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

void ui_show_user_manager(void) {
    /* Guard against stale pointer: if the screen object was destroyed but
     * the static was not cleared (e.g., closed via ui_return_to_main),
     * treat it as if no screen exists and build fresh. */
    if (s_user_screen && lv_obj_is_valid(s_user_screen)) {
        lv_scr_load(s_user_screen);
        return;
    }
    /* Reset statics in case they were left dirty */
    s_user_screen = NULL;
    s_user_list   = NULL;
    create_user_manager_screen();
}

void ui_close_user_manager(void) {
    if (!s_user_screen) return;

    if (lv_scr_act() == s_user_screen) {
        /* Null statics BEFORE navigating away so that if anything
         * triggers ui_show_user_manager during the transition it
         * builds a fresh screen rather than reloading the one we
         * are about to destroy. */
        lv_obj_t* scr  = s_user_screen;
        s_user_screen  = NULL;
        s_user_list    = NULL;
        ui_return_to_main();
        /* Destroy the old screen after the main screen is active */
        lv_obj_delete_async(scr);
        return;
    }

    /* Screen is not currently displayed — safe to delete immediately. */
    lv_obj_delete(s_user_screen);
    s_user_screen = NULL;
    s_user_list   = NULL;
}

/* ------------------------------------------------------------------ */
/*  Screen skeleton – fast, no DB calls, safe to run on LVGL task      */
/* ------------------------------------------------------------------ */

static void create_user_manager_screen(void) {
    s_user_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_user_screen);
    lv_obj_set_style_bg_color(s_user_screen, ui_theme_get_bg_color(), 0);

    /* ---- Header ---- */
    lv_obj_t* header = lv_obj_create(s_user_screen);
    lv_obj_set_size(header, DISPLAY_WIDTH, 40);
    lv_obj_set_pos(header, 0, 40);
    lv_obj_set_style_bg_color(header, ui_theme_get_header_color(), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "User Management");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, ui_theme_get_text_color(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t* close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_add_event_cb(close_btn, close_btn_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_icon = lv_label_create(close_btn);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_center(close_icon);

    /* ---- Scrollable user list container ---- */
    s_user_list = lv_obj_create(s_user_screen);
    lv_obj_set_size(s_user_list, DISPLAY_WIDTH - 40, 440);
    lv_obj_set_pos(s_user_list, 20, 95);
    lv_obj_set_style_bg_opa(s_user_list, 0, 0);
    lv_obj_set_style_border_width(s_user_list, 0, 0);
    lv_obj_set_flex_flow(s_user_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_user_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_user_list, LV_SCROLLBAR_MODE_AUTO);

    /* ---- Loading spinner while background task fetches DB ---- */
    lv_obj_t* spinner = lv_spinner_create(s_user_list);
    lv_obj_set_size(spinner, 60, 60);
    lv_obj_center(spinner);
    lv_obj_t* loading_lbl = lv_label_create(s_user_list);
    lv_label_set_text(loading_lbl, "Loading users...");
    lv_obj_set_style_text_color(loading_lbl, ui_theme_get_text_muted_color(), 0);
    lv_obj_center(loading_lbl);

    /* Load the screen NOW (fast – no DB queries above) */
    lv_scr_load(s_user_screen);

    /* Spawn background task to do DB work and populate the list */
    xTaskCreate(user_mgr_populate_task, "usr_mgr_pop", 8192, NULL, 3, NULL);
}

/* ------------------------------------------------------------------ */
/*  Background population task                                          */
/*  Runs with NO LVGL mutex held.  Acquires it only to paint widgets.  */
/* ------------------------------------------------------------------ */

/* Per-user data gathered by the background task */
typedef struct {
    user_t   u;
    char     courses_text[256];
} user_row_t;

static void user_mgr_populate_task(void* arg) {
    /* ---- Step 1: all DB work, s_lvgl_mux NOT held ---- */
    user_t* users = NULL;
    int     count = 0;

    esp_err_t ret = db_get_all_users(&users, &count);
    if (ret != ESP_OK || !users || count <= 0) {
        count = 0;
    }

    /* Pre-build per-user rows (including per-user course query) */
    user_row_t* rows = NULL;
    if (count > 0) {
        rows = (user_row_t*)heap_caps_malloc(count * sizeof(user_row_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!rows) count = 0;
    }

    for (int i = 0; i < count; i++) {
        rows[i].u = users[i];

        /* Fetch courses for this user – still no LVGL mutex */
        char**  courses      = NULL;
        int     course_count = 0;
        strcpy(rows[i].courses_text, "Courses: None");
        if (db_get_user_courses(users[i].id, &courses, &course_count) == ESP_OK
                && courses && course_count > 0) {
            strcpy(rows[i].courses_text, "Courses: ");
            for (int c = 0; c < course_count; c++) {
                strncat(rows[i].courses_text, courses[c],
                        sizeof(rows[i].courses_text) - strlen(rows[i].courses_text) - 2);
                if (c < course_count - 1)
                    strncat(rows[i].courses_text, ", ",
                            sizeof(rows[i].courses_text) - strlen(rows[i].courses_text) - 1);
                free(courses[c]);
            }
            free(courses);
        }
    }
    if (users) { free(users); users = NULL; }

    /* ---- Step 2: acquire LVGL mutex only to paint widgets ---- */
    if (!ui_acquire()) {
        ESP_LOGE(TAG, "populate_task: failed to acquire LVGL mutex");
        if (rows) free(rows);
        vTaskDelete(NULL);
        return;
    }

    /* Guard: user may have closed the screen while we were fetching */
    if (!s_user_list || !s_user_screen) {
        ui_release();
        if (rows) free(rows);
        vTaskDelete(NULL);
        return;
    }

    /* Remove the loading spinner / label */
    lv_obj_clean(s_user_list);

    if (count > 0 && rows) {
        for (int i = 0; i < count; i++) {
            user_row_t* r = &rows[i];

            /* [BUG-1] Create the top-level card first so we can roll it back
             * with lv_obj_delete(item) if ANY child allocation fails below.
             * Without this, partial widget trees cause Store Access Faults in
             * lv_draw_add_task when LVGL tries to render the dangling objects. */
            lv_obj_t* item = ui_create_card(s_user_list, DISPLAY_WIDTH - 60, 110);
            if (!item) {
                ESP_LOGE(TAG, "populate_task: item card alloc failed for user %d", i);
                continue; /* item doesn't exist, nothing to clean up */
            }
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_hor(item, 20, 0);
            lv_obj_set_style_pad_ver(item, 10, 0);

            /* Streamlined Info Column - vertical flex flow */
            lv_obj_t* info_col = lv_obj_create(item);
            if (!info_col) {
                ESP_LOGE(TAG, "info_col alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            lv_obj_set_size(info_col, DISPLAY_WIDTH - 200, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(info_col, 0, 0);
            lv_obj_set_style_border_width(info_col, 0, 0);
            lv_obj_set_style_pad_all(info_col, 0, 0);
            lv_obj_set_flex_flow(info_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(info_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

            /* 1. Name & Role */
            lv_obj_t* name = lv_label_create(info_col);
            if (!name) {
                ESP_LOGE(TAG, "name label alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            char name_buf[128];
            char role_upper[16];
            snprintf(role_upper, sizeof(role_upper), "%s", r->u.role);
            if (role_upper[0] >= 'a' && role_upper[0] <= 'z') role_upper[0] -= 32;
            snprintf(name_buf, sizeof(name_buf), "%s  (%s)", r->u.name, role_upper);
            lv_label_set_text(name, name_buf);
            lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(name, ui_theme_get_text_color(), 0);

            /* 2. Details (ID / Phone / Telegram) */
            lv_obj_t* details = lv_label_create(info_col);
            if (!details) {
                ESP_LOGE(TAG, "details label alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            char details_text[128] = {0};
            if (strcmp(r->u.role, "student") == 0) {
                snprintf(details_text, sizeof(details_text), "ID: %s  |  Phone: %s",
                         r->u.student_id[0]    ? r->u.student_id    : "N/A",
                         r->u.phone_number[0]  ? r->u.phone_number  : "N/A");
            } else {
                snprintf(details_text, sizeof(details_text), "Phone: %s  |  Telegram ID: %s",
                         r->u.phone_number[0]  ? r->u.phone_number  : "N/A",
                         r->u.telegram_id[0]   ? r->u.telegram_id   : "Not Linked");
            }
            lv_label_set_text(details, details_text);
            lv_obj_set_style_text_font(details, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(details, ui_theme_get_text_muted_color(), 0);
            lv_obj_set_style_margin_top(details, 2, 0);

            /* 3. Courses */
            lv_obj_t* course_lbl = lv_label_create(info_col);
            if (!course_lbl) {
                ESP_LOGE(TAG, "course_lbl alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            lv_label_set_text(course_lbl, r->courses_text);
            lv_obj_set_style_text_font(course_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(course_lbl, lv_color_hex(0x3B82F6), 0);
            lv_obj_set_style_margin_top(course_lbl, 2, 0);

            /* Actions column */
            lv_obj_t* actions_row = lv_obj_create(item);
            if (!actions_row) {
                ESP_LOGE(TAG, "actions_row alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            lv_obj_set_size(actions_row, 100, 45);
            lv_obj_set_style_bg_opa(actions_row, 0, 0);
            lv_obj_set_style_border_width(actions_row, 0, 0);
            lv_obj_set_style_pad_all(actions_row, 0, 0);
            lv_obj_set_flex_flow(actions_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(actions_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(actions_row, LV_OBJ_FLAG_SCROLLABLE);

            /* Re-enroll button (students and lecturers only) */
            if (strcmp(r->u.role, "admin") != 0) {
                lv_obj_t* reen_btn = lv_btn_create(actions_row);
                if (reen_btn) {
                    lv_obj_set_size(reen_btn, 45, 45);
                    lv_obj_set_style_bg_color(reen_btn, lv_color_hex(0xFF9900), 0);
                    lv_obj_set_user_data(reen_btn, (void*)(uintptr_t)r->u.id);
                    lv_obj_add_event_cb(reen_btn, reenroll_user_event_handler, LV_EVENT_CLICKED, NULL);
                    lv_obj_t* reen_icon = lv_label_create(reen_btn);
                    if (reen_icon) {
                        lv_label_set_text(reen_icon, LV_SYMBOL_REFRESH);
                        lv_obj_center(reen_icon);
                    }
                }
            }

            /* Delete button */
            lv_obj_t* del_btn = lv_btn_create(actions_row);
            if (!del_btn) {
                ESP_LOGE(TAG, "del_btn alloc failed — rolling back card");
                lv_obj_delete(item); continue;
            }
            lv_obj_set_size(del_btn, 45, 45);
            lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xCC3333), 0);
            lv_obj_set_user_data(del_btn, (void*)(uintptr_t)r->u.id);
            lv_obj_add_event_cb(del_btn, delete_user_event_handler, LV_EVENT_CLICKED, NULL);
            lv_obj_t* del_icon = lv_label_create(del_btn);
            if (del_icon) {
                lv_label_set_text(del_icon, LV_SYMBOL_TRASH);
                lv_obj_center(del_icon);
            }
        }
    } else {
        lv_obj_t* empty_label = lv_label_create(s_user_list);
        if (empty_label) {
            lv_label_set_text(empty_label, "No users enrolled.");
            lv_obj_set_style_text_color(empty_label, ui_theme_get_text_muted_color(), 0);
        }
    }

    ui_release();
    if (rows) free(rows);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Background action tasks (BUG-3: ABBA deadlock fix)                  */
/*  These run WITHOUT s_lvgl_mux.  They acquire it ONLY for UI updates. */
/* ------------------------------------------------------------------ */

static void delete_user_task(void* arg) {
    delete_ctx_t* ctx = (delete_ctx_t*)arg;
    uint32_t uid = ctx->user_id;
    free(ctx);

    ESP_LOGI(TAG, "[BG] Deleting user ID: %u", (unsigned int)uid);
    bool ok = (db_delete_user(uid) == ESP_OK);
    if (ok) recognizer_load_cache();

    if (ui_acquire()) {
        if (ok) {
            ui_show_notification(NOTIFY_SUCCESS, "User Management", "User deleted successfully", 2000);
            /* Reload the list to reflect the deletion */
            if (s_user_list && s_user_screen && lv_obj_is_valid(s_user_screen)) {
                lv_obj_clean(s_user_list);
            }
        } else {
            ui_show_notification(NOTIFY_ERROR, "User Management", "Failed to delete user", 2000);
        }
        ui_release();
    }

    /* Re-populate the list after deletion if screen is still open */
    if (ok && s_user_screen && lv_obj_is_valid(s_user_screen)) {
        xTaskCreate(user_mgr_populate_task, "usr_mgr_pop", 8192, NULL, 3, NULL);
    }

    vTaskDelete(NULL);
}

static void reenroll_user_task(void* arg) {
    reenroll_ctx_t* ctx = (reenroll_ctx_t*)arg;
    uint32_t uid = ctx->user_id;
    free(ctx);

    ESP_LOGI(TAG, "[BG] Re-enrolling user ID: %u", (unsigned int)uid);

    user_t user;
    bool fetch_ok = (db_get_user_by_id(uid, &user) == ESP_OK);
    bool del_ok   = fetch_ok && (db_delete_user(uid) == ESP_OK);

    if (del_ok) {
        recognizer_load_cache();
        ble_registration_add_pending_student(user.name, user.student_id, user.role, user.phone_number);
    }

    if (ui_acquire()) {
        if (del_ok) {
            ui_show_notification(NOTIFY_SUCCESS, "Re-enrollment", "Student queued. Opening Enrollment screen...", 2500);
            ui_close_user_manager();
            ui_show_enrollment_screen();
        } else if (!fetch_ok) {
            ui_show_notification(NOTIFY_ERROR, "Re-enrollment", "Failed to load user details", 2000);
        } else {
            ui_show_notification(NOTIFY_ERROR, "Re-enrollment", "Failed to delete existing user", 2000);
        }
        ui_release();
    }
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  Event handlers — extract user_id, spawn task, return immediately    */
/*  NO DB calls here (s_lvgl_mux is held by the LVGL event loop).      */
/* ------------------------------------------------------------------ */

static void delete_user_event_handler(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uint32_t user_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    delete_ctx_t* ctx = (delete_ctx_t*)malloc(sizeof(delete_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "delete_user_event_handler: malloc failed");
        return;
    }
    ctx->user_id = user_id;
    if (xTaskCreate(delete_user_task, "del_user", 4096, ctx, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "delete_user_event_handler: task create failed");
        free(ctx);
    }
    /* Return immediately — DB work happens in delete_user_task */
}

static void reenroll_user_event_handler(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uint32_t user_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    reenroll_ctx_t* ctx = (reenroll_ctx_t*)malloc(sizeof(reenroll_ctx_t));
    if (!ctx) {
        ESP_LOGE(TAG, "reenroll_user_event_handler: malloc failed");
        return;
    }
    ctx->user_id = user_id;
    if (xTaskCreate(reenroll_user_task, "reenroll_user", 6144, ctx, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "reenroll_user_event_handler: task create failed");
        free(ctx);
    }
    /* Return immediately — DB work happens in reenroll_user_task */
}

static void close_btn_event_handler(lv_event_t* e) {
    ui_close_user_manager();
}

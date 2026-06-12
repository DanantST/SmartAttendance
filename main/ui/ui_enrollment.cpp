#include "ui_main.h"
/**
 * @file ui_enrollment.cpp
 * @brief BLE Multi-Student Enrollment Screen implementation
 */

#include "ui_enrollment.h"
#include "ui_theme.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"
#include <string.h>
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "UI_ENROLL";

extern "C" {
    extern volatile bool g_enrollment_cancel;
    void start_single_capture_task(void *pvParam);
}

/* ─── UI Objects ───────────────────────────────────────────────────────── */
static lv_obj_t* s_enroll_screen = NULL;

/* Left Panel: Student Queue */
static lv_obj_t* s_list_panel = NULL;
static lv_obj_t* s_student_list = NULL;
static lv_obj_t* s_student_cards[MAX_PENDING_STUDENTS] = {NULL};

/* Right Panel: Camera & Feedback */
static lv_obj_t* s_right_panel = NULL;
static lv_obj_t* s_camera_container = NULL;
static lv_obj_t* s_camera_preview = NULL;
static lv_draw_buf_t s_camera_img_dsc;
static uint8_t* s_camera_buffer = NULL;

static lv_obj_t* s_progress_bar = NULL;
static lv_obj_t* s_progress_label = NULL;
static lv_obj_t* s_guidance_label = NULL;
static lv_obj_t* s_success_popup = NULL;
static lv_obj_t* s_success_label = NULL;
static lv_obj_t* s_status_label = NULL;
static lv_obj_t* s_close_btn = NULL;
static lv_timer_t* s_success_timer = NULL; /* tracked to allow cancellation on close */

/* ─── State ────────────────────────────────────────────────────────────── */
typedef enum {
    ENROLL_SCREEN_IDLE,
    ENROLL_SCREEN_CAPTURING,
    ENROLL_SCREEN_SUCCESS
} enroll_screen_state_t;

static enroll_screen_state_t s_state = ENROLL_SCREEN_IDLE;
static int s_current_student_idx = -1;

/* ─── Forward Declarations ─────────────────────────────────────────────── */
static void create_enrollment_screen(void);
static void close_btn_event_handler(lv_event_t* e);
static void student_card_event_handler(lv_event_t* e);
static void popup_timer_cb(lv_timer_t* timer);

/* ─── Lifecycle ────────────────────────────────────────────────────────── */

void ui_show_enrollment_screen(void) {
    ESP_LOGI(TAG, "Showing multi-student enrollment screen");
    if (s_enroll_screen) {
        s_state = ENROLL_SCREEN_IDLE;
        s_current_student_idx = -1;
        ui_enrollment_set_camera_active(false);
        ui_show_pose_guidance("Tap a student to begin enrollment");
        lv_scr_load(s_enroll_screen);
        return;
    }

    s_state = ENROLL_SCREEN_IDLE;
    s_current_student_idx = -1;

    create_enrollment_screen();
    
    ui_enrollment_set_camera_active(false);
    ui_show_pose_guidance("Tap a student to begin enrollment");
}

void ui_close_enrollment_screen(void) {
    if (!s_enroll_screen) return;
    ESP_LOGI(TAG, "Closing enrollment screen");

    /* Signal background capture/enrollment task to stop */
    g_enrollment_cancel = true;

    /* Cancel dangling popup timer before deleting its target objects */
    if (s_success_timer) {
        lv_timer_del(s_success_timer);
        s_success_timer = NULL;
    }

    /* Free the PSRAM camera buffer (allocated in create_enrollment_screen) */
    if (s_camera_buffer) {
        heap_caps_free(s_camera_buffer);
        s_camera_buffer = NULL;
    }

    /* Null all student card slots — pointers become dangling after lv_obj_del */
    for (int i = 0; i < MAX_PENDING_STUDENTS; i++) {
        s_student_cards[i] = NULL;
    }

    lv_obj_del(s_enroll_screen);

    /* Null all widget handles so create_enrollment_screen starts fresh next time */
    s_enroll_screen    = NULL;
    s_list_panel       = NULL;
    s_student_list     = NULL;
    s_right_panel      = NULL;
    s_camera_container = NULL;
    s_camera_preview   = NULL;
    s_progress_bar     = NULL;
    s_progress_label   = NULL;
    s_guidance_label   = NULL;
    s_success_popup    = NULL;
    s_success_label    = NULL;
    s_status_label     = NULL;
    s_close_btn        = NULL;

    s_state               = ENROLL_SCREEN_IDLE;
    s_current_student_idx = -1;
}

/* ─── UI Creation ──────────────────────────────────────────────────────── */

static void create_enrollment_screen(void) {
    s_enroll_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_enroll_screen);
    lv_obj_set_style_bg_color(s_enroll_screen, lv_color_hex(0x121212), 0);
    lv_scr_load(s_enroll_screen);

    /* Close Button */
    s_close_btn = lv_btn_create(s_enroll_screen);
    lv_obj_set_size(s_close_btn, 40, 40);
    lv_obj_set_pos(s_close_btn, DISPLAY_WIDTH - 50, 45);
    lv_obj_set_style_bg_color(s_close_btn, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_radius(s_close_btn, 20, 0);
    lv_obj_t* close_label = lv_label_create(s_close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_add_event_cb(s_close_btn, close_btn_event_handler, LV_EVENT_CLICKED, NULL);

    /* Title */
    lv_obj_t* title = lv_label_create(s_enroll_screen);
    lv_label_set_text(title, "WEB AP Remote Enrollment");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 20, 50);

    /* Left Panel (Student List) */
    s_list_panel = lv_obj_create(s_enroll_screen);
    lv_obj_set_size(s_list_panel, 260, 440);
    lv_obj_set_pos(s_list_panel, 20, 90);
    lv_obj_set_style_bg_color(s_list_panel, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_list_panel, 0, 0);
    
    lv_obj_t* list_title = lv_label_create(s_list_panel);
    lv_label_set_text(list_title, "Pending Students");
    lv_obj_set_style_text_color(list_title, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(list_title, LV_ALIGN_TOP_MID, 0, 0);

    s_student_list = lv_obj_create(s_list_panel);
    lv_obj_set_size(s_student_list, 240, 400);
    lv_obj_align(s_student_list, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_bg_opa(s_student_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_student_list, 0, 0);
    lv_obj_set_flex_flow(s_student_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_student_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Right Panel (Camera & Feedback) */
    s_right_panel = lv_obj_create(s_enroll_screen);
    lv_obj_set_size(s_right_panel, 700, 440);
    lv_obj_set_pos(s_right_panel, 300, 90);
    lv_obj_set_style_bg_color(s_right_panel, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_right_panel, 0, 0);

    /* Rectangular Camera Container */
    s_camera_container = lv_obj_create(s_right_panel);
    lv_obj_set_size(s_camera_container, 480, 360);
    lv_obj_align(s_camera_container, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(s_camera_container, 10, 0);
    lv_obj_set_style_clip_corner(s_camera_container, false, 0);
    lv_obj_set_style_bg_color(s_camera_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_camera_container, 6, 0);
    lv_obj_set_style_border_color(s_camera_container, lv_color_hex(0x3A6EA5), 0); /* Default blue */

    s_camera_preview = lv_image_create(s_camera_container);
    lv_obj_set_size(s_camera_preview, 480, 360);
    lv_obj_center(s_camera_preview);

    s_camera_buffer = (uint8_t*)heap_caps_malloc(480 * 360 * 2, MALLOC_CAP_SPIRAM);
    if (s_camera_buffer) {
        memset(s_camera_buffer, 0, 480 * 360 * 2);
        lv_draw_buf_init(&s_camera_img_dsc, 480, 360, LV_COLOR_FORMAT_RGB565, 480 * 2, s_camera_buffer, 480 * 360 * 2);
        s_camera_img_dsc.header.flags |= LV_IMAGE_FLAGS_ALLOCATED;
        lv_image_set_src(s_camera_preview, &s_camera_img_dsc);
    }

    /* Progress Bar (above oval) */
    s_progress_bar = lv_bar_create(s_right_panel);
    lv_obj_set_size(s_progress_bar, 400, 15);
    lv_obj_align(s_progress_bar, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_bar_set_range(s_progress_bar, 0, 20);
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    s_progress_label = lv_label_create(s_right_panel);
    lv_label_set_text(s_progress_label, "");
    lv_obj_set_style_text_color(s_progress_label, lv_color_white(), 0);
    lv_obj_align_to(s_progress_label, s_progress_bar, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* Guidance Label (below oval) */
    s_guidance_label = lv_label_create(s_right_panel);
    lv_label_set_text(s_guidance_label, "Tap a student to begin enrollment");
    lv_obj_set_style_text_color(s_guidance_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_guidance_label, &lv_font_montserrat_16, 0);
    lv_obj_align(s_guidance_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* Status Label (for errors) */
    s_status_label = lv_label_create(s_right_panel);
    lv_label_set_text(s_status_label, "");
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);

    /* Success Popup (Animated) */
    s_success_popup = lv_obj_create(s_right_panel);
    lv_obj_set_size(s_success_popup, 300, 60);
    lv_obj_align(s_success_popup, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_bg_color(s_success_popup, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(s_success_popup, 10, 0);
    lv_obj_set_style_border_width(s_success_popup, 0, 0);
    lv_obj_add_flag(s_success_popup, LV_OBJ_FLAG_HIDDEN);

    s_success_label = lv_label_create(s_success_popup);
    lv_label_set_text(s_success_label, LV_SYMBOL_OK " SUCCESS");
    lv_obj_center(s_success_label);
    lv_obj_set_style_text_color(s_success_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_success_label, &lv_font_montserrat_24, 0);
}

/* ─── Event Handlers ───────────────────────────────────────────────────── */

static void close_btn_event_handler(lv_event_t* e) {
    g_enrollment_cancel = true;
    lv_obj_set_style_bg_color(s_close_btn, lv_color_hex(0xAA4444), 0);
}

static void student_card_event_handler(lv_event_t* e) {
    if (s_state != ENROLL_SCREEN_IDLE) return; /* Ignore if busy */

    lv_obj_t* card = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    ESP_LOGI(TAG, "Selected student index: %d", idx);
    
    /* Highlight card */
    lv_obj_set_style_bg_color(card, lv_color_hex(0x3A6EA5), 0);

    /* Prepare right panel */
    s_state = ENROLL_SCREEN_CAPTURING;
    s_current_student_idx = idx;
    
    ui_enrollment_set_camera_active(true);
    lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_progress_label, "0%");
    ui_show_pose_guidance("Please stand in front of the camera");
    lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);

    /* Start capture task — needs 16 KB: 30x (face_detector + alignment + feature_extractor) call stacks */
    xTaskCreate(start_single_capture_task, "single_capture", 16384, (void*)(intptr_t)idx, 5, NULL);
}

/* ─── Student List API ─────────────────────────────────────────────────── */

void ui_enrollment_add_student(const char* name, const char* student_id) {
    if (!s_student_list) return;

    /* Find first empty slot */
    int idx = -1;
    for (int i = 0; i < MAX_PENDING_STUDENTS; i++) {
        if (s_student_cards[i] == NULL) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return; /* Full */

    lv_obj_t* card = lv_obj_create(s_student_list);
    lv_obj_set_width(card, 220);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, student_card_event_handler, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    lv_obj_t* name_lbl = lv_label_create(card);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);

    lv_obj_t* id_lbl = lv_label_create(card);
    lv_label_set_text_fmt(id_lbl, "ID: %s", student_id);
    lv_obj_set_style_text_color(id_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align_to(id_lbl, name_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    s_student_cards[idx] = card;
}

void ui_enrollment_remove_student_by_index(int idx) {
    if (idx < 0 || idx >= MAX_PENDING_STUDENTS) return;
    if (s_student_cards[idx]) {
        lv_obj_delete(s_student_cards[idx]);
        s_student_cards[idx] = NULL;
    }
}

/* ─── Camera & Feedback API ────────────────────────────────────────────── */

void ui_enrollment_set_face_detected(bool detected) {
    if (!s_camera_container) return;
    if (detected) {
        lv_obj_set_style_border_color(s_camera_container, lv_color_hex(0x4CAF50), 0); /* Green */
        lv_obj_set_style_shadow_color(s_camera_container, lv_color_hex(0x4CAF50), 0);
        lv_obj_set_style_shadow_width(s_camera_container, 20, 0);
        lv_obj_set_style_shadow_opa(s_camera_container, LV_OPA_60, 0);
    } else {
        lv_obj_set_style_border_color(s_camera_container, lv_color_hex(0x3A6EA5), 0); /* Blue */
        lv_obj_set_style_shadow_width(s_camera_container, 0, 0);
    }
}

void ui_enrollment_set_capture_progress(int current, int total) {
    if (!s_progress_bar) return;
    lv_bar_set_value(s_progress_bar, current, LV_ANIM_ON);
    lv_label_set_text_fmt(s_progress_label, "%d%%", (current * 100) / total);
    
    if (current < total / 4) ui_show_pose_guidance("Look straight at the camera");
    else if (current < total / 2) ui_show_pose_guidance("Tilt your head slightly left");
    else if (current < total * 3 / 4) ui_show_pose_guidance("Tilt your head slightly right");
    else ui_show_pose_guidance("Smile!");
}

void ui_show_pose_guidance(const char* message) {
    if (s_guidance_label && message) {
        lv_label_set_text(s_guidance_label, message);
    }
}

void ui_enrollment_show_success(const char* student_name, int student_idx) {
    if (!s_success_popup) return;
    s_state = ENROLL_SCREEN_SUCCESS;
    
    lv_label_set_text_fmt(s_success_label, LV_SYMBOL_OK " %s ENROLLED", student_name);
    lv_obj_clear_flag(s_success_popup, LV_OBJ_FLAG_HIDDEN);
    
    /* Animation: slide down and fade in */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_success_popup);
    lv_anim_set_values(&a, 0, 20);
    lv_anim_set_time(&a, 300);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&a);

    /* Timer to reset UI — store handle so ui_close_enrollment_screen can cancel it */
    s_success_timer = lv_timer_create(popup_timer_cb, 2500, (void*)(intptr_t)student_idx);
    lv_timer_set_repeat_count(s_success_timer, 1); /* one-shot */
}

static void popup_timer_cb(lv_timer_t* timer) {
    int idx = (int)(intptr_t)lv_timer_get_user_data(timer);

    /* Clear the tracked handle first — the timer is self-deleting (repeat_count=1) */
    s_success_timer = NULL;

    /* Guard: screen may have been closed before the timer fired */
    if (!s_enroll_screen) return;

    ui_enrollment_remove_student_by_index(idx);

    if (s_success_popup) lv_obj_add_flag(s_success_popup, LV_OBJ_FLAG_HIDDEN);
    if (s_progress_bar)  lv_obj_add_flag(s_progress_bar,  LV_OBJ_FLAG_HIDDEN);
    ui_enrollment_set_camera_active(false);
    ui_enrollment_set_face_detected(false);
    ui_show_pose_guidance("Tap a student to begin enrollment");

    s_state               = ENROLL_SCREEN_IDLE;
    s_current_student_idx = -1;
}

void ui_enrollment_set_status(bool success, const char* message) {
    if (!s_status_label) return;
    lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_status_label, message);
    lv_obj_set_style_text_color(s_status_label, success ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF4444), 0);
    
    if (!success) {
        s_state = ENROLL_SCREEN_IDLE; /* Allow retry by tapping card again */

        /* Unhighlight the card so the user sees it's selectable again */
        if (s_current_student_idx >= 0 && s_student_cards[s_current_student_idx]) {
            lv_obj_set_style_bg_color(s_student_cards[s_current_student_idx], lv_color_hex(0x2A2A2A), 0);
        }

        /* Add a visible "Try Again" button so the user doesn't have to guess */
        if (s_right_panel) {
            lv_obj_t* retry_btn = lv_btn_create(s_right_panel);
            lv_obj_set_size(retry_btn, 160, 50);
            lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_MID, 0, -40);
            lv_obj_set_style_bg_color(retry_btn, lv_color_hex(0xE67E22), 0);
            lv_obj_set_style_radius(retry_btn, 10, 0);

            lv_obj_t* retry_lbl = lv_label_create(retry_btn);
            lv_label_set_text(retry_lbl, LV_SYMBOL_REFRESH "  Try Again");
            lv_obj_set_style_text_color(retry_lbl, lv_color_white(), 0);
            lv_obj_center(retry_lbl);

            /* Store card index in button user-data so the lambda can re-trigger */
            lv_obj_set_user_data(retry_btn, (void*)(intptr_t)s_current_student_idx);

            lv_obj_add_event_cb(retry_btn, [](lv_event_t* ev) {
                /* Remove button before re-triggering so it doesn't accumulate */
                lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(ev);
                int card_idx  = (int)(intptr_t)lv_obj_get_user_data(btn);
                lv_obj_delete(btn);

                /* Re-highlight the card and kick off another capture */
                if (card_idx >= 0 && card_idx < MAX_PENDING_STUDENTS
                        && s_student_cards[card_idx]) {
                    lv_obj_set_style_bg_color(s_student_cards[card_idx],
                                              lv_color_hex(0x3A6EA5), 0);
                }

                s_state = ENROLL_SCREEN_CAPTURING;
                s_current_student_idx = card_idx;
                ui_enrollment_set_camera_active(true);
                if (s_progress_bar) {
                    lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
                    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
                }
                if (s_progress_label) lv_label_set_text(s_progress_label, "0%");
                if (s_status_label)   lv_obj_add_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
                ui_show_pose_guidance("Please stand in front of the camera");

                xTaskCreate(start_single_capture_task, "enroll_retry",
                            16384, (void*)(intptr_t)card_idx, 5, NULL);
            }, LV_EVENT_CLICKED, NULL);
        }
    }
}

void ui_enrollment_set_camera_active(bool active) {
    if (active) {
        lv_obj_clear_flag(s_camera_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_camera_container, LV_OBJ_FLAG_HIDDEN);
        if (s_camera_buffer) {
            memset(s_camera_buffer, 0, 480 * 360 * 2);
            lv_obj_invalidate(s_camera_preview);
        }
    }
}

extern "C" void ui_update_enrollment_camera_frame(uint8_t* img, int width, int height) {
    if (!s_camera_buffer || !img || !s_enroll_screen || s_state != ENROLL_SCREEN_CAPTURING) return;

    if (!ui_acquire()) return;
    if (s_enroll_screen && s_camera_buffer && s_camera_preview &&
        !lv_obj_has_flag(s_camera_container, LV_OBJ_FLAG_HIDDEN)) {
        
        int dst_w = s_camera_img_dsc.header.w;
        int dst_h = s_camera_img_dsc.header.h;

        if (width == dst_w && height == dst_h) {
            memcpy(s_camera_buffer, img, (size_t)dst_w * dst_h * 2);
        } else {
            uint16_t* src = (uint16_t*)img;
            uint16_t* dst = (uint16_t*)s_camera_buffer;
            for (int y = 0; y < dst_h; y++) {
                int src_y = (y * height) / dst_h;
                uint16_t* src_row = src + src_y * width;
                uint16_t* dst_row = dst + y * dst_w;
                for (int x = 0; x < dst_w; x++) {
                    dst_row[x] = src_row[(x * width) / dst_w];
                }
            }
        }
        lv_obj_invalidate(s_camera_preview);
    }
    ui_release();
}

/* Legacy aliases */
void ui_update_enrollment_progress(int current, int total) {
    ui_enrollment_set_capture_progress(current, total);
}
void ui_update_enrollment_frame_captured(float quality) {
    /* Handled by border color now */
}

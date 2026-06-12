/**
 * @file ui_attendance.cpp
 * @brief Attendance scanner application screen
 */

#include "ui_attendance.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char* TAG = "UI_ATTEN";

static lv_obj_t* s_attendance_screen = NULL;
static lv_obj_t* s_camera_container = NULL;
static lv_obj_t* s_camera_image = NULL;
static lv_obj_t* s_detection_overlay = NULL;
static lv_obj_t* s_recognition_label = NULL;
static lv_obj_t* s_attendance_label = NULL;

static lv_draw_buf_t s_camera_img_dsc;
static uint8_t* s_camera_buffer = NULL;

extern "C" void ui_show_attendance_screen(void) {
    ESP_LOGI(TAG, "Opening Attendance Scanner");
    if (s_attendance_screen) {
        lv_scr_load(s_attendance_screen);
        return;
    }

    s_attendance_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_attendance_screen);
    lv_obj_set_style_bg_color(s_attendance_screen, lv_color_hex(0x121212), 0);

    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_attendance_screen);
    lv_obj_set_size(title_bar, 1024, 50);
    lv_obj_set_pos(title_bar, 0, 40); /* Below status bar */
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Attendance Scanner");
    lv_obj_set_pos(title_label, 20, 12);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);

    /* Attendance count label */
    s_attendance_label = lv_label_create(s_attendance_screen);
    lv_obj_set_pos(s_attendance_label, 20, 100);
    lv_label_set_text(s_attendance_label, "Today: 0 present");
    lv_obj_set_style_text_color(s_attendance_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_attendance_label, &lv_font_montserrat_14, 0);

    /* Create camera preview area (centered) */
    int preview_width = 480;
    int preview_height = 360;
    int preview_x = (1024 - preview_width) / 2;
    int preview_y = 130;
    
    s_camera_container = lv_obj_create(s_attendance_screen);
    lv_obj_set_size(s_camera_container, preview_width, preview_height);
    lv_obj_set_pos(s_camera_container, preview_x, preview_y);
    lv_obj_set_style_bg_color(s_camera_container, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_camera_container, 2, 0);
    lv_obj_set_style_border_color(s_camera_container, lv_color_hex(0x3A6EA5), 0);
    lv_obj_set_style_radius(s_camera_container, 10, 0);
    lv_obj_set_style_pad_all(s_camera_container, 0, 0);
    
    s_camera_image = lv_image_create(s_camera_container);
    lv_obj_set_size(s_camera_image, preview_width, preview_height);
    lv_obj_set_pos(s_camera_image, 0, 0);
    
    s_detection_overlay = lv_obj_create(s_camera_container);
    lv_obj_set_size(s_detection_overlay, preview_width, preview_height);
    lv_obj_set_pos(s_detection_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_detection_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_detection_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_detection_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_detection_overlay, 0, 0);
    
    /* Allocate camera buffer */
    s_camera_buffer = (uint8_t*)heap_caps_malloc(preview_width * preview_height * 2, MALLOC_CAP_SPIRAM);
    if (s_camera_buffer) {
        lv_draw_buf_init(&s_camera_img_dsc, preview_width, preview_height, LV_COLOR_FORMAT_RGB565, preview_width * 2, s_camera_buffer, preview_width * preview_height * 2);
        s_camera_img_dsc.header.flags |= LV_IMAGE_FLAGS_ALLOCATED;
        lv_image_set_src(s_camera_image, &s_camera_img_dsc);
    }
    
    s_recognition_label = lv_label_create(s_camera_container);
    lv_obj_set_pos(s_recognition_label, 10, preview_height - 40);
    lv_obj_set_style_bg_color(s_recognition_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_recognition_label, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(s_recognition_label, 5, 0);
    lv_obj_set_style_radius(s_recognition_label, 5, 0);
    lv_label_set_text(s_recognition_label, "Scanning...");
    lv_obj_set_style_text_color(s_recognition_label, lv_color_white(), 0);

    /* Load screen directly (keep main screen in memory) */
    lv_scr_load(s_attendance_screen);
}

extern "C" void ui_close_attendance_screen(void) {
    if (!s_attendance_screen) return;
    ESP_LOGI(TAG, "Closing Attendance Scanner");
    /* Do not free buffer or null pointers; keep app in memory */

    ui_return_to_main();
}

/* We also need to update the global update functions to use these pointers */
extern "C" void ui_update_camera_frame(uint8_t* img, int width, int height) {
    if (!s_camera_buffer || !img || !s_attendance_screen) return;
    
    if (!ui_acquire()) return;
    if (s_attendance_screen && s_camera_buffer && s_camera_image) {
        int dst_w = s_camera_img_dsc.header.w;
        int dst_h = s_camera_img_dsc.header.h;
        
        if (width == dst_w && height == dst_h) {
            memcpy(s_camera_buffer, img, dst_w * dst_h * 2);
        } else {
            /* Software nearest-neighbor scaling for RGB565 */
            uint16_t* src = (uint16_t*)img;
            uint16_t* dst = (uint16_t*)s_camera_buffer;
            
            for (int y = 0; y < dst_h; y++) {
                int src_y = (y * height) / dst_h;
                uint16_t* src_row = src + src_y * width;
                uint16_t* dst_row = dst + y * dst_w;
                for (int x = 0; x < dst_w; x++) {
                    int src_x = (x * width) / dst_w;
                    dst_row[x] = src_row[src_x];
                }
            }
        }
        lv_obj_invalidate(s_camera_image);
    }
    ui_release();
}

extern "C" void ui_show_recognition_result(const char* name, float confidence) {
    if (!ui_acquire()) return;
    if (!s_recognition_label || !s_attendance_screen) {
        ui_release();
        return;
    }
    char buf[64];
    bool matched = (confidence >= 0.5f); // Threshold for visual feedback color
    if (matched) {
        snprintf(buf, sizeof(buf), "Match: %s (%.1f%%)", name, confidence * 100.0f);
        lv_obj_set_style_text_color(s_recognition_label, lv_color_hex(0x44FF44), 0);
    } else {
        snprintf(buf, sizeof(buf), "Unknown (%.1f%%)", confidence * 100.0f);
        lv_obj_set_style_text_color(s_recognition_label, lv_color_hex(0xFF4444), 0);
    }
    lv_label_set_text(s_recognition_label, buf);
    ui_release();
}

extern "C" void ui_update_detection_bounding_box(int x, int y, int w, int h, bool detected) {
    if (!ui_acquire()) return;
    if (!s_detection_overlay || !s_attendance_screen) {
        ui_release();
        return;
    }
    lv_obj_clean(s_detection_overlay);
    if (detected && w > 0 && h > 0) {
        lv_obj_t* box = lv_obj_create(s_detection_overlay);
        lv_obj_set_pos(box, x, y);
        lv_obj_set_size(box, w, h);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(0x44FF44), 0);
        lv_obj_set_style_border_width(box, 2, 0);
        lv_obj_set_style_radius(box, 0, 0);
    }
    ui_release();
}

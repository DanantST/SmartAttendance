#include "ui_admin_setup.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "lvgl.h"
#include "esp_log.h"
#include "ble/ble_registration.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "UI_ADMIN_SETUP";

static lv_obj_t* s_admin_setup_screen = NULL;
static lv_obj_t* s_name_ta = NULL;
static lv_obj_t* s_matric_ta = NULL;
static lv_obj_t* s_phone_ta = NULL;

extern "C" {
    extern volatile char g_enrollment_role_override[16];
    extern void start_enrollment_task(void *pvParam);
}

static void close_btn_event_handler(lv_event_t* e) {
    if (!s_admin_setup_screen) return;
    lv_obj_delete_async(s_admin_setup_screen);
    s_admin_setup_screen = NULL;
    s_name_ta = NULL;
    s_matric_ta = NULL;
    s_phone_ta = NULL;
    ui_show_settings_screen();
}

static void start_capture_btn_event_handler(lv_event_t* e) {
    if (!s_name_ta || !s_matric_ta || !s_phone_ta) return;
    
    const char* name = lv_textarea_get_text(s_name_ta);
    const char* matric = lv_textarea_get_text(s_matric_ta);
    const char* phone = lv_textarea_get_text(s_phone_ta);
    
    if (strlen(name) == 0 || strlen(matric) == 0 || strlen(phone) == 0) {
        ui_show_notification(NOTIFY_WARNING, "Form Incomplete", "Please fill out all fields", 2000);
        return;
    }
    
    // Set admin role override
    strncpy((char*)g_enrollment_role_override, "admin", sizeof(g_enrollment_role_override) - 1);
    
    // Add to pending queue (role is admin)
    esp_err_t err = ble_registration_add_pending_student(name, matric, "admin", phone);
    if (err != ESP_OK) {
        ui_show_notification(NOTIFY_ERROR, "Queue Error", "Failed to queue admin registration", 2000);
        return;
    }
    
    // Switch to enrollment screen and launch enrollment task
    lv_obj_delete_async(s_admin_setup_screen);
    s_admin_setup_screen = NULL;
    s_name_ta = NULL;
    s_matric_ta = NULL;
    s_phone_ta = NULL;
    
    xTaskCreate(start_enrollment_task, "enrollment", 8192, NULL, 5, NULL);
}

static void textarea_click_event_cb(lv_event_t* e) {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    const char* title = (const char*)lv_event_get_user_data(e);
    ui_show_keyboard(ta, title);
}

void ui_show_admin_setup_wizard(void) {
    ESP_LOGI(TAG, "Showing Admin Setup Wizard...");
    if (s_admin_setup_screen) return;
    
    s_admin_setup_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_admin_setup_screen);
    lv_obj_set_style_bg_color(s_admin_setup_screen, lv_color_hex(0x121212), 0);
    lv_scr_load(s_admin_setup_screen);
    
    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_admin_setup_screen);
    lv_obj_set_size(title_bar, DISPLAY_WIDTH, 50);
    lv_obj_set_pos(title_bar, 0, 40);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    
    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Admin Face Enrollment Wizard");
    lv_obj_set_pos(title_label, 20, 12);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    
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
    lv_obj_add_event_cb(close_btn, close_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    /* Content panel */
    lv_obj_t* card = ui_create_card(s_admin_setup_screen, DISPLAY_WIDTH - 100, 440);
    lv_obj_set_pos(card, 50, 95);
    
    lv_obj_t* desc = lv_label_create(card);
    lv_label_set_text(desc, "Complete this form to set up the device Administrator.\nAfter tapping 'Start Face Capture', you will be redirected to the face scanner.");
    lv_obj_set_pos(desc, 20, 20);
    lv_obj_set_style_text_color(desc, lv_color_hex(0xCCCCCC), 0);
    
    /* Name Field */
    lv_obj_t* name_label = lv_label_create(card);
    lv_label_set_text(name_label, "Full Name");
    lv_obj_set_pos(name_label, 20, 70);
    
    s_name_ta = lv_textarea_create(card);
    lv_obj_set_size(s_name_ta, DISPLAY_WIDTH - 200, 40);
    lv_obj_set_pos(s_name_ta, 20, 95);
    lv_textarea_set_one_line(s_name_ta, true);
    lv_obj_add_event_cb(s_name_ta, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Admin Full Name");
    
    /* Matric Field */
    lv_obj_t* matric_label = lv_label_create(card);
    lv_label_set_text(matric_label, "Matric / Admin ID");
    lv_obj_set_pos(matric_label, 20, 145);
    
    s_matric_ta = lv_textarea_create(card);
    lv_obj_set_size(s_matric_ta, DISPLAY_WIDTH - 200, 40);
    lv_obj_set_pos(s_matric_ta, 20, 170);
    lv_textarea_set_one_line(s_matric_ta, true);
    lv_obj_add_event_cb(s_matric_ta, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Admin Matric ID");
    
    /* Phone Field */
    lv_obj_t* phone_label = lv_label_create(card);
    lv_label_set_text(phone_label, "Phone Number (Telegram-linked)");
    lv_obj_set_pos(phone_label, 20, 220);
    
    s_phone_ta = lv_textarea_create(card);
    lv_obj_set_size(s_phone_ta, DISPLAY_WIDTH - 200, 40);
    lv_obj_set_pos(s_phone_ta, 20, 245);
    lv_textarea_set_one_line(s_phone_ta, true);
    lv_obj_add_event_cb(s_phone_ta, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Admin Phone Number");
    
    /* Submit button */
    lv_obj_t* start_btn = ui_create_button(card, "Start Face Capture", 240, 40);
    lv_obj_set_pos(start_btn, 20, 305);
    lv_obj_add_event_cb(start_btn, start_capture_btn_event_handler, LV_EVENT_CLICKED, NULL);
}

/**
 * @file ui_setup_wizard.cpp
 * @brief First-time setup wizard implementation
 */

#include "ui_setup_wizard.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "lvgl.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "network/wifi_manager.h"
#include "config.h"
#include <cstring>

static const char* TAG = "UI_SETUP";

static lv_obj_t* s_setup_screen = NULL;
static lv_obj_t* s_steps_container = NULL;
static int s_current_step = 0;

/* Active text input tracking */
static lv_obj_t* s_wifi_ssid_ta = NULL;
static lv_obj_t* s_wifi_pass_ta = NULL;
static lv_obj_t* s_pin_ta = NULL;

extern "C" {
    extern volatile char g_enrollment_role_override[16];
    extern volatile bool g_wizard_admin_enrolled;
    extern void start_enrollment_task(void *pvParam);
}

/* Wizard pages */
static lv_obj_t* create_welcome_page(lv_obj_t* parent);
static lv_obj_t* create_wifi_page(lv_obj_t* parent);
static lv_obj_t* create_admin_page(lv_obj_t* parent);
static lv_obj_t* create_pin_page(lv_obj_t* parent);

static void next_btn_event_handler(lv_event_t* e);
static void skip_wifi_event_handler(lv_event_t* e);

void ui_show_setup_wizard(void) {
    ESP_LOGI(TAG, "Showing setup wizard...");
    if (s_setup_screen) return;
    
    s_setup_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_setup_screen);
    lv_obj_set_style_bg_color(s_setup_screen, lv_color_hex(0x0A0A0A), 0);
    
    s_steps_container = lv_obj_create(s_setup_screen);
    lv_obj_set_size(s_steps_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_opa(s_steps_container, 0, 0);
    lv_obj_set_style_border_width(s_steps_container, 0, 0);
    
    s_current_step = 0;
    create_welcome_page(s_steps_container);
    
    lv_scr_load(s_setup_screen);
}

static lv_obj_t* create_welcome_page(lv_obj_t* parent) {
    
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Welcome to SmartAttendance");
    /* Use default font instead of Montserrat for debug */
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00A8FF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);
    
    /* DEBUG: Add a big red circle to verify object rendering */
    lv_obj_t* circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 100, 100);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xFF0000), 0);
    lv_obj_align(circle, LV_ALIGN_TOP_LEFT, 20, 20);
    
    lv_obj_t* desc = lv_label_create(parent);
    lv_label_set_text(desc, "Let's set up your device for the first time.\\n"
                            "This process will ensure your device is secure\\n"
                            "and ready for attendance tracking.");
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_t* next_btn = ui_create_button(parent, "Get Started", 200, 50);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_add_event_cb(next_btn, next_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    return parent;
}

static lv_obj_t* create_wifi_page(lv_obj_t* parent) {
    
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Step 1: Network Connection");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    lv_obj_t* desc = lv_label_create(parent);
    lv_label_set_text(desc, "Connect to Wi-Fi to enable remote sync\\n"
                            "and automatic time updates.");
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -40);
    
    /* Simplified Wi-Fi fields for setup */
    s_wifi_ssid_ta = lv_textarea_create(parent);
    lv_obj_set_size(s_wifi_ssid_ta, 300, 40);
    lv_obj_align(s_wifi_ssid_ta, LV_ALIGN_CENTER, 0, 20);
    lv_textarea_set_placeholder_text(s_wifi_ssid_ta, "SSID (Network Name)");
    lv_obj_add_event_cb(s_wifi_ssid_ta, [](lv_event_t* e) {
        ui_show_keyboard(s_wifi_ssid_ta, "SSID");
    }, LV_EVENT_CLICKED, NULL);
    
    s_wifi_pass_ta = lv_textarea_create(parent);
    lv_obj_set_size(s_wifi_pass_ta, 300, 40);
    lv_obj_align(s_wifi_pass_ta, LV_ALIGN_CENTER, 0, 70);
    lv_textarea_set_placeholder_text(s_wifi_pass_ta, "Password");
    lv_textarea_set_password_mode(s_wifi_pass_ta, true);
    lv_obj_add_event_cb(s_wifi_pass_ta, [](lv_event_t* e) {
        ui_show_keyboard(s_wifi_pass_ta, "Password");
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* next_btn = ui_create_button(parent, "Connect & Next", 180, 50);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_MID, 100, -40);
    lv_obj_add_event_cb(next_btn, next_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* skip_btn = ui_create_button(parent, "Skip", 120, 50);
    lv_obj_set_style_bg_color(skip_btn, lv_color_hex(0x444444), 0);
    lv_obj_align(skip_btn, LV_ALIGN_BOTTOM_MID, -100, -40);
    lv_obj_add_event_cb(skip_btn, skip_wifi_event_handler, LV_EVENT_CLICKED, NULL);
    
    return parent;
}

static lv_obj_t* s_admin_next_btn = NULL;

static void admin_enroll_event_handler(lv_event_t* e) {
    g_wizard_admin_enrolled = false;  /* clear any stale flag */
    strncpy((char*)g_enrollment_role_override, "admin", sizeof(g_enrollment_role_override) - 1);
    xTaskCreate(start_enrollment_task, "enrollment", 8192, NULL, 5, NULL);

    /* Disable "Next" until enrollment task signals completion */
    if (s_admin_next_btn) {
        lv_obj_add_state(s_admin_next_btn, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(s_admin_next_btn, 0), "Enrolling...");
    }

    /* Poll for completion — re-enable Next when task sets g_wizard_admin_enrolled */
    lv_timer_create([](lv_timer_t* t) {
        if (g_wizard_admin_enrolled) {
            if (s_admin_next_btn) {
                lv_obj_clear_state(s_admin_next_btn, LV_STATE_DISABLED);
                lv_label_set_text(lv_obj_get_child(s_admin_next_btn, 0), "Next");
            }
            lv_timer_del(t);
        }
    }, 500, NULL);
}

static void next_btn_event_handler(lv_event_t* e) {
    if (s_current_step == 1 && s_wifi_ssid_ta) {
        const char* ssid = lv_textarea_get_text(s_wifi_ssid_ta);
        const char* pass = lv_textarea_get_text(s_wifi_pass_ta);
        if (strlen(ssid) > 0) {
            wifi_manager_connect(ssid, pass);
        }
    }
    
    if (s_current_step == 3 && s_pin_ta) {
        const char* pin = lv_textarea_get_text(s_pin_ta);
        if (!pin || strlen(pin) < 4) {
            ui_show_notification(NOTIFY_WARNING, "PIN Required",
                                 "Enter a 4-8 digit PIN", 2000);
            return;  /* block wizard advance until valid PIN is entered */
        }
        nvs_handle_t nvs;
        if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "admin_pin", pin);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
    }

    s_current_step++;
    
    /* Safely transition to the next step by deleting the old container asynchronously
       so the button's event handler doesn't crash from being deleted mid-event. */
    lv_obj_t* old_container = s_steps_container;
    s_steps_container = lv_obj_create(s_setup_screen);
    lv_obj_set_size(s_steps_container, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_opa(s_steps_container, 0, 0);
    lv_obj_set_style_border_width(s_steps_container, 0, 0);
    
    switch(s_current_step) {
        case 1: create_wifi_page(s_steps_container); break;
        case 2: create_admin_page(s_steps_container); break;
        case 3: create_pin_page(s_steps_container); break;
        case 4: 
            /* Finish */
            nvs_handle_t nvs;
            if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_set_u8(nvs, "setup_done", 1);
                nvs_commit(nvs);
                nvs_close(nvs);
            }
            ui_return_to_main();
            break;
    }
    
    if (old_container) {
        lv_obj_delete_async(old_container);
    }
}

static void skip_wifi_event_handler(lv_event_t* e) {
    ESP_LOGI(TAG, "Wi-Fi setup skipped");
    s_current_step = 1; /* Move to next step */
    next_btn_event_handler(NULL);
}

static lv_obj_t* create_admin_page(lv_obj_t* parent) {
    
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Step 2: Admin Enrollment");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    lv_obj_t* desc = lv_label_create(parent);
    lv_label_set_text(desc, "Enrolling your face as the Administrator\\n"
                            "enables secure access to settings.");
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -20);
    
    lv_obj_t* start_btn = ui_create_button(parent, "Enroll Admin Face", 220, 50);
    lv_obj_align(start_btn, LV_ALIGN_BOTTOM_MID, -90, -60);
    lv_obj_add_event_cb(start_btn, admin_enroll_event_handler, LV_EVENT_CLICKED, NULL);

    s_admin_next_btn = ui_create_button(parent, "Next", 120, 50);
    lv_obj_align(s_admin_next_btn, LV_ALIGN_BOTTOM_MID, 110, -60);
    lv_obj_add_event_cb(s_admin_next_btn, next_btn_event_handler, LV_EVENT_CLICKED, NULL);

    return parent;
}

static lv_obj_t* create_pin_page(lv_obj_t* parent) {
    
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Step 3: Security PIN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);
    
    lv_obj_t* desc = lv_label_create(parent);
    lv_label_set_text(desc, "Set a 4-8 digit numeric PIN for\\n"
                            "backup admin access.");
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_CENTER, 0, -20);
    
    s_pin_ta = lv_textarea_create(parent);
    lv_obj_set_size(s_pin_ta, 200, 50);
    lv_obj_align(s_pin_ta, LV_ALIGN_CENTER, 0, 40);
    lv_textarea_set_password_mode(s_pin_ta, true);
    lv_textarea_set_max_length(s_pin_ta, 8);
    lv_obj_add_event_cb(s_pin_ta, [](lv_event_t* e) {
        ui_show_keyboard(s_pin_ta, "Set Security PIN");
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* finish_btn = ui_create_button(parent, "Finish Setup", 180, 50);
    lv_obj_align(finish_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_add_event_cb(finish_btn, next_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    return parent;
}

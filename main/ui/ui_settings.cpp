/**
 * @file ui_settings.c
 * @brief Settings screen implementation
 */

#include "ui_settings.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "network/wifi_manager.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "config.h"
#include "database/db_manager.h"
#include "ui_user_manager.h"
#include "ui_admin_setup.h"
#include <cstring>

/* C function declarations */
extern "C" {
    void cloud_sync_set_interval(uint32_t interval_ms);
    void system_halt_for_reset(void);
}

static const char* TAG = "UI_SETTINGS";

/* Settings screen objects */
static lv_obj_t* s_settings_screen = NULL;
static lv_obj_t* s_brightness_slider = NULL;
static lv_obj_t* s_brightness_label = NULL;
static lv_obj_t* s_theme_switch = NULL;
static lv_obj_t* s_wifi_ssid_input = NULL;
static lv_obj_t* s_wifi_pass_input = NULL;
static lv_obj_t* s_telegram_token_input = NULL;
static lv_obj_t* s_telegram_chat_input = NULL;
static lv_obj_t* s_api_endpoint_input = NULL;
static lv_obj_t* s_about_label = NULL;
static lv_obj_t* s_user_mgr_btn = NULL;
static lv_obj_t* s_factory_reset_btn = NULL;
static lv_obj_t* s_known_networks_btn = NULL;
static lv_obj_t* s_wifi_status_label = NULL;
static lv_obj_t* s_sync_interval_dropdown = NULL;

/* Forward declarations */
static void create_settings_screen(void);
static void brightness_slider_event(lv_event_t* e);
static void theme_switch_event(lv_event_t* e);
static void wifi_connect_btn_event(lv_event_t* e);
static void wifi_scan_btn_event(lv_event_t* e);
static void wifi_scan_item_event_handler(lv_event_t* e);
static void user_mgr_btn_event(lv_event_t* e);
static void known_networks_btn_event(lv_event_t* e);
static void factory_reset_btn_event(lv_event_t* e);
static void factory_reset_confirm_cb(lv_event_t* e);
static void close_btn_event(lv_event_t* e);

void ui_show_settings_screen(void) {
    ESP_LOGI(TAG, "Showing settings screen");
    
    if (s_settings_screen) {
        lv_scr_load(s_settings_screen);
        return;
    }
    
    create_settings_screen();
}

void ui_close_settings_screen(void) {
    if (!s_settings_screen) return;
    ui_return_to_main();
}

static void textarea_click_event_cb(lv_event_t* e) {
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    const char* title = (const char*)lv_event_get_user_data(e);
    ui_show_keyboard(ta, title);
}

static void create_settings_screen(void) {
    s_settings_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_settings_screen);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(0x121212), 0);
    lv_scr_load(s_settings_screen);
    lv_obj_set_style_bg_color(s_settings_screen, lv_color_hex(0x121212), 0);
    
    /* Title bar */
    lv_obj_t* title_bar = lv_obj_create(s_settings_screen);
    lv_obj_set_size(title_bar, DISPLAY_WIDTH, 50);
    lv_obj_set_pos(title_bar, 0, 40);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    
    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "Settings");
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
    
    lv_obj_add_event_cb(close_btn, close_btn_event, LV_EVENT_CLICKED, NULL);
    
    /* Scrollable content area */
    lv_obj_t* content = lv_obj_create(s_settings_screen);
    lv_obj_set_size(content, DISPLAY_WIDTH, 450);
    lv_obj_set_pos(content, 0, 90);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    
    /* Brightness slider */
    lv_obj_t* brightness_card = ui_create_card(content, DISPLAY_WIDTH - 40, 100);
    lv_obj_set_style_pad_all(brightness_card, 15, 0);
    
    lv_obj_t* brightness_title = lv_label_create(brightness_card);
    lv_label_set_text(brightness_title, "Screen Brightness");
    lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_14, 0);
    
    s_brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_size(s_brightness_slider, DISPLAY_WIDTH - 100, 10);
    lv_obj_set_pos(s_brightness_slider, 15, 40);
    lv_slider_set_range(s_brightness_slider, 0, 100);
    lv_slider_set_value(s_brightness_slider, ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    
    s_brightness_label = lv_label_create(brightness_card);
    lv_obj_set_pos(s_brightness_label, DISPLAY_WIDTH - 80, 35);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", ui_get_brightness());
    lv_label_set_text(s_brightness_label, buf);
    
    /* Theme toggle */
    lv_obj_t* theme_card = ui_create_card(content, DISPLAY_WIDTH - 40, 80);
    
    lv_obj_t* theme_title = lv_label_create(theme_card);
    lv_label_set_text(theme_title, "Dark Theme");
    lv_obj_set_pos(theme_title, 15, 20);
    
    s_theme_switch = lv_switch_create(theme_card);
    lv_obj_set_pos(s_theme_switch, DISPLAY_WIDTH - 90, 15);
    lv_obj_set_state(s_theme_switch, LV_STATE_CHECKED, (ui_get_theme() == THEME_DARK));
    lv_obj_add_event_cb(s_theme_switch, theme_switch_event, LV_EVENT_VALUE_CHANGED, NULL);
    
    /* Wi-Fi section */
    lv_obj_t* wifi_card = ui_create_card(content, DISPLAY_WIDTH - 40, 220);
    
    lv_obj_t* wifi_title = lv_label_create(wifi_card);
    lv_label_set_text(wifi_title, "Wi-Fi Configuration");
    lv_obj_set_pos(wifi_title, 15, 10);
    
    lv_obj_t* ssid_label = lv_label_create(wifi_card);
    lv_label_set_text(ssid_label, "SSID");
    lv_obj_set_pos(ssid_label, 15, 45);
    
    s_wifi_ssid_input = lv_textarea_create(wifi_card);
    lv_obj_set_size(s_wifi_ssid_input, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_wifi_ssid_input, 15, 70);
    lv_textarea_set_one_line(s_wifi_ssid_input, true);
    lv_obj_add_event_cb(s_wifi_ssid_input, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"WiFi SSID");
    
    lv_obj_t* pass_label = lv_label_create(wifi_card);
    lv_label_set_text(pass_label, "Password");
    lv_obj_set_pos(pass_label, 15, 120);
    
    s_wifi_pass_input = lv_textarea_create(wifi_card);
    lv_obj_set_size(s_wifi_pass_input, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_wifi_pass_input, 15, 145);
    lv_textarea_set_one_line(s_wifi_pass_input, true);
    lv_textarea_set_password_mode(s_wifi_pass_input, true);
    lv_obj_add_event_cb(s_wifi_pass_input, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"WiFi Password");
    
    lv_obj_t* connect_btn = ui_create_button(wifi_card, "Connect", 120, 40);
    lv_obj_set_pos(connect_btn, 15, 195);
    lv_obj_add_event_cb(connect_btn, wifi_connect_btn_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t* scan_btn = ui_create_button(wifi_card, "Scan", 80, 40);
    lv_obj_set_pos(scan_btn, 150, 195);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_event, LV_EVENT_CLICKED, NULL);
    
    s_wifi_status_label = lv_label_create(wifi_card);
    lv_obj_set_pos(s_wifi_status_label, 15, 240);
    lv_label_set_text(s_wifi_status_label, "Not connected");
    lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0xAAAAAA), 0);
    
    s_known_networks_btn = ui_create_button(wifi_card, "Known Networks", 180, 40);
    lv_obj_set_pos(s_known_networks_btn, 15, 270);
    lv_obj_set_style_bg_color(s_known_networks_btn, lv_color_hex(0x336699), 0);
    lv_obj_add_event_cb(s_known_networks_btn, known_networks_btn_event, LV_EVENT_CLICKED, NULL);
    
    lv_obj_set_height(wifi_card, 330);
    
    /* Telegram section */
    lv_obj_t* telegram_card = ui_create_card(content, DISPLAY_WIDTH - 40, 330);
    
    lv_obj_t* telegram_title = lv_label_create(telegram_card);
    lv_label_set_text(telegram_title, "Cloud Sync & Telegram Configuration");
    lv_obj_set_pos(telegram_title, 15, 10);
    
    lv_obj_t* token_label = lv_label_create(telegram_card);
    lv_label_set_text(token_label, "Bot Token");
    lv_obj_set_pos(token_label, 15, 45);
    
    s_telegram_token_input = lv_textarea_create(telegram_card);
    lv_obj_set_size(s_telegram_token_input, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_telegram_token_input, 15, 70);
    lv_textarea_set_one_line(s_telegram_token_input, true);
    lv_obj_add_event_cb(s_telegram_token_input, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Telegram Bot Token");
    
    lv_obj_t* chat_label = lv_label_create(telegram_card);
    lv_label_set_text(chat_label, "Chat ID");
    lv_obj_set_pos(chat_label, 15, 120);
    
    s_telegram_chat_input = lv_textarea_create(telegram_card);
    lv_obj_set_size(s_telegram_chat_input, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_telegram_chat_input, 15, 145);
    lv_textarea_set_one_line(s_telegram_chat_input, true);
    lv_obj_add_event_cb(s_telegram_chat_input, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Telegram Chat ID");

    lv_obj_t* endpoint_label = lv_label_create(telegram_card);
    lv_label_set_text(endpoint_label, "Cloud Server URL / API Endpoint");
    lv_obj_set_pos(endpoint_label, 15, 195);

    s_api_endpoint_input = lv_textarea_create(telegram_card);
    lv_obj_set_size(s_api_endpoint_input, DISPLAY_WIDTH - 100, 40);
    lv_obj_set_pos(s_api_endpoint_input, 15, 220);
    lv_textarea_set_one_line(s_api_endpoint_input, true);
    lv_obj_add_event_cb(s_api_endpoint_input, textarea_click_event_cb, LV_EVENT_CLICKED, (void*)"Cloud Server URL");

    nvs_handle_t nvs_tel;
    if (nvs_open("telegram", NVS_READONLY, &nvs_tel) == ESP_OK) {
        char buf[256] = {0}; size_t len = sizeof(buf);
        if (nvs_get_str(nvs_tel, "token", buf, &len) == ESP_OK) lv_textarea_set_text(s_telegram_token_input, buf);
        len = sizeof(buf);
        if (nvs_get_str(nvs_tel, "chat_id", buf, &len) == ESP_OK) lv_textarea_set_text(s_telegram_chat_input, buf);
        len = sizeof(buf);
        if (nvs_get_str(nvs_tel, "endpoint", buf, &len) == ESP_OK) lv_textarea_set_text(s_api_endpoint_input, buf);
        nvs_close(nvs_tel);
    }

    lv_obj_t* save_tel_btn = ui_create_button(telegram_card, "Save", 120, 40);
    lv_obj_set_pos(save_tel_btn, 15, 275);
    lv_obj_add_event_cb(save_tel_btn, [](lv_event_t* e) {
        nvs_handle_t nvsh;
        if (nvs_open("telegram", NVS_READWRITE, &nvsh) == ESP_OK) {
            nvs_set_str(nvsh, "token", lv_textarea_get_text(s_telegram_token_input));
            nvs_set_str(nvsh, "chat_id", lv_textarea_get_text(s_telegram_chat_input));
            nvs_set_str(nvsh, "endpoint", lv_textarea_get_text(s_api_endpoint_input));
            nvs_commit(nvsh); nvs_close(nvsh);
            ui_show_notification(NOTIFY_SUCCESS, "Cloud Sync", "Credentials saved", 2000);
        }
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_set_height(telegram_card, 330);

    /* Sync interval dropdown */
    lv_obj_t* sync_card = ui_create_card(content, DISPLAY_WIDTH - 40, 100);
    
    lv_obj_t* sync_title = lv_label_create(sync_card);
    lv_label_set_text(sync_title, "Cloud Sync Interval");
    lv_obj_set_pos(sync_title, 15, 10);
    
    const char* intervals[] = {"Every hour", "Every 6 hours", "Every 12 hours", "Daily", "Never"};
    s_sync_interval_dropdown = lv_dropdown_create(sync_card);
    lv_obj_set_size(s_sync_interval_dropdown, 200, 40);
    lv_obj_set_pos(s_sync_interval_dropdown, 15, 45);
    char options[256];
    snprintf(options, sizeof(options), "%s\n%s\n%s\n%s\n%s", intervals[0], intervals[1], intervals[2], intervals[3], intervals[4]);
    lv_dropdown_set_options(s_sync_interval_dropdown, options);

    uint32_t saved_ms = 21600000;
    nvs_handle_t nvs_int;
    if (nvs_open("storage", NVS_READONLY, &nvs_int) == ESP_OK) {
        nvs_get_u32(nvs_int, "sync_ms", &saved_ms);
        nvs_close(nvs_int);
    }
    static const uint32_t ms_vals[] = {3600000, 21600000, 43200000, 86400000, 0};
    for (int i=0; i<5; i++) {
        if (ms_vals[i] == saved_ms) { lv_dropdown_set_selected(s_sync_interval_dropdown, i); break; }
    }
    
    lv_obj_add_event_cb(s_sync_interval_dropdown, [](lv_event_t* e) {
        const uint32_t* ms_vals_ptr = (const uint32_t*)lv_event_get_user_data(e);
        uint16_t sel = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
        uint32_t ms = ms_vals_ptr[sel];
        nvs_handle_t nvs;
        if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_u32(nvs, "sync_ms", ms);
            nvs_commit(nvs); nvs_close(nvs);
        }
        cloud_sync_set_interval(ms);
    }, LV_EVENT_VALUE_CHANGED, (void*)ms_vals);

    
    /* Device Management section */
    lv_obj_t* device_card = ui_create_card(content, DISPLAY_WIDTH - 40, 150);
    
    lv_obj_t* device_title = lv_label_create(device_card);
    lv_label_set_text(device_title, "Device Management");
    lv_obj_set_pos(device_title, 15, 10);
    
    s_user_mgr_btn = ui_create_button(device_card, "User Management", 200, 40);
    lv_obj_set_pos(s_user_mgr_btn, 15, 45);
    lv_obj_add_event_cb(s_user_mgr_btn, user_mgr_btn_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t* change_pin_btn = ui_create_button(device_card, "Change PIN", 150, 40);
    lv_obj_set_pos(change_pin_btn, 230, 45);
    lv_obj_add_event_cb(change_pin_btn, [](lv_event_t* e) {
        ui_show_pin_prompt(true, [](bool ok) {
            if (!ok) return;
            ui_show_keyboard_input("Enter New PIN", "", true, [](const char* text) {
                if(text && strlen(text) >= 4) {
                    nvs_handle_t nvs;
                    if(nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                        nvs_set_str(nvs, "admin_pin", text);
                        nvs_commit(nvs);
                        nvs_close(nvs);
                        ui_show_notification(NOTIFY_SUCCESS, "Security", "PIN Changed Successfully", 2000);
                    }
                } else {
                    ui_show_notification(NOTIFY_ERROR, "Security", "PIN must be at least 4 digits", 2000);
                }
            });
        });
    }, LV_EVENT_CLICKED, NULL);
    
    s_factory_reset_btn = ui_create_button(device_card, "Factory Reset", 150, 40);
    lv_obj_set_pos(s_factory_reset_btn, 15, 95);
    lv_obj_set_style_bg_color(s_factory_reset_btn, lv_color_hex(0xCC3333), 0);
    lv_obj_add_event_cb(s_factory_reset_btn, factory_reset_btn_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t* enroll_admin_btn = ui_create_button(device_card, "Enroll Admin", 180, 40);
    lv_obj_set_pos(enroll_admin_btn, 180, 95);
    lv_obj_set_style_bg_color(enroll_admin_btn, lv_color_hex(0x00A8FF), 0);
    lv_obj_add_event_cb(enroll_admin_btn, [](lv_event_t* e) {
        lv_obj_t* screen = lv_obj_get_screen((lv_obj_t*)lv_event_get_target(e));
        if (screen) {
            lv_obj_delete_async(screen);
            s_settings_screen = NULL;
        }
        ui_show_admin_setup_wizard();
    }, LV_EVENT_CLICKED, NULL);
    
    /* About section */
    lv_obj_t* about_card = ui_create_card(content, DISPLAY_WIDTH - 40, 120);
    
    lv_obj_t* about_title = lv_label_create(about_card);
    lv_label_set_text(about_title, "About");
    lv_obj_set_pos(about_title, 15, 10);
    
    s_about_label = lv_label_create(about_card);
    lv_label_set_text(s_about_label, "Smart Attendance System\nVersion 1.0.0\nCrowPanel Advanced 7\" ESP32-P4");
    lv_obj_set_pos(s_about_label, 15, 45);
    lv_obj_set_style_text_color(s_about_label, lv_color_hex(0xAAAAAA), 0);
}

static void brightness_slider_event(lv_event_t* e) {
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    ui_set_brightness(value);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(s_brightness_label, buf);
}

int ui_get_brightness(void) {
    uint8_t value = 80;
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "brightness", &value);
        nvs_close(nvs);
    }
    return (int)value;
}

void ui_set_brightness(int percent) {
    board_backlight_set(percent);
    
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "brightness", (uint8_t)percent);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void theme_switch_event(lv_event_t* e) {
    lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
    bool is_dark = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (is_dark) {
        ui_set_theme(THEME_DARK);
    } else {
        ui_set_theme(THEME_LIGHT);
    }
    /* Store theme preference */
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "theme", is_dark ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void wifi_connect_btn_event(lv_event_t* e) {
    const char* ssid = lv_textarea_get_text(s_wifi_ssid_input);
    const char* pass = lv_textarea_get_text(s_wifi_pass_input);
    
    if (strlen(ssid) == 0) {
        ui_show_notification(NOTIFY_WARNING, "Wi-Fi", "Please enter SSID", 2000);
        return;
    }
    
    lv_label_set_text(s_wifi_status_label, "Connecting...");
    lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0xFFAA44), 0);
    
    wifi_manager_connect(ssid, pass);
    
    lv_timer_create([](lv_timer_t* t) {
        wifi_status_t st = wifi_manager_get_status();
        if (st == WIFI_STATUS_CONNECTED) {
            lv_label_set_text(s_wifi_status_label, "Connected");
            lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0x44FF44), 0);
            ui_show_notification(NOTIFY_SUCCESS, "Wi-Fi", "Connected successfully", 2000);
            lv_timer_del(t);
        } else if (st == WIFI_STATUS_CONNECTION_FAILED) {
            lv_label_set_text(s_wifi_status_label, "Connection failed");
            lv_obj_set_style_text_color(s_wifi_status_label, lv_color_hex(0xFF4444), 0);
            ui_show_notification(NOTIFY_ERROR, "Wi-Fi", "Connection failed", 2000);
            lv_timer_del(t);
        }
    }, 500, NULL);
}

static void wifi_scan_btn_event(lv_event_t* e) {
    lv_label_set_text(s_wifi_status_label, "Scanning...");
    
    wifi_ap_record_t scan_results[20];
    int ap_count = wifi_manager_scan(scan_results, 20);
    
    if (ap_count <= 0) {
        lv_label_set_text(s_wifi_status_label, "No networks found");
        return;
    }
    
    /* Create a modal list of networks */
    lv_obj_t* modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal, DISPLAY_WIDTH - 100, DISPLAY_HEIGHT - 100);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t* title = lv_label_create(modal);
    lv_label_set_text(title, "Select Network");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    
    for (int i = 0; i < ap_count; i++) {
        lv_obj_t* btn = lv_list_add_btn(modal, LV_SYMBOL_WIFI, (char*)scan_results[i].ssid);
        lv_obj_add_event_cb(btn, wifi_scan_item_event_handler, LV_EVENT_CLICKED, modal);
    }
    
    lv_obj_t* close = lv_list_add_btn(modal, LV_SYMBOL_CLOSE, "Cancel");
    lv_obj_add_event_cb(close, [](lv_event_t* e){ lv_obj_delete_async((lv_obj_t*)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED, modal);
    
    lv_label_set_text(s_wifi_status_label, "Select a network");
}

static void wifi_scan_item_event_handler(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* modal = (lv_obj_t*)lv_event_get_user_data(e);
    
    const char* ssid = lv_list_get_btn_text(modal, btn);
    lv_textarea_set_text(s_wifi_ssid_input, ssid);
    
    lv_obj_delete_async(modal);
    lv_label_set_text(s_wifi_status_label, "SSID selected");
}

static void user_mgr_btn_event(lv_event_t* e) {
    ui_show_user_manager();
}

static void known_networks_btn_event(lv_event_t* e) {
    /* Create a modal list of known networks from NVS */
    lv_obj_t* modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(modal, DISPLAY_WIDTH - 120, DISPLAY_HEIGHT - 120);
    lv_obj_center(modal);
    lv_obj_set_style_bg_color(modal, lv_color_hex(0x222222), 0);
    lv_obj_set_flex_flow(modal, LV_FLEX_FLOW_COLUMN);
    
    lv_obj_t* title = lv_label_create(modal);
    lv_label_set_text(title, "Known Networks");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    
    nvs_handle_t nvs;
    if (nvs_open("wifi_creds", NVS_READONLY, &nvs) == ESP_OK) {
        int32_t count = 0;
        nvs_get_i32(nvs, "count", &count);
        for (int i = 0; i < count; i++) {
            char key[16], ssid[32];
            size_t len = sizeof(ssid);
            snprintf(key, sizeof(key), "s_%d", i);
            if (nvs_get_str(nvs, key, ssid, &len) == ESP_OK) {
                lv_obj_t* label = lv_label_create(modal);
                lv_label_set_text_fmt(label, "%d. %s", i+1, ssid);
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
            }
        }
        nvs_close(nvs);
    }
    
    lv_obj_t* close = ui_create_button(modal, "Close", 100, 40);
    lv_obj_add_event_cb(close, [](lv_event_t* e){ lv_obj_delete_async((lv_obj_t*)lv_event_get_user_data(e)); }, LV_EVENT_CLICKED, modal);
}

static void factory_reset_btn_event(lv_event_t* e) {
    lv_obj_t * m = lv_msgbox_create(NULL);
    lv_msgbox_add_title(m, "Factory Reset");
    lv_msgbox_add_text(m, "This will wipe all users, logs, and settings.\nAre you sure?");
    
    lv_obj_t * btn_reset = lv_msgbox_add_footer_button(m, "Reset");
    lv_obj_add_event_cb(btn_reset, factory_reset_confirm_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)0);
    
    lv_obj_t * btn_cancel = lv_msgbox_add_footer_button(m, "Cancel");
    lv_obj_add_event_cb(btn_cancel, factory_reset_confirm_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)1);
    
    lv_obj_center(m);
}

static void factory_reset_confirm_cb(lv_event_t* e) {
    uintptr_t btn_id = (uintptr_t)lv_event_get_user_data(e);
    lv_obj_t * btn = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t * m = lv_obj_get_parent(lv_obj_get_parent(btn)); /* btn -> footer -> msgbox */
    
    if (btn_id == 0) { /* Reset */
        ESP_LOGW(TAG, "Factory Reset confirmed!");
        system_halt_for_reset();
        
        db_manager_flush();
        db_factory_reset();
        nvs_flash_erase(); /* Extreme wipe */
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        lv_msgbox_close(m);
    }
}

static void close_btn_event(lv_event_t* e) {
    ui_close_settings_screen();
}
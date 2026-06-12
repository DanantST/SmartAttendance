/**
 * @file ui_main.c
 * @brief Main UI implementation for Smart Attendance System
 */

#include "ui_main.h"
#include "font/lv_symbol_def.h"

/* Explicitly declare LVGL fonts since they might not be enabled in sdkconfig */
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_32);
LV_FONT_DECLARE(lv_font_montserrat_48);

#include "ui_components.h"
#include "ui_theme.h"
#include "config.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "cloud_sync.h"
#include "display/lcd_driver.h"
#include "display/touch_driver.h"
#include "display/backlight.h"
#include "ui_attendance.h"
#include "ui_file_manager.h"
#include "ui_enrollment.h"
#include "ui_settings.h"
#include "ui_reports.h"
#include "ui_user_manager.h"
#include "wifi_manager.h"

static void update_time_task(lv_timer_t *timer);

static const char* TAG = "UI_MAIN";

static void notification_close_cb(lv_event_t *e) {
    ui_hide_notification();
}
static void keyboard_close_cb(lv_event_t *e) {
    ui_hide_keyboard();
}
/* LVGL objects */
static lv_display_t* s_display = NULL;
static lv_indev_t* s_touch_indev = NULL;
static lv_obj_t* s_main_screen = NULL;

/* Thread safety */
/* Mutex removed, using lcd_driver.c's s_lvgl_mux via ui_lock/ui_unlock instead */

bool ui_acquire(void) {
    return ui_lock();
}

void ui_release(void) {
    ui_unlock();
}

/* Navigation bar objects */
static lv_obj_t* s_nav_bar = NULL;
static lv_obj_t* s_nav_buttons[5]; /* Home, Enroll, Sync, Reports, Settings */
static void (*s_nav_callback)(ui_nav_button_t) = NULL;

/* Status bar objects */
static lv_obj_t* s_status_bar = NULL;
static lv_obj_t* s_battery_icon = NULL;
static lv_obj_t* s_battery_label = NULL;
static lv_obj_t* s_wifi_icon = NULL;
static lv_obj_t* s_sync_icon = NULL;
static lv_obj_t* s_sd_icon = NULL;
static lv_obj_t* s_time_label = NULL;
static lv_obj_t* s_date_label = NULL;

/* Desktop Clock Widget objects */
static lv_obj_t* s_desktop_clock = NULL;
static lv_obj_t* s_desktop_date = NULL;

/* Notification & Quick Settings panels */
static lv_obj_t* s_notification_panel = NULL;
static lv_timer_t* s_notification_timer = NULL;
static lv_obj_t* s_qs_panel = NULL;
static lv_obj_t* s_recents_panel = NULL;
static lv_obj_t* s_screen_history[10];
static int s_history_depth = 0;
static int s_recent_apps[5] = {-1, -1, -1, -1, -1};

/* Keyboard */
static lv_obj_t* s_keyboard = NULL;
static lv_obj_t* s_keyboard_textarea = NULL;
static lv_obj_t* s_keyboard_panel = NULL;

/* SD Card Banner */
static lv_obj_t* s_sd_banner = NULL;
static bool s_sd_card_ok = true;
static lv_obj_t* s_pin_panel = NULL;
static lv_obj_t* s_pin_textarea = NULL;
static lv_obj_t* s_pin_keypad = NULL;
static void (*s_pin_callback)(bool) = NULL;
static bool s_is_admin_change = false;

/* Current state */
static ui_theme_t s_current_theme = (ui_theme_t)UI_THEME_DEFAULT;
static bool s_notification_visible = false;

struct NotificationHistory {
    char title[64];
    char message[128];
};
static NotificationHistory s_notify_history[3];
static int s_notify_count = 0;

/* Forward declarations */
static void create_status_bar(void);
static void create_navigation_bar(void);
static void create_main_content(void);
static void nav_button_event_handler(lv_event_t* e);
void ui_show_quick_settings(void);
void ui_hide_quick_settings(void);
void ui_show_recent_apps(void);
static void launch_app_by_id(int app_id);

/*============================================================================
   Initialization
 *===========================================================================*/

esp_err_t ui_init(void) {
    ESP_LOGI(TAG, "Initializing UI system");

    /* Rely on the display_lvgl_init to create the true LVGL mutex */

    /* 1. Initialize touch driver first!
     * The GT911 reset line (GPIO 40) is shared with the LCD hardware reset on this board.
     * Pulsing it resets BOTH chips. We must do this before initializing the LCD via MIPI-DSI,
     * otherwise the LCD will lose its initialization state and remain blank. */
    touch_init();

    /* 2. Initialize display (now that hardware reset is complete and held HIGH) */
    esp_lcd_panel_handle_t panel_handle = NULL;
    if (display_init(&panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed");
        return ESP_FAIL;
    }

    /* 3. Initialize LVGL + display + tick timer + render task */
    if (display_lvgl_init(panel_handle) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL init failed");
        return ESP_FAIL;
    }
    
    /* Get the display created inside display_lvgl_init */
    s_display = lv_display_get_next(NULL);
    if (s_display) lv_display_set_default(s_display);

    /* 4. Link touch driver to LVGL */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_display);
    s_touch_indev = indev;
    
    /* Initialize and turn on backlight */
    backlight_init();
    backlight_set(100); /* Force 100% brightness */

    /* Apply LIGHT theme for high visibility test */
    ui_set_theme(THEME_LIGHT);

    /* Create main screen layout */
    s_main_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_main_screen);
    create_main_content();
    create_status_bar();
    create_navigation_bar();
    lv_scr_load(s_main_screen);

    /* Start time update task */
    lv_timer_create(update_time_task, 1000, NULL);

    ESP_LOGI(TAG, "UI initialized successfully");
    return ESP_OK;
}

lv_display_t* ui_get_display(void) {
    return s_display;
}

lv_indev_t* ui_get_touch(void) {
    return s_touch_indev;
}

/*============================================================================
   Status Bar
 *===========================================================================*/

static void create_status_bar(void) {
    /* Create on top layer so it persists across all screens */
    s_status_bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_status_bar, DISPLAY_WIDTH, 40);
    lv_obj_set_pos(s_status_bar, 0, 0);
    lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(s_status_bar, LV_OPA_80, 0); /* Transparent overlay */
    lv_obj_set_style_radius(s_status_bar, 0, 0);
    lv_obj_set_style_border_width(s_status_bar, 0, 0);
    
    /* Remove default padding */
    lv_obj_set_style_pad_all(s_status_bar, 0, 0);
    
    /* Time label (left) */
    s_time_label = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_time_label, 15, 10);
    lv_label_set_text(s_time_label, "12:00");
    lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_14, 0);
    
    /* Battery icon and label (right) */
    s_battery_icon = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_battery_icon, DISPLAY_WIDTH - 55, 10);
    lv_label_set_text(s_battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_color(s_battery_icon, lv_color_white(), 0);
    
    s_battery_label = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_battery_label, DISPLAY_WIDTH - 35, 10);
    lv_label_set_text(s_battery_label, "100%");
    lv_obj_set_style_text_color(s_battery_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_14, 0);
    
    /* Wi-Fi icon */
    s_wifi_icon = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_wifi_icon, DISPLAY_WIDTH - 95, 10);
    lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x888888), 0);
    
    /* Sync icon */
    s_sync_icon = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_sync_icon, DISPLAY_WIDTH - 125, 10);
    lv_label_set_text(s_sync_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(s_sync_icon, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_sync_icon, &lv_font_montserrat_14, 0);

    /* SD Card icon */
    s_sd_icon = lv_label_create(s_status_bar);
    lv_obj_set_pos(s_sd_icon, DISPLAY_WIDTH - 155, 10);
    lv_label_set_text(s_sd_icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_color(s_sd_icon, s_sd_card_ok ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(s_sd_icon, &lv_font_montserrat_14, 0);

    /* Enable clicking / swiping down on status bar */
    lv_obj_add_flag(s_status_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_status_bar, [](lv_event_t *e) {
        ui_show_quick_settings();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_status_bar, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            ui_show_quick_settings();
        }
    }, LV_EVENT_GESTURE, NULL);
}

void ui_set_battery_percent(int percent, bool charging) {
    if (!s_battery_label) return;
    if (!ui_acquire()) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    lv_label_set_text(s_battery_label, buf);

    /* When charging: always show the charge/plug symbol in cyan */
    if (charging) {
        lv_label_set_text(s_battery_icon, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(s_battery_icon,  lv_color_hex(0x00CFFF), 0);
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x00CFFF), 0);
        ui_release();
        return;
    }

    /* Not charging: pick icon by level */
    const char* icon;
    if (percent >= 90)      icon = LV_SYMBOL_BATTERY_FULL;
    else if (percent >= 60) icon = LV_SYMBOL_BATTERY_3;
    else if (percent >= 30) icon = LV_SYMBOL_BATTERY_2;
    else if (percent >= 10) icon = LV_SYMBOL_BATTERY_1;
    else                    icon = LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text(s_battery_icon, icon);

    /* Colour: red below 15%, otherwise theme-white */
    if (percent < 15) {
        lv_obj_set_style_text_color(s_battery_icon,  lv_color_hex(0xFF4444), 0);
        lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0xFF4444), 0);
    } else {
        lv_color_t color = (ui_get_theme() == THEME_LIGHT) ? lv_color_black() : lv_color_white();
        lv_obj_set_style_text_color(s_battery_icon,  color, 0);
        lv_obj_set_style_text_color(s_battery_label, color, 0);
    }
    ui_release();
}

void ui_set_wifi_status(bool connected, int rssi) {
    if (!s_wifi_icon) return;
    if (!ui_acquire()) return;
    if (connected) {
        lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
        /* Optional: color based on signal strength */
        lv_obj_set_style_text_color(s_wifi_icon, lv_color_white(), 0);
    } else {
        lv_label_set_text(s_wifi_icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(s_wifi_icon, lv_color_hex(0x888888), 0);
    }
    ui_release();
}

void ui_set_sync_status(bool syncing) {
    if (!s_sync_icon) return;
    if (!ui_acquire()) return;
    if (syncing) {
        lv_label_set_text(s_sync_icon, LV_SYMBOL_REFRESH);
        /* Animation would be added for rotating effect */
        lv_obj_set_style_text_color(s_sync_icon, lv_color_hex(0x44AAFF), 0);
    } else {
        lv_label_set_text(s_sync_icon, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(s_sync_icon, lv_color_hex(0x888888), 0);
    }
    ui_release();
}

void ui_update_time(int hour, int minute) {
    if (!s_time_label) return;
    if (!ui_acquire()) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, minute);
    lv_label_set_text(s_time_label, buf);
    ui_release();
}

void ui_update_date(int day, int month, int year) {
    if (!s_date_label) return;
    if (!ui_acquire()) return;
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d, %d", months[month-1], day, year);
    lv_label_set_text(s_date_label, buf);
    ui_release();
}

static void update_time_task(lv_timer_t *timer) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    if (tm_info) {
        if (tm_info->tm_year + 1900 < 2024) {
            ui_acquire();
            if (s_time_label) lv_label_set_text(s_time_label, "--:--");
            if (s_desktop_clock) lv_label_set_text(s_desktop_clock, "--:--");
            if (s_desktop_date) lv_label_set_text(s_desktop_date, "Syncing time...");
            ui_release();
        } else {
            ui_update_time(tm_info->tm_hour, tm_info->tm_min);
            ui_update_date(tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);
            
            ui_acquire();
            if (s_desktop_clock) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d:%02d", tm_info->tm_hour, tm_info->tm_min);
                lv_label_set_text(s_desktop_clock, buf);
            }
            if (s_desktop_date) {
                const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
                const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                char buf[64];
                snprintf(buf, sizeof(buf), "%s, %s %d, %d", days[tm_info->tm_wday], months[tm_info->tm_mon], tm_info->tm_mday, tm_info->tm_year + 1900);
                lv_label_set_text(s_desktop_date, buf);
            }
            ui_release();
        }
    }
}

/*============================================================================
   Navigation Bar
 *===========================================================================*/

static void nav_button_event_handler(lv_event_t* e) {
    int btn_id = (int)(uintptr_t)lv_event_get_user_data(e);
    
    if (btn_id == 0) {
        /* Back Button */
        ui_acquire();
        if (s_history_depth > 1) {
            s_history_depth--; // Pop current screen
            lv_obj_t* prev_scr = s_screen_history[s_history_depth - 1];
            lv_screen_load(prev_scr);
            ui_release();
        } else {
            ui_release();
            ui_return_to_main();
        }
    } else if (btn_id == 1) {
        /* Home Button */
        ui_return_to_main();
    } else if (btn_id == 2) {
        /* Recents Button */
        ui_show_recent_apps();
    }
}

static void create_navigation_bar(void) {
    s_nav_bar = lv_obj_create(lv_layer_top()); /* Persist across screens */
    lv_obj_set_size(s_nav_bar, DISPLAY_WIDTH, 60);
    lv_obj_set_pos(s_nav_bar, 0, DISPLAY_HEIGHT - 60);
    lv_obj_set_style_bg_color(s_nav_bar, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_opa(s_nav_bar, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_nav_bar, 0, 0);
    lv_obj_set_style_border_width(s_nav_bar, 0, 0);
    lv_obj_set_style_pad_all(s_nav_bar, 0, 0);
    
    const char* icons[] = { LV_SYMBOL_LEFT, LV_SYMBOL_HOME, LV_SYMBOL_LIST };
    
    int btn_width = DISPLAY_WIDTH / 3;
    
    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_btn_create(s_nav_bar);
        lv_obj_set_size(btn, btn_width, 60);
        lv_obj_set_pos(btn, i * btn_width, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, icons[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        
        lv_obj_add_event_cb(btn, nav_button_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
}

void ui_register_nav_callback(void (*callback)(ui_nav_button_t button)) {
    s_nav_callback = callback;
}

void ui_set_active_nav_button(ui_nav_button_t button) {
    /* No longer used in Android style nav */
}

extern "C" void start_enrollment_task(void *pvParam);

/*============================================================================
   Launcher Desktop (App Grid)
 *===========================================================================*/

static void launch_app_by_id(int app_id) {
    if (app_id == 0 && s_nav_callback) {
        ui_show_attendance_screen();
    } else if (app_id == 1 && s_nav_callback) {
        /* Spawn the enrollment task to manage BLE and system state */
        xTaskCreate(start_enrollment_task, "enrollment", 8192, NULL, 5, NULL);
    } else if (app_id == 2 && s_nav_callback) {
        ui_show_pin_prompt(false, [](bool success) {
            if (success) ui_show_user_manager();
        });
    } else if (app_id == 3 && s_nav_callback) {
        ui_show_reports_screen();
    } else if (app_id == 4 && s_nav_callback) {
        ui_show_pin_prompt(false, [](bool success) {
            if (success) ui_show_settings_screen();
        });
    } else if (app_id == 5) {
        ui_show_file_manager_screen();
    }
}

static void app_icon_event_handler(lv_event_t* e) {
    int app_id = (int)(uintptr_t)lv_event_get_user_data(e);
    
    /* Add to recent apps if not already the most recent */
    if (s_recent_apps[0] != app_id) {
        for (int i = 4; i > 0; i--) {
            s_recent_apps[i] = s_recent_apps[i-1];
        }
        s_recent_apps[0] = app_id;
    }
    
    launch_app_by_id(app_id);
}

static void create_main_content(void) {
    /* Set wallpaper/background */
    lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(0x121212), 0);
    
    /* Large Desktop Clock Widget */
    s_desktop_clock = lv_label_create(s_main_screen);
    lv_obj_set_pos(s_desktop_clock, DISPLAY_WIDTH / 2 - 80, 60);
    lv_label_set_text(s_desktop_clock, "12:00");
    lv_obj_set_style_text_color(s_desktop_clock, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_desktop_clock, &lv_font_montserrat_48, 0);
    
    s_desktop_date = lv_label_create(s_main_screen);
    lv_obj_set_pos(s_desktop_date, DISPLAY_WIDTH / 2 - 80, 120);
    lv_label_set_text(s_desktop_date, "Syncing time...");
    lv_obj_set_style_text_color(s_desktop_date, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(s_desktop_date, &lv_font_montserrat_16, 0);

    /* Grid for App Icons */
    lv_obj_t* grid = lv_obj_create(s_main_screen);
    lv_obj_set_size(grid, DISPLAY_WIDTH - 40, 240);
    lv_obj_set_pos(grid, 20, 160);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    const char* app_names[] = {"Scanner", "Enroll", "Users", "Reports", "Settings", "Files"};
    const char* app_icons[] = {LV_SYMBOL_VIDEO, LV_SYMBOL_PLUS, LV_SYMBOL_LIST, LV_SYMBOL_FILE, LV_SYMBOL_SETTINGS, LV_SYMBOL_DIRECTORY};
    const uint32_t app_colors[] = {0x3A6EA5, 0x4CAF50, 0x00A8FF, 0xFF9800, 0x9E9E9E, 0x673AB7};

    for(int i = 0; i < 6; i++) {
        lv_obj_t* icon_btn = lv_btn_create(grid);
        lv_obj_set_size(icon_btn, 80, 80);
        lv_obj_set_style_bg_color(icon_btn, lv_color_hex(app_colors[i]), 0);
        lv_obj_set_style_radius(icon_btn, 20, 0);
        lv_obj_add_event_cb(icon_btn, app_icon_event_handler, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        
        lv_obj_t* symbol = lv_label_create(icon_btn);
        lv_label_set_text(symbol, app_icons[i]);
        lv_obj_set_style_text_font(symbol, &lv_font_montserrat_32, 0);
        lv_obj_align(symbol, LV_ALIGN_CENTER, 0, -10);
        
        lv_obj_t* label = lv_label_create(icon_btn);
        lv_label_set_text(label, app_names[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 20);
    }
}

/*============================================================================
   Theme Management
 *===========================================================================*/

void ui_set_theme(ui_theme_t theme) {
    s_current_theme = theme;
    
    if (theme == THEME_LIGHT) {
        /* Light theme styles */
        lv_display_set_theme(s_display, NULL); /* Reset */
        lv_theme_t* th = lv_theme_default_init(s_display, lv_color_hex(0x3A6EA5), lv_color_hex(0xDDDDDD), 
                                               false, &lv_font_montserrat_14);
        lv_display_set_theme(s_display, th);
        
        /* Override status bar - only if UI has been built */
        if (s_status_bar) {
            lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0xF5F5F5), 0);
            lv_obj_set_style_text_color(s_time_label, lv_color_hex(0x333333), 0);
            lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x666666), 0);
            lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0x333333), 0);
        }
        
        /* Navigation bar */
        if (s_nav_bar) {
            lv_obj_set_style_bg_color(s_nav_bar, lv_color_hex(0xE8E8E8), 0);
            for (int i = 0; i < 5; i++) {
                if (s_nav_buttons[i]) {
                    lv_obj_set_style_bg_color(s_nav_buttons[i], lv_color_hex(0xF0F0F0), 0);
                }
            }
        }
    } else {
        /* Dark theme styles (default) */
        lv_display_set_theme(s_display, NULL);
        lv_theme_t* th = lv_theme_default_init(s_display, lv_color_hex(0x3A6EA5), lv_color_hex(0x333333), 
                                               true, &lv_font_montserrat_14);
        lv_display_set_theme(s_display, th);
        
        if (s_status_bar) {
            lv_obj_set_style_bg_color(s_status_bar, lv_color_hex(0x1E1E1E), 0);
            lv_obj_set_style_text_color(s_time_label, lv_color_white(), 0);
            lv_obj_set_style_text_color(s_date_label, lv_color_hex(0xAAAAAA), 0);
            lv_obj_set_style_text_color(s_battery_label, lv_color_white(), 0);
        }
        
        if (s_nav_bar) {
            lv_obj_set_style_bg_color(s_nav_bar, lv_color_hex(0x1E1E1E), 0);
            for (int i = 0; i < 5; i++) {
                if (s_nav_buttons[i]) {
                    lv_obj_set_style_bg_color(s_nav_buttons[i], lv_color_hex(0x2A2A2A), 0);
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "Theme set to %s", theme == THEME_LIGHT ? "light" : "dark");
}

ui_theme_t ui_get_theme(void) {
    return s_current_theme;
}

void ui_theme_toggle(void) {
    if (s_current_theme == THEME_LIGHT) {
        ui_set_theme(THEME_DARK);
    } else {
        ui_set_theme(THEME_LIGHT);
    }
}

/*============================================================================
   Notification Panel
 *===========================================================================*/

void ui_show_notification(ui_notify_type_t type, const char* title, const char* message, uint32_t timeout_ms) {
    if (!ui_acquire()) return;
    
    // Record in history (FIFO)
    for (int i = 2; i > 0; i--) {
        s_notify_history[i] = s_notify_history[i - 1];
    }
    strncpy(s_notify_history[0].title, title ? title : "Info", sizeof(s_notify_history[0].title) - 1);
    strncpy(s_notify_history[0].message, message ? message : "", sizeof(s_notify_history[0].message) - 1);
    if (s_notify_count < 3) s_notify_count++;

    /* If notification panel exists, delete it */
    if (s_notification_panel) {
        lv_obj_delete_async(s_notification_panel);
        s_notification_panel = NULL;
    }
    if (s_notification_timer) {
        lv_timer_del(s_notification_timer);
        s_notification_timer = NULL;
    }
    
    /* Create panel on top layer so it appears above all screens */
    s_notification_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_notification_panel, DISPLAY_WIDTH - 40, 100);
    lv_obj_set_pos(s_notification_panel, 20, 45);
    lv_obj_set_style_radius(s_notification_panel, 12, 0);
    lv_obj_set_style_shadow_width(s_notification_panel, 10, 0);
    lv_obj_set_style_shadow_ofs_x(s_notification_panel, 0, 0);
    lv_obj_set_style_shadow_ofs_y(s_notification_panel, 2, 0);
    
    /* Set colors based on type */
    lv_color_t bg_color;
    lv_color_t border_color;
    const char* icon;
    
    switch (type) {
        case NOTIFY_INFO:
            bg_color = lv_color_hex(0x2196F3);
            border_color = lv_color_hex(0x0B5E9E);
            icon = LV_SYMBOL_BELL;
            break;
        case NOTIFY_SUCCESS:
            bg_color = lv_color_hex(0x4CAF50);
            border_color = lv_color_hex(0x2E7D32);
            icon = LV_SYMBOL_OK;
            break;
        case NOTIFY_WARNING:
            bg_color = lv_color_hex(0xFF9800);
            border_color = lv_color_hex(0xBF5E00);
            icon = LV_SYMBOL_WARNING;
            break;
        case NOTIFY_ERROR:
            bg_color = lv_color_hex(0xF44336);
            border_color = lv_color_hex(0xB71C1C);
            icon = LV_SYMBOL_CLOSE;
            break;
        default:
            bg_color = lv_color_hex(0x757575);
            border_color = lv_color_hex(0x424242);
            icon = LV_SYMBOL_BELL;
            break;
    }
    
    lv_obj_set_style_bg_color(s_notification_panel, bg_color, 0);
    lv_obj_set_style_border_width(s_notification_panel, 2, 0);
    lv_obj_set_style_border_color(s_notification_panel, border_color, 0);
    
    /* Icon */
    lv_obj_t* icon_label = lv_label_create(s_notification_panel);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_pos(icon_label, 15, 35);
    lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_14, 0);
    
    /* Title */
    lv_obj_t* title_label = lv_label_create(s_notification_panel);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 55, 15);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, 0);
    
    /* Message */
    lv_obj_t* msg_label = lv_label_create(s_notification_panel);
    lv_label_set_text(msg_label, message);
    lv_obj_set_pos(msg_label, 55, 40);
    lv_obj_set_style_text_color(msg_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_14, 0);
    
    /* Close button (larger touch target, aligned robustly within bounds) */
    lv_obj_t* close_btn = lv_btn_create(s_notification_panel);
    lv_obj_set_size(close_btn, 44, 44);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_30, 0);
    lv_obj_set_style_radius(close_btn, 22, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_14, 0);
    
    lv_obj_add_event_cb(close_btn, notification_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_notification_panel, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_TOP) {
            ui_hide_notification();
        }
    }, LV_EVENT_GESTURE, NULL);
    
    s_notification_visible = true;
    
    /* Auto-dismiss timer */
    if (timeout_ms > 0) {
        s_notification_timer = lv_timer_create([](lv_timer_t* timer) {
            ui_hide_notification();
        }, timeout_ms, NULL);
    }
    ui_release();
}

void ui_hide_notification(void) {
    if (!ui_acquire()) return;
    if (s_notification_panel) {
        lv_obj_delete_async(s_notification_panel);
        s_notification_panel = NULL;
    }
    if (s_notification_timer) {
        lv_timer_del(s_notification_timer);
        s_notification_timer = NULL;
    }
    s_notification_visible = false;
    ui_release();
}


bool ui_is_notification_visible(void) {
    return s_notification_visible;
}

void ui_set_sd_card_status(bool ok) {
    s_sd_card_ok = ok;
    if (!ui_acquire()) return;
    if (s_sd_icon) {
        lv_obj_set_style_text_color(s_sd_icon, ok ? lv_color_hex(0x4CAF50) : lv_color_hex(0xFF4444), 0);
    }
    if (!ok && s_main_screen) {
        if (!s_sd_banner) {
            s_sd_banner = lv_obj_create(s_main_screen);
            lv_obj_set_size(s_sd_banner, DISPLAY_WIDTH, 40);
            lv_obj_set_pos(s_sd_banner, 0, 50); /* Just below status bar */
            lv_obj_set_style_bg_color(s_sd_banner, lv_color_hex(0xFF4444), 0);
            lv_obj_set_style_radius(s_sd_banner, 0, 0);
            
            lv_obj_t* label = lv_label_create(s_sd_banner);
            lv_label_set_text(label, LV_SYMBOL_WARNING "  SD CARD MISSING - ATTENDANCE LOGGING DISABLED");
            lv_obj_center(label);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        }
        lv_obj_clear_flag(s_sd_banner, LV_OBJ_FLAG_HIDDEN);
    } else if (s_sd_banner) {
        lv_obj_add_flag(s_sd_banner, LV_OBJ_FLAG_HIDDEN);
    }
    ui_release();
}

/*============================================================================
   Keyboard
 *===========================================================================*/

void ui_show_keyboard(lv_obj_t* textarea, const char* title) {
    /* Hide existing keyboard if present */
    if (s_keyboard_panel) {
        ui_hide_keyboard();
    }
    
    /* Create keyboard panel on the currently active screen.
     * Panel height = 240px, positioned at Y=290 → bottom at Y=530, above nav bar (Y=540). */
    s_keyboard_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_keyboard_panel, DISPLAY_WIDTH - 40, 240);
    lv_obj_set_pos(s_keyboard_panel, 20, 290);
    lv_obj_set_style_bg_color(s_keyboard_panel, s_current_theme == THEME_LIGHT ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(s_keyboard_panel, 16, 0);
    lv_obj_set_style_shadow_width(s_keyboard_panel, 10, 0);
    
    /* Title */
    if (title) {
        lv_obj_t* title_label = lv_label_create(s_keyboard_panel);
        lv_label_set_text(title_label, title);
        lv_obj_set_pos(title_label, 15, 10);
        lv_obj_set_style_text_color(title_label, s_current_theme == THEME_LIGHT ? lv_color_hex(0x333333) : lv_color_white(), 0);
    }
    
    /* Create keyboard */
    s_keyboard = lv_keyboard_create(s_keyboard_panel);
    lv_obj_set_size(s_keyboard, DISPLAY_WIDTH - 80, 185);
    lv_obj_set_pos(s_keyboard, 20, 40);
    lv_keyboard_set_textarea(s_keyboard, textarea);
    s_keyboard_textarea = textarea;
    
    /* Set keyboard mode to text */
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    
    /* Add close button on keyboard */
    lv_obj_t* close_btn = lv_btn_create(s_keyboard_panel);
    lv_obj_set_size(close_btn, 60, 36);
    lv_obj_set_pos(close_btn, DISPLAY_WIDTH - 110, 10);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xF44336), 0);
    lv_obj_set_style_radius(close_btn, 18, 0);
    
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Done");
    lv_obj_center(close_label);
    lv_obj_set_style_text_color(close_label, lv_color_white(), 0);
    
    lv_obj_add_event_cb(close_btn, keyboard_close_cb, LV_EVENT_CLICKED, NULL);
    
    /* Focus textarea */
    lv_group_t* group = lv_group_create();
    lv_group_add_obj(group, textarea);
    lv_indev_set_group(ui_get_touch(), group);
    lv_group_focus_obj(textarea);
}

void ui_hide_keyboard(void) {
    if (s_keyboard_panel) {
        lv_obj_delete_async(s_keyboard_panel);
        s_keyboard_panel = NULL;
    }
    s_keyboard = NULL;
    s_keyboard_textarea = NULL;
}

/*============================================================================
   Utility Functions
 *===========================================================================*/

lv_obj_t* ui_create_button(lv_obj_t* parent, const char* text, int width, int height) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A6EA5), 0);
    lv_obj_set_style_shadow_width(btn, 5, 0);
    
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    
    return btn;
}

lv_obj_t* ui_create_card(lv_obj_t* parent, int width, int height) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_bg_color(card, s_current_theme == THEME_LIGHT ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_shadow_ofs_x(card, 0, 0);
    lv_obj_set_style_shadow_ofs_y(card, 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    
    return card;
}

/*============================================================================
   PIN Entry Prompt
 *===========================================================================*/

extern "C" bool verify_admin_pin(const char *pin); /* Forward from main.c */

static void pin_keypad_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (code == LV_EVENT_READY) {
            const char * pin = lv_textarea_get_text(s_pin_textarea);
            bool success = verify_admin_pin(pin);
            if (s_pin_callback) s_pin_callback(success);
        } else {
            if (s_pin_callback) s_pin_callback(false);
        }
        ui_close_pin_prompt();
    }
}

void ui_show_pin_prompt(bool for_admin_change, void (*callback)(bool success)) {
    s_pin_callback = callback;
    s_is_admin_change = for_admin_change;

    /* Create a modal background that covers only the safe area (below status bar,
     * above nav bar): Y=40, height=500 so bottom is at Y=540. */
    s_pin_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_pin_panel, DISPLAY_WIDTH, 500);
    lv_obj_set_pos(s_pin_panel, 0, 40);
    lv_obj_set_style_bg_color(s_pin_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pin_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_pin_panel, 0, 0);
    lv_obj_set_style_radius(s_pin_panel, 0, 0);

    /* Create a card in the center of the safe area */
    lv_obj_t * card = ui_create_card(s_pin_panel, 400, 480);
    lv_obj_center(card);

    lv_obj_t * label = lv_label_create(card);
    lv_label_set_text(label, for_admin_change ? "Set New Admin PIN" : "Admin Authentication");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t * sub_label = lv_label_create(card);
    lv_label_set_text(sub_label, "Enter 8-digit PIN");
    lv_obj_align_to(sub_label, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    s_pin_textarea = lv_textarea_create(card);
    lv_textarea_set_one_line(s_pin_textarea, true);
    lv_textarea_set_password_mode(s_pin_textarea, true);
    lv_textarea_set_max_length(s_pin_textarea, 8);
    lv_obj_set_width(s_pin_textarea, 250);
    lv_obj_align_to(s_pin_textarea, sub_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_add_state(s_pin_textarea, LV_STATE_FOCUSED);

    s_pin_keypad = lv_keyboard_create(card);
    lv_keyboard_set_mode(s_pin_keypad, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_pin_keypad, s_pin_textarea);
    lv_obj_set_size(s_pin_keypad, 350, 250);
    lv_obj_align(s_pin_keypad, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(s_pin_keypad, pin_keypad_event_cb, LV_EVENT_ALL, NULL);
}

void ui_close_pin_prompt(void) {
    if (s_pin_panel) {
        lv_obj_delete_async(s_pin_panel);
        s_pin_panel = NULL;
        s_pin_textarea = NULL;
        s_pin_keypad = NULL;
    }
}

extern "C" void ui_close_file_manager_screen(void);
extern "C" void ui_close_attendance_screen(void);
void ui_close_reports_screen(void);
void ui_close_settings_screen(void);
void ui_close_enrollment_screen(void);

void ui_return_to_main(void) {
    static bool inside_return = false;
    if (inside_return) return;
    inside_return = true;

    /* Hide keyboard if visible */
    ui_hide_keyboard();
    
    /* Hide notification if visible */
    ui_hide_notification();
    ui_hide_quick_settings();
    if (s_recents_panel) {
        lv_obj_delete_async(s_recents_panel);
        s_recents_panel = NULL;
    }
    
    lv_obj_t* current = lv_scr_act();
    if (current != s_main_screen) {
        /* Transition safely back to main first */
        lv_scr_load(s_main_screen);
        
        /* Clean up and destroy screens to free memory/task handles */
        ui_close_file_manager_screen();
        ui_close_attendance_screen();
        ui_close_reports_screen();
        ui_close_settings_screen();
        ui_close_enrollment_screen();
    }

    inside_return = false;
}

/*============================================================================
   Keyboard Input Dialog
 *===========================================================================*/

static void (*s_kb_input_callback)(const char* text) = NULL;
static lv_obj_t* s_kb_input_panel = NULL;
static lv_obj_t* s_kb_input_ta = NULL;

static void kb_input_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (code == LV_EVENT_READY && s_kb_input_callback) {
            s_kb_input_callback(lv_textarea_get_text(s_kb_input_ta));
        }
        if (s_kb_input_panel) {
            lv_obj_del(s_kb_input_panel);
            s_kb_input_panel = NULL;
        }
    }
}

void ui_show_keyboard_input(const char* title, const char* initial_text, bool is_password, void (*callback)(const char* text)) {
    s_kb_input_callback = callback;
    
    /* Overlay covers only the safe area: Y=40, h=500 → bottom at Y=540. */
    s_kb_input_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_kb_input_panel, DISPLAY_WIDTH, 500);
    lv_obj_set_pos(s_kb_input_panel, 0, 40);
    lv_obj_set_style_bg_color(s_kb_input_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_kb_input_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_kb_input_panel, 0, 0);
    lv_obj_set_style_radius(s_kb_input_panel, 0, 0);
    
    lv_obj_t* card = ui_create_card(s_kb_input_panel, 400, 290);
    lv_obj_set_pos(card, (DISPLAY_WIDTH - 400) / 2, 10);
    
    lv_obj_t* label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
    
    s_kb_input_ta = lv_textarea_create(card);
    lv_obj_set_size(s_kb_input_ta, 350, 45);
    lv_obj_align(s_kb_input_ta, LV_ALIGN_CENTER, 0, -40);
    lv_textarea_set_text(s_kb_input_ta, initial_text);
    lv_textarea_set_password_mode(s_kb_input_ta, is_password);
    lv_textarea_set_one_line(s_kb_input_ta, true);
    
    /* Keyboard sits at Y=310 within the overlay, ending at Y=490 (within safe area). */
    lv_obj_t* kb = lv_keyboard_create(s_kb_input_panel);
    lv_obj_set_size(kb, DISPLAY_WIDTH, 180);
    lv_obj_set_pos(kb, 0, 310);
    lv_keyboard_set_textarea(kb, s_kb_input_ta);
    lv_obj_add_event_cb(kb, kb_input_event_cb, LV_EVENT_ALL, NULL);
}

/*============================================================================
   Quick Settings & Notification Shade
 *===========================================================================*/

void ui_show_quick_settings(void) {
    ui_acquire();
    if (s_qs_panel) {
        ui_release();
        return;
    }
    
    s_qs_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_qs_panel, DISPLAY_WIDTH - 40, 490);
    lv_obj_set_pos(s_qs_panel, 20, 45);
    lv_obj_set_style_bg_color(s_qs_panel, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(s_qs_panel, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_qs_panel, 16, 0);
    lv_obj_set_style_border_width(s_qs_panel, 2, 0);
    lv_obj_set_style_border_color(s_qs_panel, lv_color_hex(0x444444), 0);
    lv_obj_set_style_shadow_width(s_qs_panel, 20, 0);
    
    /* Header Row */
    lv_obj_t* title = lv_label_create(s_qs_panel);
    lv_label_set_text(title, "Quick Settings & Status");
    lv_obj_set_pos(title, 20, 15);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    
    /* Connect Wi-Fi Shortcut Button */
    lv_obj_t* wifi_btn = lv_btn_create(s_qs_panel);
    lv_obj_set_size(wifi_btn, 180, 40);
    lv_obj_set_pos(wifi_btn, DISPLAY_WIDTH - 330, 10);
    lv_obj_set_style_bg_color(wifi_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_t* wifi_btn_lbl = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_btn_lbl, LV_SYMBOL_WIFI " Connect Wi-Fi");
    lv_obj_center(wifi_btn_lbl);
    lv_obj_add_event_cb(wifi_btn, [](lv_event_t* e) {
        ui_hide_quick_settings();
        ui_show_pin_prompt(false, [](bool success) {
            if (success) ui_show_settings_screen();
        });
    }, LV_EVENT_CLICKED, NULL);

    /* Settings Shortcut Button */
    lv_obj_t* set_btn = lv_btn_create(s_qs_panel);
    lv_obj_set_size(set_btn, 60, 40);
    lv_obj_set_pos(set_btn, DISPLAY_WIDTH - 140, 10);
    lv_obj_set_style_bg_color(set_btn, lv_color_hex(0x333333), 0);
    lv_obj_t* set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, LV_SYMBOL_SETTINGS);
    lv_obj_center(set_lbl);
    lv_obj_add_event_cb(set_btn, [](lv_event_t* e) {
        ui_hide_quick_settings();
        ui_show_pin_prompt(false, [](bool success) {
            if (success) ui_show_settings_screen();
        });
    }, LV_EVENT_CLICKED, NULL);
    
    /* Wi-Fi Info */
    lv_obj_t* wifi_lbl = lv_label_create(s_qs_panel);
    bool wifi_ok = (wifi_manager_get_status() == WIFI_STATUS_CONNECTED);
    lv_label_set_text_fmt(wifi_lbl, "%s Wi-Fi: %s (%d dBm)", LV_SYMBOL_WIFI, wifi_ok ? "Connected" : "Disconnected", wifi_manager_get_rssi());
    lv_obj_set_pos(wifi_lbl, 20, 60);
    lv_obj_set_style_text_color(wifi_lbl, wifi_ok ? lv_color_hex(0x4CAF50) : lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_16, 0);
    
    /* Start Sync Button */
    lv_obj_t* sync_btn = lv_btn_create(s_qs_panel);
    lv_obj_set_size(sync_btn, 160, 45);
    lv_obj_set_pos(sync_btn, 20, 100);
    lv_obj_set_style_bg_color(sync_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_t* sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, LV_SYMBOL_REFRESH " Start Sync");
    lv_obj_center(sync_lbl);
    lv_obj_add_event_cb(sync_btn, [](lv_event_t* e) {
        cloud_sync_start();
        ui_show_notification(NOTIFY_SUCCESS, "Sync", "Cloud synchronization started", 3000);
    }, LV_EVENT_CLICKED, NULL);

    /* Battery & SD Status */
    lv_obj_t* sys_lbl = lv_label_create(s_qs_panel);
    lv_label_set_text_fmt(sys_lbl, "%s SD Card: %s", LV_SYMBOL_SD_CARD, s_sd_card_ok ? "Mounted OK" : "Not Detected");
    lv_obj_set_pos(sys_lbl, 220, 110);
    lv_obj_set_style_text_color(sys_lbl, s_sd_card_ok ? lv_color_hex(0x4CAF50) : lv_color_hex(0xF44336), 0);
    lv_obj_set_style_text_font(sys_lbl, &lv_font_montserrat_16, 0);
    
    /* Brightness Slider */
    lv_obj_t* bri_lbl = lv_label_create(s_qs_panel);
    lv_label_set_text(bri_lbl, LV_SYMBOL_EYE_OPEN " Screen Brightness");
    lv_obj_set_pos(bri_lbl, 20, 160);
    lv_obj_set_style_text_color(bri_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bri_lbl, &lv_font_montserrat_16, 0);
    
    lv_obj_t* bri_slider = lv_slider_create(s_qs_panel);
    lv_slider_set_range(bri_slider, 10, 100);
    lv_slider_set_value(bri_slider, ui_get_brightness(), LV_ANIM_OFF);
    lv_obj_set_size(bri_slider, DISPLAY_WIDTH - 120, 20);
    lv_obj_set_pos(bri_slider, 20, 190);
    lv_obj_add_event_cb(bri_slider, [](lv_event_t* e) {
        int val = (int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
        ui_set_brightness(val);
    }, LV_EVENT_VALUE_CHANGED, NULL);
    
    /* Notifications Header */
    lv_obj_t* notify_hdr = lv_label_create(s_qs_panel);
    lv_label_set_text(notify_hdr, LV_SYMBOL_BELL " Recent Notifications");
    lv_obj_set_pos(notify_hdr, 20, 230);
    lv_obj_set_style_text_color(notify_hdr, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_text_font(notify_hdr, &lv_font_montserrat_16, 0);

    /* Notification List Container */
    lv_obj_t* notify_box = lv_obj_create(s_qs_panel);
    lv_obj_set_size(notify_box, DISPLAY_WIDTH - 120, 110);
    lv_obj_set_pos(notify_box, 20, 260);
    lv_obj_set_style_bg_color(notify_box, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(notify_box, 1, 0);
    lv_obj_set_style_border_color(notify_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(notify_box, 5, 0);
    lv_obj_set_flex_flow(notify_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(notify_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    if (s_notify_count == 0) {
        lv_obj_t* empty = lv_label_create(notify_box);
        lv_label_set_text(empty, "No recent notifications");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
    } else {
        for (int i = 0; i < s_notify_count; i++) {
            lv_obj_t* item = lv_label_create(notify_box);
            lv_label_set_text_fmt(item, "[%s] %s", s_notify_history[i].title, s_notify_history[i].message);
            lv_obj_set_style_text_color(item, lv_color_hex(0xDDDDDD), 0);
            lv_obj_set_style_text_font(item, &lv_font_montserrat_14, 0);
        }
    }

    /* Close Button / Bottom Bar */
    lv_obj_t* close_bar = lv_btn_create(s_qs_panel);
    lv_obj_set_size(close_bar, DISPLAY_WIDTH - 120, 40);
    lv_obj_set_pos(close_bar, 20, 440);
    lv_obj_set_style_bg_color(close_bar, lv_color_hex(0x444444), 0);
    lv_obj_t* close_lbl = lv_label_create(close_bar);
    lv_label_set_text(close_lbl, LV_SYMBOL_UP " Click or Swipe Up to Close");
    lv_obj_center(close_lbl);
    
    lv_obj_add_event_cb(close_bar, [](lv_event_t* e) {
        ui_hide_quick_settings();
    }, LV_EVENT_CLICKED, NULL);
    
    lv_obj_add_event_cb(s_qs_panel, [](lv_event_t* e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_TOP) {
            ui_hide_quick_settings();
        }
    }, LV_EVENT_GESTURE, NULL);
    
    ui_release();
}

void ui_hide_quick_settings(void) {
    if (!ui_acquire()) return;
    if (s_qs_panel) {
        lv_obj_delete_async(s_qs_panel);
        s_qs_panel = NULL;
    }
    ui_release();
}

/*============================================================================
   Recent Apps Popup
 *===========================================================================*/

void ui_show_recent_apps(void) {
    ui_acquire();
    if (s_recents_panel) {
        lv_obj_delete_async(s_recents_panel);
        s_recents_panel = NULL;
        ui_release();
        return;
    }
    
    if (s_recent_apps[0] == -1) {
        ui_show_notification(NOTIFY_INFO, "Recent Apps", "No recently used applications", 2000);
        ui_release();
        return;
    }
    
    s_recents_panel = lv_obj_create(s_main_screen);
    lv_obj_set_size(s_recents_panel, 800, 360);
    lv_obj_center(s_recents_panel);
    lv_obj_set_style_bg_color(s_recents_panel, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_bg_opa(s_recents_panel, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_recents_panel, 16, 0);
    lv_obj_set_style_border_width(s_recents_panel, 2, 0);
    lv_obj_set_style_border_color(s_recents_panel, lv_color_hex(0x555555), 0);
    lv_obj_set_style_shadow_width(s_recents_panel, 30, 0);
    
    lv_obj_t* title = lv_label_create(s_recents_panel);
    lv_label_set_text(title, "Recently Used Applications");
    lv_obj_set_pos(title, 20, 15);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    
    /* Clear All Button */
    lv_obj_t* clear_btn = lv_btn_create(s_recents_panel);
    lv_obj_set_size(clear_btn, 110, 40);
    lv_obj_set_pos(clear_btn, 610, 10);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0xAA4444), 0);
    lv_obj_t* clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, "Clear All");
    lv_obj_center(clear_lbl);
    lv_obj_add_event_cb(clear_btn, [](lv_event_t* e) {
        ui_acquire();
        for (int i = 0; i < 5; i++) {
            s_recent_apps[i] = -1;
        }
        
        /* Transition back to main screen first to ensure active views are safe */
        lv_obj_t* current = lv_scr_act();
        if (current != s_main_screen) {
            lv_screen_load(s_main_screen);
        }
        
        /* Purge and destroy ALL screens completely from RAM! */
        ui_close_file_manager_screen();
        ui_close_attendance_screen();
        ui_close_reports_screen();
        ui_close_settings_screen();
        ui_close_enrollment_screen();
        
        if (s_recents_panel) {
            lv_obj_delete_async(s_recents_panel);
            s_recents_panel = NULL;
        }
        ui_show_notification(NOTIFY_SUCCESS, "Recent Apps", "Cleared all apps from RAM", 2000);
        ui_release();
    }, LV_EVENT_CLICKED, NULL);

    /* Close Button */
    lv_obj_t* close_btn = lv_btn_create(s_recents_panel);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_set_pos(close_btn, 730, 10);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x444444), 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        if (s_recents_panel) {
            lv_obj_delete_async(s_recents_panel);
            s_recents_panel = NULL;
        }
    }, LV_EVENT_CLICKED, NULL);
    
    /* Container for Recent Cards */
    lv_obj_t* flex_box = lv_obj_create(s_recents_panel);
    lv_obj_set_size(flex_box, 750, 250);
    lv_obj_set_pos(flex_box, 20, 70);
    lv_obj_set_style_bg_opa(flex_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(flex_box, 0, 0);
    lv_obj_set_layout(flex_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(flex_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(flex_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    const char* app_names[] = {"Scanner", "Enroll", "Users", "Reports", "Settings", "Files"};
    const char* app_icons[] = {LV_SYMBOL_VIDEO, LV_SYMBOL_PLUS, LV_SYMBOL_LIST, LV_SYMBOL_FILE, LV_SYMBOL_SETTINGS, LV_SYMBOL_DIRECTORY};
    const uint32_t app_colors[] = {0x3A6EA5, 0x4CAF50, 0x00A8FF, 0xFF9800, 0x9E9E9E, 0x673AB7};
    
    for (int i = 0; i < 5; i++) {
        int app_id = s_recent_apps[i];
        if (app_id == -1) continue;
        
        lv_obj_t* card = lv_btn_create(flex_box);
        lv_obj_set_size(card, 135, 180);
        lv_obj_set_style_bg_color(card, lv_color_hex(app_colors[app_id]), 0);
        lv_obj_set_style_radius(card, 12, 0);
        
        lv_obj_t* icon_lbl = lv_label_create(card);
        lv_label_set_text(icon_lbl, app_icons[app_id]);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_48, 0);
        lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -20);
        
        lv_obj_t* name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, app_names[app_id]);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
        
        lv_obj_add_event_cb(card, [](lv_event_t* e) {
            int target_id = (int)(uintptr_t)lv_event_get_user_data(e);
            if (s_recents_panel) {
                lv_obj_delete_async(s_recents_panel);
                s_recents_panel = NULL;
            }
            launch_app_by_id(target_id);
        }, LV_EVENT_CLICKED, (void*)(uintptr_t)app_id);

        /* Close Individual App Button */
        lv_obj_t* close_app = lv_btn_create(card);
        lv_obj_set_size(close_app, 30, 30);
        lv_obj_align(close_app, LV_ALIGN_TOP_RIGHT, 5, -5);
        lv_obj_set_style_bg_color(close_app, lv_color_hex(0xAA3333), 0);
        lv_obj_set_style_radius(close_app, 15, 0);
        lv_obj_set_style_pad_all(close_app, 0, 0);
        
        lv_obj_t* close_icon = lv_label_create(close_app);
        lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
        lv_obj_center(close_icon);
        lv_obj_set_style_text_font(close_icon, &lv_font_montserrat_12, 0);
        
        lv_obj_add_event_cb(close_app, [](lv_event_t* e) {
            int idx = (int)(uintptr_t)lv_event_get_user_data(e);
            ui_acquire();
            int app_id = s_recent_apps[idx];
            if (app_id != -1) {
                s_recent_apps[idx] = -1;
                
                // Shift subsequent apps to fill the gap
                for (int j = idx; j < 4; j++) {
                    s_recent_apps[j] = s_recent_apps[j + 1];
                }
                s_recent_apps[4] = -1;
                
                // Transition safely to main screen if any app was active
                lv_obj_t* active = lv_scr_act();
                if (active != s_main_screen) {
                    lv_screen_load(s_main_screen);
                }
                
                // Close and purge from RAM
                if (app_id == 0) ui_close_attendance_screen();
                else if (app_id == 1) ui_close_enrollment_screen();
                else if (app_id == 2) ui_close_user_manager();
                else if (app_id == 3) ui_close_reports_screen();
                else if (app_id == 4) ui_close_settings_screen();
                else if (app_id == 5) ui_close_file_manager_screen();
            }
            
            if (s_recents_panel) {
                lv_obj_delete_async(s_recents_panel);
                s_recents_panel = NULL;
            }
            ui_show_notification(NOTIFY_SUCCESS, "Recent Apps", "Removed app from RAM", 1500);
            
            bool has_apps = false;
            for (int k = 0; k < 5; k++) {
                if (s_recent_apps[k] != -1) has_apps = true;
            }
            if (has_apps) {
                ui_release();
                ui_show_recent_apps();
            } else {
                ui_release();
            }
        }, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
    
    ui_release();
}

extern "C" void ui_load_screen_with_history(lv_obj_t* scr) {
    if (!scr) return;
    ui_acquire();
    lv_obj_t* active = lv_scr_act();
    if (active && active != scr && active != s_main_screen) {
        bool exists = false;
        for (int i = 0; i < s_history_depth; i++) {
            if (s_screen_history[i] == active) { exists = true; break; }
        }
        if (!exists && s_history_depth < 10) {
            s_screen_history[s_history_depth++] = active;
        }
    } else if (scr == s_main_screen) {
        s_history_depth = 0;
    }
    
    lv_screen_load(scr);
    ui_release();
}

static void screen_bg_click_event_cb(lv_event_t * e) {
    static uint32_t last_tap = 0;
    static bool screen_on = true;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    if (now - last_tap < 400) {
        if (screen_on) {
            backlight_set(0);
            screen_on = false;
        } else {
            backlight_set(ui_get_brightness());
            screen_on = true;
        }
        last_tap = 0;
    } else {
        last_tap = now;
    }
}

void ui_add_double_tap_to_screen(lv_obj_t* scr) {
    if (scr) {
        lv_obj_add_event_cb(scr, screen_bg_click_event_cb, LV_EVENT_CLICKED, NULL);
    }
}
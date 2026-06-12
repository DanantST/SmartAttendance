/**
 * @file ui_main.h
 * @brief Main UI interface for Smart Attendance System
 * Contains LVGL screen management, theme toggle, notification panel, and navigation
 */

#ifndef UI_MAIN_H
#define UI_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

void ui_load_screen_with_history(struct _lv_obj_t* scr);
#ifdef lv_scr_load
#undef lv_scr_load
#endif
#define lv_scr_load(scr) ui_load_screen_with_history((struct _lv_obj_t*)(scr))

/* Theme types */
typedef enum {
    THEME_LIGHT,
    THEME_DARK
} ui_theme_t;

/* Notification types */
typedef enum {
    NOTIFY_INFO,
    NOTIFY_SUCCESS,
    NOTIFY_WARNING,
    NOTIFY_ERROR
} ui_notify_type_t;

/* Navigation buttons */
typedef enum {
    NAV_HOME,
    NAV_ENROLL,
    NAV_SYNC,
    NAV_REPORTS,
    NAV_SETTINGS
} ui_nav_button_t;

/*====================
   Initialization
 *====================*/

/**
 * @brief Initialize UI system (LVGL, display, touch)
 * @return ESP_OK on success
 */
esp_err_t ui_init(void);

/**
 * @brief Get LVGL display handle
 * @return display handle
 */
lv_display_t* ui_get_display(void);

/**
 * @brief Get LVGL touch input handle
 * @return indev handle
 */
lv_indev_t* ui_get_touch(void);

/**
 * @brief Acquire LVGL thread-safety mutex
 */
bool ui_acquire(void);  /**< returns false on timeout (LVGL mutex stuck) */

/**
 * @brief Release LVGL thread-safety mutex
 */
void ui_release(void);

/*====================
   Theme Management
 *====================*/

/**
 * @brief Set UI theme (light or dark)
 * @param theme THEME_LIGHT or THEME_DARK
 */
void ui_set_theme(ui_theme_t theme);

/**
 * @brief Get current theme
 * @return current theme
 */
ui_theme_t ui_get_theme(void);

/**
 * @brief Toggle between light and dark themes
 */
void ui_theme_toggle(void);

/*====================
   Main Screen
 *====================*/

/**
 * @brief Get main screen object
 * @return main screen LVGL object
 */
lv_obj_t* ui_get_main_screen(void);

/**
 * @brief Update camera preview image
 * @param img pointer to image data (RGB565)
 * @param width image width
 * @param height image height
 */
void ui_update_camera_frame(uint8_t* img, int width, int height);

/**
 * @brief Show face detection bounding box
 * @param x bounding box x coordinate
 * @param y bounding box y coordinate
 * @param w bounding box width
 * @param h bounding box height
 * @param recognized true if face recognized, false if unknown
 */
void ui_update_detection_bounding_box(int x, int y, int w, int h, bool recognized);

/**
 * @brief Show recognition result overlay
 * @param name recognized name (NULL for unknown)
 * @param confidence confidence score (0-1)
 */
void ui_show_recognition_result(const char* name, float confidence);

/**
 * @brief Update attendance count display
 * @param count number of students marked present today
 */
void ui_update_attendance_count(int count);

/*====================
   Notification Panel
 *====================*/

/**
 * @brief Show a notification in the drop-down panel
 * @param type notification type (info, success, warning, error)
 * @param title notification title
 * @param message notification message
 * @param timeout_ms auto-dismiss timeout (0 = manual dismiss)
 */
void ui_show_notification(ui_notify_type_t type, const char* title, const char* message, uint32_t timeout_ms);

/**
 * @brief Hide notification panel
 */
void ui_hide_notification(void);

/**
 * @brief Check if notification panel is visible
 * @return true if visible
 */
bool ui_is_notification_visible(void);

/*====================
   Navigation Bar
 *====================*/

/**
 * @brief Set active navigation button
 * @param button which button to highlight
 */
void ui_set_active_nav_button(ui_nav_button_t button);

/**
 * @brief Register navigation button callback
 * @param callback function to call when navigation button pressed
 */
void ui_register_nav_callback(void (*callback)(ui_nav_button_t button));

/*====================
   Status Bar
 *====================*/

/**
 * @brief Update battery percentage display
 * @param percent battery level (0-100)
 * @param charging true if currently charging
 */
void ui_set_battery_percent(int percent, bool charging);

/**
 * @brief Update Wi-Fi status icon
 * @param connected true if connected to Wi-Fi
 * @param rssi signal strength (if connected)
 */
void ui_set_wifi_status(bool connected, int rssi);

/**
 * @brief Update sync status icon
 * @param syncing true if sync in progress
 */
void ui_set_sync_status(bool syncing);

/**
 * @brief Update time display
 * @param hour hour (0-23)
 * @param minute minute (0-59)
 */
void ui_update_time(int hour, int minute);

/**
 * @brief Update date display
 * @param day day of month
 * @param month month (1-12)
 * @param year year
 */
void ui_update_date(int day, int month, int year);

/**
 * @brief Set SD card status and show/hide warning banner
 * @param ok true if SD card detected and DB initialized
 */
void ui_set_sd_card_status(bool ok);

/**
 * @brief Show the first-time setup wizard
 */
void ui_show_setup_wizard(void);


/*====================
   Enrollment Screen
 *====================*/

/**
 * @brief Show enrollment screen
 */
void ui_show_enrollment_screen(void);

/**
 * @brief Update enrollment progress
 * @param current current frames captured
 * @param total total frames needed
 */
void ui_update_enrollment_progress(int current, int total);

/**
 * @brief Update enrollment frame capture feedback
 * @param quality quality score of captured frame (0-1)
 */
void ui_update_enrollment_frame_captured(float quality);

/**
 * @brief Show pose guidance message
 * @param message text to display (e.g., "Tilt head left")
 */
void ui_show_pose_guidance(const char* message);

/**
 * @brief Close enrollment screen and return to main
 */
void ui_close_enrollment_screen(void);

/*====================
   Attendance Screen
 *====================*/

/**
 * @brief Show attendance screen
 */
void ui_show_attendance_screen(void);

/**
 * @brief Close attendance screen
 */
void ui_close_attendance_screen(void);

/*====================
   Sound Recorder
 *====================*/

/**
 * @brief Show sound recorder screen
 */
void ui_show_recorder_screen(void);

/**
 * @brief Close sound recorder screen
 */
void ui_close_recorder_screen(void);

/*====================
   Audio Player
 *====================*/

/**
 * @brief Show audio player screen
 */
void ui_show_player_screen(void);

/**
 * @brief Close audio player screen
 */
void ui_close_player_screen(void);

/*====================
   File Manager
 *====================*/

/**
 * @brief Show file manager screen
 */
void ui_show_file_manager_screen(void);

/**
 * @brief Close file manager screen
 */
void ui_close_file_manager_screen(void);

/*====================
   Settings Screen
 *====================*/

/**
 * @brief Show settings screen
 */
void ui_show_settings_screen(void);

/**
 * @brief Close settings screen
 */
void ui_close_settings_screen(void);

/**
 * @brief Get current brightness setting
 * @return brightness percent (0-100)
 */
int ui_get_brightness(void);

/**
 * @brief Set brightness (updates hardware via board_backlight_set)
 * @param percent brightness (0-100)
 */
void ui_set_brightness(int percent);

/*====================
   Reports Screen
 *====================*/

/**
 * @brief Show reports screen
 */
void ui_show_reports_screen(void);

/**
 * @brief Close reports screen
 */
void ui_close_reports_screen(void);

/**
 * @brief Populate course list in reports screen
 * @param courses array of course names
 * @param count number of courses
 */
void ui_reports_populate_courses(const char** courses, int count);

/**
 * @brief Show attendance report data
 * @param data CSV or formatted data
 * @param len data length
 */
void ui_reports_show_data(const char* data, size_t len);

/*====================
   Keyboard (Pop-up)
 *====================*/

/**
 * @brief Show pop-up keyboard for text input
 * @param textarea textarea widget to attach keyboard to
 * @param title keyboard title (optional)
 */
void ui_show_keyboard(lv_obj_t* textarea, const char* title);

/**
 * @brief Hide pop-up keyboard
 */
void ui_hide_keyboard(void);
void ui_show_keyboard_input(const char* title, const char* initial_text, bool is_password, void (*callback)(const char* text));

/**
 * @brief Show PIN entry prompt (numeric keypad)
 * @param for_admin_change true if used for changing admin PIN, false for access
 * @param callback function to call with result (true = authenticated)
 */
void ui_show_pin_prompt(bool for_admin_change, void (*callback)(bool success));

/**
 * @brief Close PIN entry prompt
 */
void ui_close_pin_prompt(void);

/*====================
   Utility Functions
 *====================*/

/**
 * @brief Create a styled button with modern appearance
 * @param parent parent object
 * @param text button label
 * @param width button width
 * @param height button height
 * @return button object
 */
lv_obj_t* ui_create_button(lv_obj_t* parent, const char* text, int width, int height);

/**
 * @brief Create a card container with shadow
 * @param parent parent object
 * @param width card width
 * @param height card height
 * @return container object
 */
lv_obj_t* ui_create_card(lv_obj_t* parent, int width, int height);

/**
 * @brief Return to main screen (close any active sub-screen)
 */
void ui_return_to_main(void);


/**
 * @brief Add double tap listener to an empty screen background to toggle backlight
 * @param scr The screen object to attach the listener to
 */
void ui_add_double_tap_to_screen(lv_obj_t* scr);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UI_MAIN_H */
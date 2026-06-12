/**
 * @file config.h
 * @brief System-wide configuration definitions for Smart Attendance System
 * @version 1.2  (audio + BLE subsystems removed 2026-06-12; Web AP enrollment only)
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"

/* ==================== System Version ==================== */
#define SYSTEM_VERSION_MAJOR    1
#define SYSTEM_VERSION_MINOR    0
#define SYSTEM_VERSION_PATCH    0
#define SYSTEM_NAME             "SmartAttendance"
#define SYSTEM_HARDWARE         "CrowPanel-Advanced-7-P4"

/* ==================== Task Configuration ==================== */
#define TASK_CAMERA_PRIORITY        7    /* Below LVGL(10) so rendering is never starved */
#define TASK_CAMERA_STACK_SIZE      8192
#define TASK_DETECTION_PRIORITY     8
#define TASK_DETECTION_STACK_SIZE   16384
#define TASK_UI_PRIORITY            7
#define TASK_UI_STACK_SIZE          8192
/* TASK_BLE_* removed — BLE subsystem stripped 2026-06-12 */
#define TASK_NETWORK_PRIORITY       5
#define TASK_NETWORK_STACK_SIZE     16384
#define TASK_BATTERY_PRIORITY       4
#define TASK_BATTERY_STACK_SIZE     4096
#define TASK_DB_PRIORITY            6
#define TASK_DB_STACK_SIZE          8192

/* ==================== Camera Configuration ==================== */
/* NOTE: ESP32-P4 CSI outputs RGB565. The face detector must accept RGB565. */
#define CAMERA_PIXEL_FORMAT         PIXFORMAT_RGB565
#define CAMERA_FRAME_SIZE           FRAMESIZE_QVGA   // 320x240 for detection
#define CAMERA_JPEG_QUALITY         10
#define CAMERA_FB_COUNT             2
#define CAMERA_XCLK_FREQ            20000000         // 20 MHz

/* For enrollment high-quality capture */
#define CAMERA_ENROLL_FRAME_SIZE    FRAMESIZE_VGA    // 640x480

/* Autofocus timing */
#define AF_TRIGGER_TIMEOUT_MS       500
#define AF_SETTLE_TIME_MS           50

/* ==================== Face Recognition Configuration ==================== */
#define FACE_ALIGN_SIZE             112              // 112x112 aligned face
#define EMBEDDING_DIM               128              // 128-dimensional embedding
#define RECOGNITION_THRESHOLD       0.65f            // Cosine similarity threshold
#define EMBEDDING_CACHE_SIZE        500              // Max cached users

/* Face detection thresholds */
#define FACE_DETECT_CONFIDENCE_MIN  0.6f             // Minimum detection confidence
#define FACE_MIN_SIZE_PX            50               // Minimum face bounding box size in pixels

/* Multi-frame enrollment */
#define ENROLL_FRAMES_TOTAL         30               // Total frames to capture
#define ENROLL_FRAMES_KEEP          15               // Top quality frames to keep
#define ENROLL_SHARPNESS_MIN        50.0f            // Minimum Laplacian variance
#define ENROLL_BRIGHTNESS_MIN       40               // 0-255, min average luminance
#define ENROLL_BRIGHTNESS_MAX       200              // Max average luminance
#define ENROLL_YAW_MAX_DEG          25.0f            // Max head yaw angle

/* ==================== Audio Configuration ==================== */
/* Audio subsystem removed 2026-06-11. All defines below are retained as
 * tombstones only to avoid breaking any third-party code that may reference
 * them. They have no effect on firmware behaviour. */
// #define AUDIO_SAMPLE_RATE  (removed)
// #define AUDIO_PROMPTS_PATH (removed)

/* ==================== Database Configuration ==================== */
#define DB_PATH                     "/sdcard/attendance.db"
#define DB_BACKUP_PATH              "/sdcard/attendance_backup.db"
#define DB_PAGE_SIZE                4096
#define DB_CACHE_SIZE               2000
#define DB_SYNC_INTERVAL_MS         3600000          // 1 hour

/* ==================== Enrollment Queue (Web AP Portal) ==================== */
/* Students self-register via the Wi-Fi SoftAP captive portal.                 */
/* BLE/NimBLE removed 2026-06-12. Queue is purely in-memory + mutex-protected. */

/* Admin-configured device identity — set in Settings → Device Info */
#define DEVICE_DEPARTMENT           "Computer Science"  /* Default; overrideable in NVS */
#define DEVICE_LOCATION             "Lab Block A"       /* Default; overrideable in NVS */

/* ==================== Network Configuration ==================== */
#define WIFI_MAX_RETRY              5
#define WIFI_CONNECT_TIMEOUT_MS     10000
#define CLOUD_SYNC_INTERVAL_MS      21600000         // 6 hours
#define CLOUD_API_TIMEOUT_MS        15000
#define CLOUD_API_RETRY_DELAY_MS    60000            // 1 minute

/* Telegram Bot Configuration */
#define TELEGRAM_BOT_TOKEN          "YOUR_BOT_TOKEN"
#define TELEGRAM_CHAT_ID            "YOUR_CHAT_ID"

/* ==================== Power Management ==================== */
#define BATTERY_ADC_CHANNEL         ADC_CHANNEL_4    // GPIO4 - verify with schematic
#define BATTERY_VOLTAGE_MAX         4.2f             // Fully charged
#define BATTERY_VOLTAGE_MIN         3.3f             // Empty
#define BATTERY_WARNING_THRESHOLD   15               // Percentage
#define BATTERY_SHUTDOWN_THRESHOLD  5                // Percentage
#define BATTERY_CHECK_INTERVAL_MS   10000            // 10 seconds

/* ==================== UI Configuration ==================== */
#define DISPLAY_WIDTH               1024
#define DISPLAY_HEIGHT              600
#define DISPLAY_COLOR_DEPTH         16
#define UI_THEME_DEFAULT            1       // 0=Light, 1=Dark
#define UI_ANIMATION_DURATION_MS    300

/* ==================== Event Queue ==================== */
#define SYSTEM_EVENT_QUEUE_SIZE     50
#define CAMERA_FRAME_QUEUE_SIZE     1
#define DB_REQUEST_QUEUE_SIZE       20
/* AUDIO_REQUEST_QUEUE_SIZE removed — audio subsystem stripped */

/* ==================== Logging ==================== */
#define LOG_ENABLE                  1
#define LOG_LEVEL                   ESP_LOG_WARN   // DEBUG, INFO, WARN, ERROR

/* ==================== Feature Flags ==================== */
/* Use integer 1/0 rather than true/false for safe preprocessor #if usage */
/* ENABLE_AUDIO_GUIDANCE removed — audio subsystem fully stripped 2026-06-11 */
/* ENABLE_BLE_ENROLLMENT removed — BLE subsystem fully stripped 2026-06-12 */
#define ENABLE_CLOUD_SYNC           1
#define ENABLE_BATTERY_MONITOR      1
 
 /* ==================== System States ==================== */
typedef enum {
    SYSTEM_STATE_NORMAL,        // Normal operation: recognition mode
    SYSTEM_STATE_ENROLLMENT,    // User enrollment mode
    SYSTEM_STATE_SYNCING,       // Cloud sync in progress
    SYSTEM_STATE_SETTINGS,      // Settings menu
    SYSTEM_STATE_REPORTS,       // Reports view
    SYSTEM_STATE_LOW_BATTERY,   // Low battery warning
    SYSTEM_STATE_SHUTDOWN       // Shutting down
} system_state_t;

system_state_t get_system_state(void);
void set_system_state(system_state_t new_state);


#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */
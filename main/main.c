/**
 * @file main.c
 * @brief Main entry point and system orchestrator for Smart Attendance System
 *
 * Subsystems: Camera, Face Detection/Recognition, LVGL UI, Web AP Enrollment,
 * Cloud Sync, Battery Monitoring.
 * Audio subsystem removed 2026-06-11. BLE/NimBLE removed 2026-06-12.
 */

#include <stdio.h>
#include <string.h>
#include "lvgl.h"  /* Required for lv_tick_inc and lv_task_handler */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "driver/gpio.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted.h"
#endif

#include "config.h"
#include "boards/elecrow_p4_board.h"
#include "camera/camera_driver.h"
#include "detection/face_detector.h"
#include "recognition/recognizer.h"
#include "database/db_manager.h"
#include "ui/ui_main.h"
#include "ui/ui_attendance.h"
#include "ui/ui_enrollment.h"
#include "ble/ble_registration.h"
#include "network/wifi_manager.h"
#include "network/wifi_ap_portal.h"
#include "network/cloud_sync.h"
#include "power/battery_monitor.h"
#include "utils/queue_manager.h"
#include "storage/sdcard_mount.h"
#include "network/wifi_manager.h"
#include "network/cloud_sync.h"
#include "network/sntp_sync.h"
#include "esp_task_wdt.h"
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "esp_timer.h"
#include "esp_random.h"

static const char *TAG = "MAIN";

/* Forward declarations */

static system_state_t g_system_state = SYSTEM_STATE_NORMAL;
static SemaphoreHandle_t g_state_mutex = NULL;  /* Issue 3.8: mutex for g_system_state */
EventGroupHandle_t g_system_event_group;

/* Admin session tracking */
static bool g_is_admin_active = false;
static int64_t g_admin_session_expiry_us = 0;
#define ADMIN_SESSION_TIMEOUT_US (5 * 60 * 1000 * 1000LL) /* 5 minutes */
#define DEFAULT_ADMIN_PIN "1234"

volatile bool g_enrollment_cancel = false;
volatile char g_enrollment_role_override[16] = {0};
volatile bool g_wizard_admin_enrolled = false;  /* [T1-3] set by start_enrollment_task for wizard gating */

/* Thread-safe state access helpers (Issue 3.8) */
system_state_t get_system_state(void) {
    system_state_t s;
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    s = g_system_state;
    xSemaphoreGive(g_state_mutex);
    return s;
}

void set_system_state(system_state_t new_state) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_system_state = new_state;
    xSemaphoreGive(g_state_mutex);
}

/* Thread-safe admin status check */
static bool is_admin_session_valid(void) {
    if (!g_is_admin_active) return false;
    if (esp_timer_get_time() > g_admin_session_expiry_us) {
        g_is_admin_active = false;
        return false;
    }
    return true;
}

static void activate_admin_session(void) {
    g_is_admin_active = true;
    g_admin_session_expiry_us = esp_timer_get_time() + ADMIN_SESSION_TIMEOUT_US;
    ESP_LOGI(TAG, "Admin session activated");
}

bool verify_admin_pin(const char *input_pin) {
    char stored_pin[16] = DEFAULT_ADMIN_PIN;
    size_t size = sizeof(stored_pin);
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        if (nvs_get_str(nvs, "admin_pin", stored_pin, &size) != ESP_OK) {
            /* If not set, use default and save it */
            nvs_set_str(nvs, "admin_pin", DEFAULT_ADMIN_PIN);
            nvs_commit(nvs);
        }
        nvs_close(nvs);
    }
    
    if (strcmp(input_pin, stored_pin) == 0) {
        activate_admin_session();
        return true;
    }
    return false;
}

/* Queues for inter-task communication (defined in queue_manager.c) */
QueueHandle_t g_camera_frame_queue = NULL;
QueueHandle_t g_system_event_queue = NULL;
QueueHandle_t g_db_request_queue = NULL;
/* Camera task handle — used to suspend/resume the task cleanly during enrollment
 * instead of a busy-poll flag that would spin at priority 10 and starve LVGL. */
static TaskHandle_t s_camera_task_handle = NULL;
static uint8_t *s_detection_fb_buf = NULL;

/* ---------------------------------------------------------------------------
 * In-memory attendance dedup table
 * Prevents duplicate inserts caused by the async DB queue: when a student
 * scans repeatedly in quick succession the DB write may not have committed
 * yet, so the SQL check would return false multiple times. This table is
 * updated IMMEDIATELY (before the queue send), acting as a reliable gate.
 * --------------------------------------------------------------------------- */
#define SESSION_DEDUP_MAX 200   /* supports up to 200 unique student+class combos */
typedef struct { 
    uint32_t user_id; 
    uint32_t schedule_id; 
    int scan_count;
} session_key_t;
static session_key_t s_session_dedup[SESSION_DEDUP_MAX];
static int           s_session_dedup_count = 0;
static portMUX_TYPE  s_session_mux = portMUX_INITIALIZER_UNLOCKED;

static bool session_already_recorded(uint32_t user_id, uint32_t schedule_id, bool is_test_or_exam) {
    bool found = false;
    portENTER_CRITICAL(&s_session_mux);
    for (int i = 0; i < s_session_dedup_count; i++) {
        if (s_session_dedup[i].user_id == user_id &&
            s_session_dedup[i].schedule_id == schedule_id) {
            int max_allowed = is_test_or_exam ? 2 : 1;
            if (s_session_dedup[i].scan_count >= max_allowed) {
                found = true;
            }
            break;
        }
    }
    portEXIT_CRITICAL(&s_session_mux);
    return found;
}

static void session_record_attendance(uint32_t user_id, uint32_t schedule_id) {
    portENTER_CRITICAL(&s_session_mux);
    bool found = false;
    for (int i = 0; i < s_session_dedup_count; i++) {
        if (s_session_dedup[i].user_id == user_id &&
            s_session_dedup[i].schedule_id == schedule_id) {
            s_session_dedup[i].scan_count++;
            found = true;
            break;
        }
    }
    if (!found && s_session_dedup_count < SESSION_DEDUP_MAX) {
        s_session_dedup[s_session_dedup_count].user_id     = user_id;
        s_session_dedup[s_session_dedup_count].schedule_id = schedule_id;
        s_session_dedup[s_session_dedup_count].scan_count   = 1;
        s_session_dedup_count++;
    }
    portEXIT_CRITICAL(&s_session_mux);
}

#define SYSTEM_EVENT_RECOGNITION_SUCCESS   (1 << 0)
#define SYSTEM_EVENT_RECOGNITION_FAIL      (1 << 1)
#define SYSTEM_EVENT_ENROLLMENT_COMPLETE   (1 << 2)
#define SYSTEM_EVENT_ENROLLMENT_FAIL       (1 << 3)
#define SYSTEM_EVENT_SYNC_COMPLETE         (1 << 4)
#define SYSTEM_EVENT_BATTERY_LOW           (1 << 5)
#define SYSTEM_EVENT_BATTERY_CRITICAL      (1 << 6)
#define SYSTEM_EVENT_BUTTON_PRESS           (1 << 7)
#define SYSTEM_EVENT_TOUCH_MENU            (1 << 8)
/* [Fix M2] Set by cloud_sync_task when it finishes, waited on by network_sync_task */
#define SYSTEM_EVENT_CLOUD_SYNC_DONE       (1 << 9)

/**
 * @brief System event structure for inter-task communication
 */
typedef struct {
    uint32_t event_id;
    void *data;
    size_t data_len;
} system_event_t;

/**
 * @brief Camera frame passed through the detection queue.
 *
 * AR-8: camera_fb_t is embedded BY VALUE so xQueueSend copies it atomically
 * into the queue item. This eliminates the data race that existed when a
 * static camera_fb_t was updated by camera_task while detection_recognition_task
 * was still reading the previous frame's metadata through the old pointer.
 * `buf` inside the copy still points to s_detection_fb_buf (stable heap memory).
 */
typedef struct {
    camera_fb_t  fb;           /* AR-8: embedded by value — NOT a pointer */
    uint32_t     timestamp_ms;
} camera_frame_t;

/* Forward declarations */
static void camera_task(void *pvParameters);
static void detection_recognition_task(void *pvParameters);
static void db_task(void *pvParameters);
static void network_sync_task(void *pvParameters);
static void battery_task(void *pvParameters);
static void system_state_machine(void);
static esp_err_t process_recognition_result(user_t *user, float confidence);
void start_enrollment_task(void *pvParam);
/* Screen deferred for safe deletion from admin setup wizard (ui_admin_setup.cpp) */
extern lv_obj_t *g_admin_setup_screen_to_delete;
static void schedule_checker_task(void *pvParameters);
static void handle_low_battery(void);
static void graceful_shutdown(void);
static void pin_auth_callback(bool success);

/* Navigation target for PIN callback */
static ui_nav_button_t s_target_nav_button = NAV_HOME;

/* Helper: generate a hex UUID string (Issue 3.1) */
static void generate_uuid_hex(char *buf, size_t buf_size) {
    /* Generate a 128-bit random UUID formatted as hex (36 chars + null) */
    if (buf_size < 37) return;
    uint32_t r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = esp_random();
    }
    snprintf(buf, buf_size,
             "%08" PRIx32 "-%04" PRIx32 "-%04" PRIx32 "-%04" PRIx32 "-%08" PRIx32 "%04" PRIx32,
             r[0],
             (r[1] >> 16) & 0xFFFF,
             (r[2] >> 16 & 0x0FFF) | 0x4000,  /* Version 4 */
             ((r[2] >> 16) & 0x3FFF) | 0x8000,  /* Variant 1 */
             (r[2] & 0xFFFF) | (((r[3] >> 16) & 0xFFFF) << 16),
             r[3] & 0xFFFF);
}

static void pin_auth_callback(bool success) {
    if (success) {
        ESP_LOGI(TAG, "PIN authentication successful. Target: %d", (int)s_target_nav_button);
        ui_acquire();
        ui_show_notification(NOTIFY_SUCCESS, "Admin Active", "Session started", 3000);
        
        /* Proceed to target if requested */
        if (s_target_nav_button == NAV_ENROLL) {
            xTaskCreate(start_enrollment_task, "enrollment", TASK_DETECTION_STACK_SIZE, NULL, 5, NULL);
        } else if (s_target_nav_button == NAV_SETTINGS) {
            ESP_LOGI(TAG, "Opening settings screen...");
            ui_show_settings_screen();
        }
        ui_release();
    } else {
        ESP_LOGW(TAG, "PIN authentication failed");
        ui_acquire();
        ui_show_notification(NOTIFY_ERROR, "Access Denied", "Incorrect PIN", 3000);
        ui_release();
    }
    s_target_nav_button = NAV_HOME;
}

/* UI Navigation callback handler */
static void ui_nav_callback_handler(ui_nav_button_t button) {
    /* Handle long-press (Setup Trigger) */
    if ((int)button == 100) {
        if (!is_admin_session_valid()) {
            ui_acquire();
            ui_show_pin_prompt(false, pin_auth_callback);
            ui_release();
        } else {
            ui_acquire();
            ui_show_notification(NOTIFY_INFO, "Admin Active", "Already authenticated", 2000);
            ui_release();
        }
        return;
    }

    switch (button) {
        case NAV_ENROLL:
            if (get_system_state() == SYSTEM_STATE_NORMAL) {
                /* Always require PIN for destructive enroll action [T3-3] */
                s_target_nav_button = NAV_ENROLL;
                ui_acquire();
                ui_show_pin_prompt(false, pin_auth_callback);
                ui_release();
            }
            break;
        case NAV_SETTINGS:
            if (is_admin_session_valid()) {
                ui_acquire();
                ui_show_settings_screen();
                ui_release();
            } else {
                s_target_nav_button = NAV_SETTINGS;
                ui_acquire();
                ui_show_pin_prompt(false, pin_auth_callback);
                ui_release();
            }
            break;
        case NAV_SYNC:
            if (get_system_state() == SYSTEM_STATE_NORMAL) {
                /* Set event bit to trigger internal task immediately */
                xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_TOUCH_MENU);
                ui_acquire();
                if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
                    ui_show_notification(NOTIFY_INFO, "Sync Started", "Syncing with cloud...", 3000);
                } else {
                    ui_show_notification(NOTIFY_INFO, "Sync Started", "Connecting to Wi-Fi...", 3000);
                }
                ui_release();
            }
            break;
        case NAV_HOME:
            set_system_state(SYSTEM_STATE_NORMAL);
            break;
        default:
            break;
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Smart Attendance System v%d.%d.%d", 
             SYSTEM_VERSION_MAJOR, SYSTEM_VERSION_MINOR, SYSTEM_VERSION_PATCH);
    ESP_LOGI(TAG, "Hardware: %s", SYSTEM_HARDWARE);
    ESP_LOGI(TAG, "========================================");

    /* Watchdog configuration - increase timeout for AI inference */
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 20000,
        .idle_core_mask = (1 << 0), /* CPU0 IDLE */
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&twdt_config);

    /* Initialize NVS flash using secure init if configured */
    esp_err_t ret = ESP_OK;
    nvs_sec_cfg_t cfg;
    const esp_partition_t *key_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    esp_err_t err = (key_part != NULL) ? nvs_flash_read_security_cfg(key_part, &cfg) : ESP_ERR_NOT_FOUND;
    if (err == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        ESP_LOGI(TAG, "Generating NVS keys...");
        nvs_flash_generate_keys(key_part, &cfg);
    }
    ret = nvs_flash_secure_init(&cfg);

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corruption detected, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_secure_init(&cfg);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %d", ret);
        return;
    }

    /* Initialize board hardware (backlight, power, I2C) */
    ret = board_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed: %d", ret);
        return;
    }

    /* Initialize UI system early to show the booting screen */
    ret = ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI init failed");
        return;
    }
    ui_register_nav_callback(ui_nav_callback_handler);
    ESP_LOGI(TAG, "UI initialized (boot screen active)");

    ui_update_boot_status("Preparing system...", 5);

    /* Create state mutex (Issue 3.8) */
    g_state_mutex = xSemaphoreCreateMutex();
    if (!g_state_mutex) {
        ESP_LOGE(TAG, "State mutex creation failed");
        return;
    }

    /* Create queues */
    g_camera_frame_queue = xQueueCreate(CAMERA_FRAME_QUEUE_SIZE, sizeof(camera_frame_t));
    g_system_event_queue = xQueueCreate(SYSTEM_EVENT_QUEUE_SIZE, sizeof(system_event_t));
    g_db_request_queue = xQueueCreate(DB_REQUEST_QUEUE_SIZE, sizeof(db_request_t));
    g_system_event_group = xEventGroupCreate();

    if (!g_camera_frame_queue || !g_system_event_queue || !g_db_request_queue ||
        !g_system_event_group) {
        ESP_LOGE(TAG, "Queue creation failed");
        return;
    }

    /* Set event bit to trigger cloud sync cycle immediately on boot */
    xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_TOUCH_MENU);

    /* Allocate detection frame buffer in SPIRAM once (supports up to VGA size) */
    s_detection_fb_buf = (uint8_t *)heap_caps_malloc(640 * 480 * 2, MALLOC_CAP_SPIRAM);
    if (!s_detection_fb_buf) {
        ESP_LOGE(TAG, "Failed to allocate s_detection_fb_buf in SPIRAM");
        return;
    }

    /* Initialize subsystems in order */
    
    /* 1. Camera (must be first for detection) */
    ui_update_boot_status("Initializing camera...", 15);
    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %d", ret);
        return;
    }
    ESP_LOGI(TAG, "Camera initialized");

    /* 2. Face detector */
    ui_update_boot_status("Starting face detector...", 30);
    ret = face_detector_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Face detector init failed");
        return;
    }
    ESP_LOGI(TAG, "Face detector initialized");

    /* 3. Face recognizer (loads embedding model) */
    ui_update_boot_status("Loading AI model...", 50);
    ret = recognizer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Recognizer init failed");
        return;
    }
    ESP_LOGI(TAG, "Face recognizer initialized");

    /* 4. Database manager (SQLite on SD card) */
    ui_update_boot_status("Mounting SD card...", 75);
    ret = db_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Database init failed, attempting to use internal storage");
        /* Fallback - system can still operate with limited functionality */
        ui_set_sd_card_status(false);
    } else {
        /* Load user embeddings into cache */
        recognizer_load_cache();
        ui_set_sd_card_status(true);
        ESP_LOGI(TAG, "Database initialized, %d users loaded", recognizer_get_cache_size());
    }

    /* 5. Audio system for voice prompts */
    #if ENABLE_AUDIO_GUIDANCE
    ret = audio_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed - voice prompts disabled");
    } else {
        ESP_LOGI(TAG, "Audio system initialized");
        /* Play system startup audio prompt */
        audio_play(AUDIO_PROMPTS_PATH "system_start.wav", false);
    }
    #endif

    /* 7. Battery monitor — always init, no external deps */
    ui_update_boot_status("Starting battery monitor...", 80);
    #if ENABLE_BATTERY_MONITOR
    ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor init failed");
    }
    #endif

    /* 8. BLE + WiFi + Cloud Sync */
    ui_update_boot_status("Initializing Network...", 85);
    
    esp_err_t r = ble_registration_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "NET: Enrollment queue init failed (%d)", r);
    } else {
        ESP_LOGI(TAG, "NET: Enrollment queue ready");
    }
    
    #if ENABLE_CLOUD_SYNC
    r = cloud_sync_init();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "NET_BLE: Cloud sync init failed (%d)", r);
    }

    r = wifi_manager_init();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "NET_BLE: Wi-Fi init failed (%d)", r);
        ui_update_boot_status("Wi-Fi Init Failed", 100);
        vTaskDelay(pdMS_TO_TICKS(800));
    } else {
        ESP_LOGI(TAG, "NET_BLE: Wi-Fi ready; starting network sync task");
        xTaskCreate(network_sync_task, "network_sync", TASK_NETWORK_STACK_SIZE,
                    NULL, TASK_NETWORK_PRIORITY, NULL);

        // Check if we have saved Wi-Fi networks in NVS
        nvs_handle_t nvs_w;
        int32_t wifi_count = 0;
        if (nvs_open("wifi_creds", NVS_READONLY, &nvs_w) == ESP_OK) {
            nvs_get_i32(nvs_w, "count", &wifi_count);
            nvs_close(nvs_w);
        }

        if (wifi_count > 0) {
            ESP_LOGI(TAG, "Found saved Wi-Fi networks, waiting for auto-connect...");
            int last_retry = -1;
            uint32_t start_time = xTaskGetTickCount();
            // Wait for connection or failed attempts (timeout after 90 seconds just in case)
            while ((xTaskGetTickCount() - start_time) < pdMS_TO_TICKS(90000)) {
                wifi_status_t status = wifi_manager_get_status();
                if (status == WIFI_STATUS_CONNECTED) {
                    ui_update_boot_status("Connected", 100);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    break;
                }
                if (status == WIFI_STATUS_CONNECTION_FAILED) {
                    ui_update_boot_status("Wi-Fi Unavailable", 100);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    break;
                }

                int retry = wifi_manager_get_retry_count();
                if (retry != last_retry) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Connecting Wi-Fi (Attempt %d/7)...", retry + 1);
                    // Map 7 retries to progress from 85% to 99%
                    int progress = 85 + (int)(retry * 2.0f);
                    if (progress > 99) progress = 99;
                    ui_update_boot_status(msg, progress);
                    last_retry = retry;
                }
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        } else {
            ui_update_boot_status("Ready", 100);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    #else
    ui_update_boot_status("Ready", 100);
    vTaskDelay(pdMS_TO_TICKS(300));
    #endif

    ui_hide_boot_screen();

    /* Create FreeRTOS tasks */
    xTaskCreate(camera_task, "camera", TASK_CAMERA_STACK_SIZE, NULL,
                TASK_CAMERA_PRIORITY, &s_camera_task_handle);
    xTaskCreate(detection_recognition_task, "detection", TASK_DETECTION_STACK_SIZE, NULL,
                TASK_DETECTION_PRIORITY, NULL);
    xTaskCreate(db_task, "database", TASK_DB_STACK_SIZE, NULL,
                TASK_DB_PRIORITY, NULL);
    /* NOTE: audio_async_task is now created inside audio_init() — no separate create here */
    xTaskCreate(battery_task, "battery", TASK_BATTERY_STACK_SIZE, NULL,
                TASK_BATTERY_PRIORITY, NULL);
    xTaskCreate(schedule_checker_task, "sched_check", 8192, NULL,
                2, NULL);

    ESP_LOGI(TAG, "All tasks started. System ready.");

    /* Setup Wizard Check */
    {
        uint8_t setup_done = 0;
        nvs_handle_t nvs_check;
        if (nvs_open("storage", NVS_READONLY, &nvs_check) == ESP_OK) {
            nvs_get_u8(nvs_check, "setup_done", &setup_done);
            nvs_close(nvs_check);
        }
        if (!setup_done) {
            ui_acquire();
            ui_show_setup_wizard();
            ui_release();
        } else {
            ui_acquire();
            ui_return_to_main();
            ui_release();
        }
    }

    /* Main loop - system state machine */
    while (1) {
        system_state_machine();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Camera capture task
 * Continuously captures frames from the SC2336 MIPI-CSI sensor and queues
 * them for face detection / recognition processing.
 */
static void camera_task(void *pvParameters) {
    camera_frame_t frame;
    
    while (1) {
        if (get_system_state() == SYSTEM_STATE_ENROLLMENT) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Capture frame from camera (blocks up to 1s on s_frame_sem) */
        camera_fb_t *fb = camera_capture_frame();
        if (fb == NULL) {
            ESP_LOGE("CAM_TASK", "Frame capture failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Update UI preview with the latest frame */
        ui_update_camera_frame(fb->buf, fb->width, fb->height);

        /* Queue frame for face detection/recognition only when detection task is idle */
        if (get_system_state() == SYSTEM_STATE_NORMAL) {
            if (uxQueueSpacesAvailable(g_camera_frame_queue) > 0) {
                if (s_detection_fb_buf != NULL) {
                    /* AR-8: Copy pixel data into the stable detection buffer, then
                     * embed the camera_fb_t metadata BY VALUE into the queue item.
                     * xQueueSend() copies camera_frame_t (including the embedded fb
                     * struct) into the queue storage, so detection_recognition_task
                     * gets its own private copy of all metadata fields. There is no
                     * longer a shared static struct that can be overwritten mid-read. */
                    memcpy(s_detection_fb_buf, fb->buf, fb->len);
                    frame.fb           = *fb;                         /* copy struct by value */
                    frame.fb.buf       = s_detection_fb_buf;          /* redirect buf to stable heap copy */
                    frame.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
                    xQueueSend(g_camera_frame_queue, &frame, 0);
                }
            }
        }
        
        /* ~30 FPS pacing */
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

/**
 * @brief Face detection and recognition task
 * Processes frames from camera queue, runs detection and recognition
 */
static void detection_recognition_task(void *pvParameters) {
    camera_frame_t frame;
    aligned_face_t aligned_face;
    face_embedding_t embedding;
    user_t *matched_user;
    float confidence;
    

    
    while (1) {
        /* [T1-7] Suspend during factory reset / shutdown */
        if (get_system_state() == SYSTEM_STATE_SHUTDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Wait for next frame */
        if (xQueueReceive(g_camera_frame_queue, &frame, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        /* Skip processing if enrollment is in progress to avoid queue contention */
        if (get_system_state() != SYSTEM_STATE_NORMAL) {
            camera_return_frame(&frame.fb);
            continue;
        }
        
        /* Run face detection */
        detection_result_t det_result;
        if (face_detector_run(&frame.fb, &det_result) != ESP_OK) {
            camera_return_frame(&frame.fb);
            continue;
        }
        
        /* If no face detected, update UI and continue */
        if (det_result.face_count == 0) {
            if (ui_acquire()) {
                ui_update_detection_bounding_box(0, 0, 0, 0, false);
                ui_release();
            }
            camera_return_frame(&frame.fb);
            continue;
        }
        
        /* Take the largest face (first in list) */
        detected_face_t *face = &det_result.faces[0];
        
        /* Skip low confidence detections */
        if (face->confidence < FACE_DETECT_CONFIDENCE_MIN) {
            camera_return_frame(&frame.fb);
            continue;
        }
        
        /* Align face to 112x112 canonical view */
        if (face_alignment_align(&frame.fb, face, &aligned_face) != ESP_OK) {
            camera_return_frame(&frame.fb);
            continue;
        }

        /* Actively adjust face crop brightness & contrast to target luminance (128) */
        face_alignment_normalize_brightness(&aligned_face);
        
        /* Extract embedding */
        if (feature_extractor_run(&aligned_face, &embedding) != ESP_OK) {
            face_alignment_free(&aligned_face);
            camera_return_frame(&frame.fb);
            continue;
        }
        
        /* Match against database */
        recognizer_identify(&embedding, &matched_user, &confidence);
        
        ESP_LOGI(TAG, "Recognition run: matched_user=%s, confidence=%.3f (threshold=%.2f)",
                 (matched_user != NULL) ? matched_user->name : "None/Unknown", confidence, RECOGNITION_THRESHOLD);
        
        /* Update UI with bounding box and recognition result (scaled to 480x360 UI preview size) */
        if (ui_acquire()) {
            int scaled_x = (frame.fb.width > 0) ? (face->x * 480 / frame.fb.width) : face->x;
            int scaled_y = (frame.fb.height > 0) ? (face->y * 360 / frame.fb.height) : face->y;
            int scaled_w = (frame.fb.width > 0) ? (face->w * 480 / frame.fb.width) : face->w;
            int scaled_h = (frame.fb.height > 0) ? (face->h * 360 / frame.fb.height) : face->h;
            ui_update_detection_bounding_box(scaled_x, scaled_y, scaled_w, scaled_h, true);
            
            if (matched_user != NULL && confidence >= RECOGNITION_THRESHOLD) {
                /* Recognition success */
                ui_show_recognition_result(matched_user->name, confidence);
                ui_release();
                
                /* Auto-activate admin session if recognized user is admin */
                if (strcmp(matched_user->role, "admin") == 0) {
                    activate_admin_session();
                }
                
                /* Log attendance (async) */
                process_recognition_result(matched_user, confidence);
                
                /* Play success sound */
                #if ENABLE_AUDIO_GUIDANCE
                audio_play(AUDIO_PROMPTS_PATH "attendance_success.wav", false);
                #endif
            } else {
                /* Unknown face */
                ui_show_recognition_result("Unknown", confidence);
                ui_release();
                
                /* Play unknown sound (optional) */
                #if ENABLE_AUDIO_GUIDANCE
                audio_play(AUDIO_PROMPTS_PATH "unknown_face.wav", false);
                #endif
            }
        } else {
            /* Log attendance even if UI update was skipped due to lock timeout */
            if (matched_user != NULL && confidence >= RECOGNITION_THRESHOLD) {
                process_recognition_result(matched_user, confidence);
            }
        }
        
        /* Clean up */
        face_alignment_free(&aligned_face);
        camera_return_frame(&frame.fb);
        

        
        /* Progress log */
        static int det_cnt = 0;
        if (++det_cnt % 10 == 0) esp_rom_printf("P");
    }
}

/**
 * @brief Process successful recognition and log attendance.
 *
 * Duplicate prevention uses a TWO-LAYER gate:
 *  1. In-memory dedup table (session_already_recorded) — checked FIRST.
 *     This is updated atomically BEFORE the async DB queue send, so even if
 *     10 frames arrive within one DB write cycle, only the first passes.
 *  2. DB query (db_attendance_exists_for_schedule) — checked SECOND as a
 *     safety net after power-cycle or app restart for the same class window.
 *
 * When no active schedule exists (schedule_id == 0), we still dedup using
 * a sentinel schedule_id of UINT32_MAX so the same student is only recorded
 * once per boot session.
 */
static esp_err_t process_recognition_result(user_t *user, float confidence) {
    /* --- Determine active schedule --- */
    uint32_t schedule_id = db_get_current_schedule_id();

    /* --- Outside of a scheduled event, just show name and return --- */
    if (schedule_id == 0) {
        static char id_msg[96];
        snprintf(id_msg, sizeof(id_msg), "Identified as %s", user->name);
        ui_show_attendance_feedback(id_msg, 2); /* 2 = Neutral blue/gray banner */
        return ESP_OK;
    }

    bool is_test_or_exam = db_is_test_or_exam_schedule(schedule_id);

    /* --- Fetch active schedule name for personalised message --- */
    db_schedule_t active_sched;
    bool has_class = (db_get_active_schedule(&active_sched) == ESP_OK);

    /* ---------------------------------------------------------------
     * LAYER 1: In-memory dedup — instant, no DB latency
     * --------------------------------------------------------------- */
    if (session_already_recorded(user->id, schedule_id, is_test_or_exam)) {
        ui_show_attendance_feedback(
            "Your attendance has been previously recorded for this class", 1);
        ESP_LOGI(TAG, "Dedup(mem): user %lu already in session (schedule %lu)",
                 (unsigned long)user->id, (unsigned long)schedule_id);
        return ESP_OK;
    }

    /* ---------------------------------------------------------------
     * LAYER 2: DB check — catches reboot/restart within same window
     * --------------------------------------------------------------- */
    if (db_attendance_exists_for_schedule(user->id, schedule_id)) {
        session_record_attendance(user->id, schedule_id);
        ui_show_attendance_feedback(
            "Your attendance has been previously recorded for this class", 1);
        ESP_LOGI(TAG, "Dedup(db): user %lu already in DB for schedule %lu",
                 (unsigned long)user->id, (unsigned long)schedule_id);
        return ESP_OK;
    }

    /* ---------------------------------------------------------------
     * FIRST/SECOND SCAN: register in memory table NOW (before queue send)
     * --------------------------------------------------------------- */
    session_record_attendance(user->id, schedule_id);

    /* --- Insert attendance log (async) --- */
    attendance_log_t *log = calloc(1, sizeof(attendance_log_t));
    if (!log) {
        ESP_LOGE(TAG, "Failed to allocate attendance log");
        return ESP_ERR_NO_MEM;
    }

    log->user_id     = user->id;
    log->schedule_id = schedule_id;
    log->timestamp   = time(NULL);
    strncpy(log->status, "present", sizeof(log->status) - 1);
    log->status[sizeof(log->status) - 1] = '\0';
    log->synced = 0;
    generate_uuid_hex(log->uuid, sizeof(log->uuid));

    db_request_t req = {
        .type      = DB_REQUEST_INSERT_LOG,
        .data      = log,
        .data_len  = sizeof(attendance_log_t),
        .free_data = true
    };

    if (xQueueSend(g_db_request_queue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(log);
        return ESP_FAIL;
    }

    /* --- Increment attendance counter on screen --- */
    ui_attendance_increment_count();

    /* --- Show green success feedback --- */
    static char ok_msg[160];
    if (has_class) {
        snprintf(ok_msg, sizeof(ok_msg),
                 "%.40s, your attendance for %.50s has been recorded",
                 user->name, active_sched.course_name);
    } else {
        snprintf(ok_msg, sizeof(ok_msg),
                 "%.40s, your attendance has been recorded", user->name);
    }
    ui_show_attendance_feedback(ok_msg, 0);

    return ESP_OK;
}

/**
 * @brief Start enrollment mode
 * Called from UI when user taps "Enroll" button
 */
static float compute_int8_cosine_similarity(const face_embedding_t *a, const face_embedding_t *b) {
    long long dot = 0, norm_a = 0, norm_b = 0;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        dot += (int)a->values[i] * (int)b->values[i];
        norm_a += (int)a->values[i] * (int)a->values[i];
        norm_b += (int)b->values[i] * (int)b->values[i];
    }
    if (norm_a == 0 || norm_b == 0) return 0.0f;
    return (float)dot / (sqrtf((float)norm_a) * sqrtf((float)norm_b));
}

/**
 * @brief Statistically robust 8-step facial template enrollment algorithm
 * 1. Capture 30 valid frames with single-face, blur, brightness & pose gates
 * 2. Store embeddings in PSRAM temporary array
 * 3. Pairwise similarity outlier removal (\mu - 1.5\sigma threshold clamped \ge 0.65)
 * 4. K-Means pose clustering (K=3) with automatic fallback for empty clusters
 * 5. Compute centroids for non-empty clusters
 * 6. Cluster-size weighted averaging and L2 norm restoration
 * 7. Score quality (0-100 / EXCELLENT, GOOD, AVERAGE, POOR) & persist single template + metadata
 * 8. UI quality feedback (Redo button shown ONLY when quality is POOR)
 */
static esp_err_t process_enrollment_frames_for_user(user_t* new_user) {
    ESP_LOGI(TAG, "Starting robust 8-step enrollment for %s", new_user->name);

    /* Reset camera to AUTO exposure profile (Profile 0) so ISP AE calculates optimal luminance */
    camera_set_profile(CAMERA_PROFILE_AUTO);

    /* Step 1 & 2: Allocate PSRAM buffers for 30 valid samples */
    camera_fb_t **frames = (camera_fb_t**)heap_caps_calloc(ENROLL_FRAMES_TOTAL, sizeof(camera_fb_t *), MALLOC_CAP_SPIRAM);
    aligned_face_t *aligned_frames = (aligned_face_t*)heap_caps_calloc(ENROLL_FRAMES_TOTAL, sizeof(aligned_face_t), MALLOC_CAP_SPIRAM);
    face_embedding_t *embeddings = (face_embedding_t*)heap_caps_calloc(ENROLL_FRAMES_TOTAL, sizeof(face_embedding_t), MALLOC_CAP_SPIRAM);
    float *quality_scores = (float*)heap_caps_calloc(ENROLL_FRAMES_TOTAL, sizeof(float), MALLOC_CAP_SPIRAM);
    float *sharpness_vals = (float*)heap_caps_calloc(ENROLL_FRAMES_TOTAL, sizeof(float), MALLOC_CAP_SPIRAM);

    if (!frames || !aligned_frames || !embeddings || !quality_scores || !sharpness_vals) {
        ESP_LOGE(TAG, "Failed to allocate enrollment PSRAM buffers");
        if (frames) free(frames);
        if (aligned_frames) free(aligned_frames);
        if (embeddings) free(embeddings);
        if (quality_scores) free(quality_scores);
        if (sharpness_vals) free(sharpness_vals);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = ESP_OK;
    int valid_count = 0;
    int rejected_count = 0;
    int total_attempts = 0;
    const int MAX_ATTEMPTS = 120;

    xSemaphoreTake(camera_get_frame_sem(), 0);
    camera_set_framesize(CAMERA_ENROLL_FRAME_SIZE);

    /* Step 1: Capture 30 valid samples with filtering & live progress display */
    while (valid_count < ENROLL_FRAMES_TOTAL && total_attempts < MAX_ATTEMPTS) {
        vTaskDelay(pdMS_TO_TICKS(20)); /* Feed task watchdog and allow LVGL rendering */

        if (g_enrollment_cancel) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        total_attempts++;

        /* Update UI progress */
        if (ui_acquire()) {
            ui_enrollment_set_capture_progress(valid_count, ENROLL_FRAMES_TOTAL);
            ui_release();
        }

        camera_fb_t* fb = camera_capture_with_autofocus();
        if (fb == NULL) {
            rejected_count++;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        ui_update_enrollment_camera_frame(fb->buf, fb->width, fb->height);
        vTaskDelay(pdMS_TO_TICKS(5));

        /* Run face detection (matching Attendance Scanner pipeline) */
        detection_result_t det_result;
        esp_err_t det_err = face_detector_run(fb, &det_result);
        if (det_err != ESP_OK || det_result.face_count == 0) {
            camera_return_frame(fb);
            rejected_count++;
            if (ui_acquire()) {
                ui_enrollment_set_face_detected(false);
                ui_show_pose_guidance("Position face in camera frame");
                ui_release();
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        /* Take primary face (highest confidence) */
        detected_face_t *face = &det_result.faces[0];
        if (face->confidence < FACE_DETECT_CONFIDENCE_MIN) {
            camera_return_frame(fb);
            rejected_count++;
            if (ui_acquire()) {
                ui_enrollment_set_face_detected(false);
                ui_show_pose_guidance("Position face in camera frame");
                ui_release();
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        float sharpness = face_detector_compute_sharpness(fb, face);
        float brightness = face_detector_compute_brightness(fb, face);
        float yaw = face_detector_compute_yaw(face);

        if (ui_acquire()) {
            ui_enrollment_set_face_detected(true);
            ui_release();
        }

        /* Step 2: Align face crop and actively normalize brightness before passing to model */
        if (face_alignment_align(fb, face, &aligned_frames[valid_count]) != ESP_OK) {
            camera_return_frame(fb);
            rejected_count++;
            continue;
        }

        /* Actively adjust face crop brightness & contrast to target luminance (128) */
        face_alignment_normalize_brightness(&aligned_frames[valid_count]);

        if (feature_extractor_run(&aligned_frames[valid_count], &embeddings[valid_count]) != ESP_OK) {
            face_alignment_free(&aligned_frames[valid_count]);
            camera_return_frame(fb);
            rejected_count++;
            continue;
        }

        /* Calculate frame quality score */
        float q_score = (sharpness / 100.0f) * 0.5f +
                        (1.0f - (fabsf(yaw) / ENROLL_YAW_MAX_DEG)) * 0.3f + 0.2f;
        if (q_score < 0.01f) q_score = 0.01f;

        quality_scores[valid_count] = q_score;
        sharpness_vals[valid_count] = sharpness;
        frames[valid_count] = fb;

        valid_count++;
        ESP_LOGI(TAG, "Accepted sample %d/30 (sharpness=%.1f, brightness=%.1f, yaw=%.1f)", valid_count, sharpness, brightness, yaw);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (valid_count < 10) {
        ESP_LOGE(TAG, "Enrollment failed: Insufficient valid samples (%d/30)", valid_count);
        ret = ESP_FAIL;
        goto cleanup;
    }

    /* Step 3: Remove Outliers using Adaptive Threshold (\mu - 1.5\sigma, clamp \ge 0.65) */
    {
        float mean_sims[ENROLL_FRAMES_TOTAL] = {0.0f};
        float sum_all_means = 0.0f;

        for (int i = 0; i < valid_count; i++) {
            float sum_sim = 0.0f;
            int count_sim = 0;
            for (int j = 0; j < valid_count; j++) {
                if (i == j) continue;
                sum_sim += compute_int8_cosine_similarity(&embeddings[i], &embeddings[j]);
                count_sim++;
            }
            mean_sims[i] = (count_sim > 0) ? (sum_sim / (float)count_sim) : 0.0f;
            sum_all_means += mean_sims[i];
        }

        float grand_mean = sum_all_means / (float)valid_count;
        float variance_sum = 0.0f;
        for (int i = 0; i < valid_count; i++) {
            float diff = mean_sims[i] - grand_mean;
            variance_sum += diff * diff;
        }
        float std_dev = sqrtf(variance_sum / (float)valid_count);

        float adaptive_threshold = grand_mean - 1.5f * std_dev;
        if (adaptive_threshold < 0.65f) {
            adaptive_threshold = 0.65f; /* Clamped to minimum 0.65 */
        }

        ESP_LOGI(TAG, "Outlier Filter: grand_mean=%.3f, std_dev=%.3f -> Adaptive Threshold=%.3f", grand_mean, std_dev, adaptive_threshold);

        int inlier_indices[ENROLL_FRAMES_TOTAL];
        int inlier_count = 0;
        int outlier_count = 0;

        for (int i = 0; i < valid_count; i++) {
            if (mean_sims[i] >= adaptive_threshold) {
                inlier_indices[inlier_count++] = i;
            } else {
                outlier_count++;
                ESP_LOGW(TAG, "Outlier discarded: sample %d (mean_sim=%.3f < threshold=%.3f)", i, mean_sims[i], adaptive_threshold);
            }
        }

        if (inlier_count == 0) {
            /* Fallback: pick top 5 highest similarity samples if all dropped */
            inlier_count = valid_count < 5 ? valid_count : 5;
            for (int i = 0; i < inlier_count; i++) inlier_indices[i] = i;
        }

        /* Step 4 & 5: K-Means Pose Clustering (K=3 with Automatic Fallback) */
        int K_target = 3;
        if (inlier_count < K_target) K_target = inlier_count;

        int cluster_labels[ENROLL_FRAMES_TOTAL];
        float centroids[3][EMBEDDING_DIM];
        memset(centroids, 0, sizeof(centroids));

        /* Initialise centroids spread across inliers */
        for (int k = 0; k < K_target; k++) {
            int init_idx = inlier_indices[(k * inlier_count) / K_target];
            for (int m = 0; m < EMBEDDING_DIM; m++) {
                centroids[k][m] = (float)embeddings[init_idx].values[m];
            }
        }

        /* Run 10 iterations of K-Means */
        for (int iter = 0; iter < 10; iter++) {
            /* Assign points to nearest centroid */
            for (int i = 0; i < inlier_count; i++) {
                int idx = inlier_indices[i];
                float max_sim = -2.0f;
                int best_c = 0;
                for (int k = 0; k < K_target; k++) {
                    float dot = 0.0f, c_norm_sq = 0.0f, e_norm_sq = 0.0f;
                    for (int m = 0; m < EMBEDDING_DIM; m++) {
                        float ev = (float)embeddings[idx].values[m];
                        float cv = centroids[k][m];
                        dot += ev * cv;
                        e_norm_sq += ev * ev;
                        c_norm_sq += cv * cv;
                    }
                    float sim = (e_norm_sq > 0.0f && c_norm_sq > 0.0f) ? (dot / (sqrtf(e_norm_sq) * sqrtf(c_norm_sq))) : -1.0f;
                    if (sim > max_sim) {
                        max_sim = sim;
                        best_c = k;
                    }
                }
                cluster_labels[i] = best_c;
            }

            /* Update centroids */
            float new_centroids[3][EMBEDDING_DIM];
            int cluster_sizes[3] = {0};
            memset(new_centroids, 0, sizeof(new_centroids));

            for (int i = 0; i < inlier_count; i++) {
                int c = cluster_labels[i];
                int idx = inlier_indices[i];
                cluster_sizes[c]++;
                for (int m = 0; m < EMBEDDING_DIM; m++) {
                    new_centroids[c][m] += (float)embeddings[idx].values[m];
                }
            }

            for (int k = 0; k < K_target; k++) {
                if (cluster_sizes[k] > 0) {
                    for (int m = 0; m < EMBEDDING_DIM; m++) {
                        centroids[k][m] = new_centroids[k][m] / (float)cluster_sizes[k];
                    }
                }
            }
        }

        /* Fallback: count non-empty clusters and gather centroids */
        float active_centroids[3][EMBEDDING_DIM];
        int active_sizes[3] = {0};
        int K_effective = 0;

        for (int k = 0; k < K_target; k++) {
            int size_k = 0;
            for (int i = 0; i < inlier_count; i++) {
                if (cluster_labels[i] == k) size_k++;
            }
            if (size_k > 0) {
                active_sizes[K_effective] = size_k;
                memcpy(active_centroids[K_effective], centroids[k], sizeof(float) * EMBEDDING_DIM);
                K_effective++;
            }
        }

        ESP_LOGI(TAG, "Pose Clustering complete: Inliers=%d, Effective Clusters K=%d (Sizes: %d, %d, %d)",
                 inlier_count, K_effective, active_sizes[0], active_sizes[1], active_sizes[2]);

        /* Step 6: Cluster-Size Weighted Averaging & Norm Restoration */
        float weighted_repr[EMBEDDING_DIM] = {0.0f};
        float total_inlier_weight = 0.0f;

        for (int k = 0; k < K_effective; k++) {
            float weight = (float)active_sizes[k];
            total_inlier_weight += weight;
            for (int m = 0; m < EMBEDDING_DIM; m++) {
                weighted_repr[m] += active_centroids[k][m] * weight;
            }
        }

        /* Calculate mean L2 norm of inliers */
        float sum_inlier_norms = 0.0f;
        for (int i = 0; i < inlier_count; i++) {
            int idx = inlier_indices[i];
            float norm_sq = 0.0f;
            for (int m = 0; m < EMBEDDING_DIM; m++) {
                float val = (float)embeddings[idx].values[m];
                norm_sq += val * val;
            }
            sum_inlier_norms += sqrtf(norm_sq);
        }
        float target_norm = (inlier_count > 0) ? (sum_inlier_norms / (float)inlier_count) : 100.0f;

        /* Normalize weighted_repr to target_norm and quantise to int8 */
        float repr_norm_sq = 0.0f;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            weighted_repr[m] /= total_inlier_weight;
            repr_norm_sq += weighted_repr[m] * weighted_repr[m];
        }
        float repr_norm = sqrtf(repr_norm_sq);
        float scale = (repr_norm > 1e-6f) ? (target_norm / repr_norm) : 1.0f;

        face_embedding_t final_embedding;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            int rounded = (int)roundf(weighted_repr[m] * scale);
            if (rounded < -128) rounded = -128;
            if (rounded > 127)  rounded = 127;
            final_embedding.values[m] = (int8_t)rounded;
        }

        /* Step 7: Compute Numeric Enrollment Quality Score (0-100) & Store Metadata */
        float inlier_ratio_score = ((float)inlier_count / (float)ENROLL_FRAMES_TOTAL) * 100.0f;
        
        float inlier_sim_sum = 0.0f;
        for (int i = 0; i < inlier_count; i++) {
            inlier_sim_sum += mean_sims[inlier_indices[i]];
        }
        float mean_inlier_sim = (inlier_count > 0) ? (inlier_sim_sum / (float)inlier_count) : 0.65f;
        float compactness_score = ((mean_inlier_sim - 0.65f) / 0.35f) * 100.0f;
        if (compactness_score < 0.0f) compactness_score = 0.0f;
        if (compactness_score > 100.0f) compactness_score = 100.0f;

        float sharpness_sum = 0.0f;
        for (int i = 0; i < inlier_count; i++) sharpness_sum += sharpness_vals[inlier_indices[i]];
        float avg_sharpness = (inlier_count > 0) ? (sharpness_sum / (float)inlier_count) : 50.0f;
        float sharpness_score = (avg_sharpness / 120.0f) * 100.0f;
        if (sharpness_score > 100.0f) sharpness_score = 100.0f;

        int final_quality_score = (int)roundf(0.40f * inlier_ratio_score + 0.40f * compactness_score + 0.20f * sharpness_score);
        if (final_quality_score < 0) final_quality_score = 0;
        if (final_quality_score > 100) final_quality_score = 100;

        const char* rating_str = "POOR";
        if (final_quality_score >= 85) rating_str = "EXCELLENT";
        else if (final_quality_score >= 70) rating_str = "GOOD";
        else if (final_quality_score >= 55) rating_str = "AVERAGE";

        ESP_LOGI(TAG, "Enrollment Quality Evaluation: Score=%d/100, Rating=%s (Inliers=%d/%d, Discarded=%d)",
                 final_quality_score, rating_str, inlier_count, valid_count, rejected_count + outlier_count);

        /* Duplicate enrollment check against existing DB templates */
        user_t *existing_user = NULL;
        float existing_confidence = 0.0f;
        recognizer_identify(&final_embedding, &existing_user, &existing_confidence);
        if (existing_user != NULL && existing_confidence >= RECOGNITION_THRESHOLD) {
            ESP_LOGW(TAG, "Face already enrolled under user: %s (confidence: %.2f)", existing_user->name, existing_confidence);
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }

        /* Populate new_user record with representative embedding and metadata */
        new_user->embedding = final_embedding;
        new_user->created_at = (uint32_t)time(NULL);
        new_user->updated_at = new_user->created_at;
        new_user->enroll_quality = (uint8_t)final_quality_score;
        new_user->enroll_accepted = (uint8_t)inlier_count;
        new_user->enroll_rejected = (uint8_t)(rejected_count + outlier_count);
        new_user->model_version = 1;
        new_user->embedding_dim = 128;
        generate_uuid_hex(new_user->uuid, sizeof(new_user->uuid));

        /* Store ONLY single representative embedding into database */
        ret = db_insert_user(new_user);
        if (ret == ESP_OK) {
            recognizer_add_to_cache(new_user);
            vTaskDelay(pdMS_TO_TICKS(5));

            /* Step 8: Update UI with Quality Result Card (Redo option shown ONLY if POOR) */
            if (ui_acquire()) {
                int student_idx = (int)(intptr_t)new_user->id;
                ui_enrollment_show_quality_result(rating_str, final_quality_score, inlier_count,
                                                  rejected_count + outlier_count, new_user->name, student_idx);
                ui_release();
            }
        }
    }

cleanup:
    camera_set_framesize(CAMERA_FRAME_SIZE);
    for (int j = 0; j < ENROLL_FRAMES_TOTAL; j++) {
        if (frames[j]) camera_return_frame(frames[j]);
        if (aligned_frames[j].data) face_alignment_free(&aligned_frames[j]);
    }
    heap_caps_free(frames);
    heap_caps_free(aligned_frames);
    heap_caps_free(embeddings);
    heap_caps_free(quality_scores);
    heap_caps_free(sharpness_vals);

    return ret;
}

void start_single_capture_task(void *pvParam) {
    int student_idx = (int)(intptr_t)pvParam;
    g_enrollment_cancel = false;
    
    enrollment_data_t reg_data;
    if (!ble_registration_peek_student(student_idx, &reg_data)) {
        if (ui_acquire()) {
            ui_enrollment_set_status(false, "Failed to load student data.");
            ui_release();
        }
        vTaskDelete(NULL);
        return;
    }
    
    user_t new_user;
    memset(&new_user, 0, sizeof(user_t));
    strncpy(new_user.name,         reg_data.name,         sizeof(new_user.name)         - 1);
    strncpy(new_user.student_id,   reg_data.student_id,   sizeof(new_user.student_id)   - 1);
    strncpy(new_user.phone_number, reg_data.phone_number, sizeof(new_user.phone_number) - 1);
    new_user.telegram_id[0] = '\0';
    strncpy(new_user.role,         reg_data.role,         sizeof(new_user.role)         - 1);

    /* Check if student ID is already registered in database */
    if (db_student_id_exists(new_user.student_id)) {
        if (ui_acquire()) {
            ui_enrollment_set_status(false, "Matric ID already registered!");
            ui_release();
        }
        ble_registration_set_result(false, 0);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = process_enrollment_frames_for_user(&new_user);

    bool enroll_ok = (ret == ESP_OK);

    /* --- Update UI (inside mutex) --- */
    if (ui_acquire()) {
        if (enroll_ok) {
            ble_registration_consume_student(student_idx);
            ui_enrollment_show_success(new_user.name, student_idx);
            ble_registration_set_result(true, new_user.id);

            /* If this was an admin enrollment, apply the Telegram username saved
             * in NVS by the Admin Setup Wizard before face capture started. */
            if (strncmp((const char*)g_enrollment_role_override, "admin",
                        sizeof(g_enrollment_role_override)) == 0) {
                nvs_handle_t nvs_h;
                if (nvs_open("storage", NVS_READWRITE, &nvs_h) == ESP_OK) {
                    char tg_username[64] = {0};
                    size_t tg_len = sizeof(tg_username);
                    if (nvs_get_str(nvs_h, "admin_telegram", tg_username, &tg_len) == ESP_OK
                            && strlen(tg_username) > 0) {
                        db_update_user_telegram_id(new_user.uuid, tg_username);
                        ESP_LOGI(TAG, "Set admin telegram_id to '%s' for uuid %s",
                                 tg_username, new_user.uuid);
                        /* Clear the key so it doesn't persist to future enrollments */
                        nvs_erase_key(nvs_h, "admin_telegram");
                        nvs_commit(nvs_h);
                    }
                    nvs_close(nvs_h);
                }
            }
        } else {
            if (ret == ESP_ERR_INVALID_STATE) {
                ui_enrollment_set_status(false, "Face already registered!");
            } else {
                ui_enrollment_set_status(false, "Face capture failed. Try again.");
            }
            ble_registration_set_result(false, 0);
        }
        ui_release();
    }

    /* (Audio guidance removed 2026-06-11) */

    vTaskDelete(NULL);
}

void start_enrollment_task(void *pvParam) {
    (void)pvParam;
    set_system_state(SYSTEM_STATE_ENROLLMENT);
    
    #if ENABLE_AUDIO_GUIDANCE
    audio_play(AUDIO_PROMPTS_PATH "enroll_start.wav", true);
    #endif
    
    if (ui_acquire()) {
        /* Delete deferred admin setup screen safely within LVGL lock.
         * Deleting it from the button callback (via lv_obj_delete_async) caused
         * a use-after-free: the display refresh timer fired in the same
         * lv_timer_handler cycle and accessed the freed screen's child list. */
        if (g_admin_setup_screen_to_delete) {
            lv_obj_delete(g_admin_setup_screen_to_delete);
            g_admin_setup_screen_to_delete = NULL;
        }
        ui_show_enrollment_screen();
        ui_release();
    }
    
    /* Generate a fresh session PIN for this enrollment window */
    generate_enrollment_pin();

    /* Show PIN on device screen so the admin can read it or use it */
    if (ui_acquire()) {
        char pin_msg[128];
        snprintf(pin_msg, sizeof(pin_msg), "Connect to WiFi 'Attendance_Setup'\nPIN: %s", ble_registration_get_pin());
        ui_show_notification(NOTIFY_INFO, "Remote Enrollment", pin_msg, 0);
        ui_release();
    }

    /* Start Wi-Fi Captive Portal */
    wifi_ap_portal_start();
    
    g_enrollment_cancel = false;
    
    /* Wait until the admin clicks the close button */
    while (!g_enrollment_cancel) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Stop Wi-Fi Captive Portal */
    wifi_ap_portal_stop();
    
    set_system_state(SYSTEM_STATE_NORMAL);
    
    /* Handle wizard role override completion */
    if (strlen((char*)g_enrollment_role_override) > 0) {
        memset((char*)g_enrollment_role_override, 0, sizeof(g_enrollment_role_override));
        g_wizard_admin_enrolled = true;
    }

    if (ui_acquire()) {
        ui_return_to_main();
        ui_release();
    }

    vTaskDelete(NULL);
}

/**
 * @brief Database task - processes queued database requests
 */
static void db_task(void *pvParameters) {
    db_request_t req;

    while (1) {
        /* [T1-7] Idle during factory reset / shutdown to prevent mid-write corruption */
        if (get_system_state() == SYSTEM_STATE_SHUTDOWN) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (xQueueReceive(g_db_request_queue, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (req.type) {
                case DB_REQUEST_INSERT_LOG:
                    db_insert_attendance_log((attendance_log_t *)req.data);
                    break;
                case DB_REQUEST_GET_USERS:
                    /* Handle get users */
                    break;
                case DB_REQUEST_UPDATE_SYNC:
                    db_mark_logs_synced((uint32_t *)req.data, req.data_len);
                    break;
                default:
                    ESP_LOGW("DB_TASK", "Unknown request type: %d", req.type);
                    break;
            }

            /* Free data if allocated */
            if (req.free_data && req.data) {
                free(req.data);
            }
        }
    }
}




/**
 * @brief Network sync task - periodic and manual cloud synchronization
 */
static void network_sync_task(void *pvParameters) {
    bool s_boot_sync_done = false;

    /* --- Wait for Wi-Fi connection before running boot sync --- */
    ESP_LOGI(TAG, "Waiting for Wi-Fi before boot sync...");
    int boot_wait = 0;
    while (wifi_manager_get_status() != WIFI_STATUS_CONNECTED && boot_wait < 600) {
        vTaskDelay(pdMS_TO_TICKS(50));
        boot_wait++;
    }

    if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi connected on boot — triggering immediate cloud sync");
        /* Signal the main loop body below to run a sync immediately */
        s_boot_sync_done = false; /* will be set true after first sync */
    } else {
        s_boot_sync_done = true; /* No Wi-Fi; skip boot sync */
        ESP_LOGW(TAG, "No Wi-Fi on boot — skipping boot sync");
    }

    /* Clear the boot-triggered sync bit so we don't execute a duplicate second sync cycle immediately */
    xEventGroupClearBits(g_system_event_group, SYSTEM_EVENT_TOUCH_MENU);

    /* [AR-7] Read sync interval once before the loop (not on every iteration).
     * NVS flash ops briefly lock flash and can stutter UI rendering if called
     * inside a tight loop. Only re-read on explicit setting changes. */
    uint32_t sync_interval_ms = CLOUD_SYNC_INTERVAL_MS;
    {
        nvs_handle_t nvs_int;
        if (nvs_open("storage", NVS_READONLY, &nvs_int) == ESP_OK) {
            nvs_get_u32(nvs_int, "sync_ms", &sync_interval_ms);
            nvs_close(nvs_int);
        }
    }

    while (1) {

        ESP_LOGI(TAG, "Sync task waiting. Interval: %lu ms", (unsigned long)sync_interval_ms);

        /* First iteration: skip the wait so we sync immediately on boot */
        if (!s_boot_sync_done) {
            s_boot_sync_done = true;
            ESP_LOGI(TAG, "Boot sync: skipping timer wait");
        } else if (sync_interval_ms == 0) {
            /* "Never" - wait indefinitely for manual trigger */
            xEventGroupWaitBits(g_system_event_group, SYSTEM_EVENT_TOUCH_MENU, pdTRUE, pdFALSE, portMAX_DELAY);
        } else {
            xEventGroupWaitBits(g_system_event_group, SYSTEM_EVENT_TOUCH_MENU, pdTRUE, pdFALSE, pdMS_TO_TICKS(sync_interval_ms));
        }
        
        if (get_system_state() == SYSTEM_STATE_NORMAL) {
            set_system_state(SYSTEM_STATE_SYNCING);
            if (ui_acquire()) {
                ui_set_sync_status(true);
                ui_release();
            }
            
            ESP_LOGI(TAG, "Starting brief-connect sync cycle");
            
            /* 1. Connect Wi-Fi using saved credentials if not already connected */
            bool already_connected = (wifi_manager_get_status() == WIFI_STATUS_CONNECTED);
            bool connect_ok = false;
            bool sync_ok = false;
            
            if (already_connected) {
                connect_ok = true;
            } else {
                if (wifi_manager_connect_saved() == ESP_OK) {
                    /* Wait for Wi-Fi connection to establish (up to 30 seconds) */
                    int wait_limit = 600; // 600 * 50ms = 30 seconds
                    while (wifi_manager_get_status() != WIFI_STATUS_CONNECTED &&
                           wifi_manager_get_status() != WIFI_STATUS_CONNECTION_FAILED &&
                           wait_limit > 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                        wait_limit--;
                    }
                    if (wifi_manager_get_status() == WIFI_STATUS_CONNECTED) {
                        connect_ok = true;
                    }
                }
            }
            
            if (connect_ok) {
                /* 2. Sync Time (SNTP) — [BUG-2] retry loop up to 3 × 20 s = 60 s total.
                 * The ESP32-C6 co-processor is factory pre-flashed (cannot be updated),
                 * runs esp-hosted v2.3.0 vs host v2.12.0, and the SDIO bridge runs at
                 * 10 MHz, adding significant latency to DNS + NTP round-trips.
                 * Three fallback NTP servers are configured in sntp_sync.c. */
                bool time_synced = sntp_sync_is_synchronized();
                if (!time_synced) {
                    esp_err_t init_err = sntp_sync_init();
                    if (init_err == ESP_OK || init_err == ESP_ERR_INVALID_STATE) {
                        for (int attempt = 1; !time_synced && attempt <= 3; attempt++) {
                            ESP_LOGI(TAG, "SNTP sync attempt %d/3 (timeout 20s)...", attempt);
                            if (sntp_sync_wait_for_sync(20000) == ESP_OK) {
                                time_synced = true;
                                ESP_LOGI(TAG, "SNTP time synchronized successfully");
                            } else {
                                ESP_LOGW(TAG, "SNTP attempt %d/3 timed out", attempt);
                            }
                        }
                    } else {
                        ESP_LOGW(TAG, "SNTP init failed (%d) — will proceed without time sync", init_err);
                    }
                    if (!time_synced) {
                        ESP_LOGW(TAG, "SNTP timed out after 3 attempts — cloud sync will still run (self-syncs via HTTP Date header)");
                    }
                } else {
                    ESP_LOGI(TAG, "Time already synchronized — skipping SNTP wait");
                }
                sync_ok = time_synced; /* track for UI notification only */

                /* 3. Run Cloud Sync (Telegram).
                 * [BUG-2] Cloud sync now ALWAYS runs when Wi-Fi is connected.
                 * It independently syncs device time from the HTTP Date: response
                 * header on every API call, so SNTP failure is not a blocker.
                 * SNTP is still attempted above so that schedule window checks
                 * (start_time / end_time comparisons) use accurate local time. */
                xEventGroupClearBits(g_system_event_group, SYSTEM_EVENT_CLOUD_SYNC_DONE);
                cloud_sync_start();

                /* [Fix M2] Wait on event bit set by cloud_sync_task instead of
                 * a hardcoded vTaskDelay. Use a 120 s ceiling as a safety timeout. */
                xEventGroupWaitBits(g_system_event_group, SYSTEM_EVENT_CLOUD_SYNC_DONE,
                                    pdTRUE, pdFALSE, pdMS_TO_TICKS(120000));

                /* 4. Disconnect Wi-Fi to save power ONLY if we connected it in this task */
                if (!already_connected) {
                    wifi_manager_disconnect();
                }
            } else {
                ESP_LOGE(TAG, "Wi-Fi connection failed or timed out. Status: %d", wifi_manager_get_status());
            }
            
            if (ui_acquire()) {
                ui_set_sync_status(false);
                if (sync_ok) {
                    /* Show synchronization successful banner */
                    ui_show_notification(NOTIFY_SUCCESS, "Cloud Sync", "Synchronization Successful!", 4000);
                } else if (!connect_ok) {
                    ui_show_notification(NOTIFY_ERROR, "Cloud Sync", "Sync failed: Wi-Fi connection error.", 4000);
                } else {
                    ui_show_notification(NOTIFY_WARNING, "Cloud Sync", "Sync skipped: time not synchronized.", 4000);
                }
                ui_release();
            }
            set_system_state(SYSTEM_STATE_NORMAL);

            ESP_LOGI(TAG, "Sync cycle completed and disconnected");

        }
    }
}

/**
 * @brief Schedule checker task — polls every 10 s for an active class period.
 *        When a new active schedule is found, shows a clickable notification
 *        banner. Tapping the banner opens the Attendance Scanner app.
 */
static void schedule_checker_task(void *pvParameters) {
    static char s_last_notified_course[32] = {0};
    static time_t s_last_notified_time = 0;

    /* Give the system time to fully boot before the first check */
    vTaskDelay(pdMS_TO_TICKS(15000));

    while (1) {
        db_schedule_t active;
        if (db_get_active_schedule(&active) == ESP_OK) {
            /* Only notify once per course per start_time window */
            bool already_notified = (strcmp(s_last_notified_course, active.course_code) == 0 &&
                                     s_last_notified_time == (time_t)active.start_time);

            if (!already_notified) {
                strncpy(s_last_notified_course, active.course_code, sizeof(s_last_notified_course) - 1);
                s_last_notified_time = (time_t)active.start_time;

                ESP_LOGI(TAG, "Active schedule detected: %s — %s", active.course_code, active.course_name);

                /* Build notification message */
                static char msg[160];
                snprintf(msg, sizeof(msg), "%.60s (%.28s) has started. Tap to open scanner.",
                         active.course_name, active.course_code);

                if (ui_acquire()) {
                    ui_show_notification(NOTIFY_INFO, "Class Started", msg, 10000);
                    ui_release();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); /* check every 10 seconds */
    }
}

/**
 * @brief Battery monitoring task
 */
static void battery_task(void *pvParameters) {
    int battery_percent;
    
    while (1) {
        #if ENABLE_BATTERY_MONITOR
        battery_percent = battery_monitor_get_percent();
        bool is_charging = battery_monitor_is_charging();   /* reads cached s_charging */
        ui_set_battery_percent(battery_percent, is_charging);
        
        /* Only trigger critical battery if NOT charging */
        static int shutdown_strikes = 0; 
        if (battery_percent <= BATTERY_SHUTDOWN_THRESHOLD && !battery_monitor_is_charging()) {
            if (++shutdown_strikes >= 3) { /* 30 seconds of critical battery before shutdown */
                xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_BATTERY_CRITICAL);
            }
        } else {
            /* Reset debounce if charging or voltage recovers */
            shutdown_strikes = 0;
            if (battery_percent <= BATTERY_WARNING_THRESHOLD && !battery_monitor_is_charging()) {
                xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_BATTERY_LOW);
            }
        }
        
        battery_monitor_check_idle_sleep();
        #endif
        
        /* Update Wi-Fi status icon on the status bar (top-right) */
        ui_set_wifi_status(wifi_manager_get_status() == WIFI_STATUS_CONNECTED, wifi_manager_get_rssi());
        
        vTaskDelay(pdMS_TO_TICKS(BATTERY_CHECK_INTERVAL_MS));
    }
}

/**
 * @brief System state machine - handles transitions and events
 */
static void system_state_machine(void) {
    EventBits_t bits = xEventGroupGetBits(g_system_event_group);
    
    if (bits & SYSTEM_EVENT_BATTERY_CRITICAL) {
        if (get_system_state() != SYSTEM_STATE_SHUTDOWN) {
            handle_low_battery();
            graceful_shutdown();
        }
    } else if (bits & SYSTEM_EVENT_BATTERY_LOW) {
        if (get_system_state() == SYSTEM_STATE_NORMAL) {
            if (ui_acquire()) {
                ui_show_notification(NOTIFY_WARNING, "Battery Low", "Please connect charger", 5000);
                ui_release();
            }
        }
    }
    
    /* Clear processed events */
    xEventGroupClearBits(g_system_event_group, 
                         SYSTEM_EVENT_BATTERY_LOW | SYSTEM_EVENT_BATTERY_CRITICAL);
}

void system_halt_for_reset(void) {
    set_system_state(SYSTEM_STATE_SHUTDOWN);
    /* Give up to 500ms for all tasks to realize shutdown state and exit active loops */
    vTaskDelay(pdMS_TO_TICKS(500));
}

/**
 * @brief Handle low battery condition
 */
static void handle_low_battery(void) {
    set_system_state(SYSTEM_STATE_LOW_BATTERY);
    ESP_LOGW(TAG, "Low battery - shutting down soon");
    if (ui_acquire()) {
        ui_show_notification(NOTIFY_ERROR, "Battery Critical", "System shutting down", 3000);
        ui_release();
    }
    
    /* Give user time to see warning */
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
 * @brief Graceful system shutdown
 * Issue 5.5: Added sdcard_unmount() to prevent WAL corruption.
 */
static void graceful_shutdown(void) {
    ESP_LOGI(TAG, "Performing graceful shutdown");
    
    /* Save any pending data */
    db_manager_flush();
    
    /* Turn off peripherals */
    camera_deinit();
    board_backlight_set(0);
    
    /* Power off (if supported) */
    #if ENABLE_BATTERY_MONITOR
    battery_monitor_shutdown();
    #endif

    /* Unmount SD card to prevent WAL data loss (Issue 5.5) */
    sdcard_unmount();
    
    /* 8. First-time setup gracefully omitted if handled at boot */
    if (recognizer_get_cache_size() == 0) {
        while (1) {
            vTaskSuspend(NULL);
        }
    }
}

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
 * @brief Camera frame structure
 */
typedef struct {
    camera_fb_t *fb;
    uint32_t timestamp_ms;
} camera_frame_t;

/* Forward declarations */
static void camera_task(void *pvParameters);
static void detection_recognition_task(void *pvParameters);
static void db_task(void *pvParameters);
static void network_sync_task(void *pvParameters);
static void battery_task(void *pvParameters);
static void network_ble_init_task(void *pvParameters);
static void system_state_machine(void);
static esp_err_t process_recognition_result(user_t *user, float confidence);
void start_enrollment_task(void *pvParam);
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
            xTaskCreate(start_enrollment_task, "enrollment", 8192, NULL, 5, NULL);
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
    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %d", ret);
        return;
    }
    ESP_LOGI(TAG, "Camera initialized");

    /* 2. Face detector */
    ret = face_detector_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Face detector init failed");
        return;
    }
    ESP_LOGI(TAG, "Face detector initialized");

    /* 3. Face recognizer (loads embedding model) */
    ret = recognizer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Recognizer init failed");
        return;
    }
    ESP_LOGI(TAG, "Face recognizer initialized");

    /* 4. Database manager (SQLite on SD card) */
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

    /* 6. UI (LVGL) - must be after display init */
    ret = ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UI init failed");
        return;
    }
    ui_register_nav_callback(ui_nav_callback_handler);
    ESP_LOGI(TAG, "UI initialized");

    /* 7. Battery monitor — always init, no external deps */
    #if ENABLE_BATTERY_MONITOR
    ret = battery_monitor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor init failed");
    }
    #endif

    /* 8. BLE + WiFi + Cloud Sync — deferred to background task.
     *
     * IMPORTANT: Do NOT call esp_hosted_connect_to_slave() here in app_main.
     * The ESP-Hosted component auto-initialises the SDIO transport at boot.
     * Calling connect_to_slave() again just re-triggers SDIO reconfiguration,
     * which can race with the already-running transport task.
     *
     * More importantly: if the C6 slave chip is unavailable (e.g. missing
     * firmware), the SDIO driver used to call esp_restart() causing an
     * infinite boot-loop that prevented the UI from ever appearing.
     * CONFIG_ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE=n in sdkconfig.defaults
     * disables that restart, but we also move all hosted-dependent init into
     * a background task so that a slow/absent C6 cannot delay app_main.
     */
    xTaskCreate(network_ble_init_task, "net_ble_init",
                TASK_NETWORK_STACK_SIZE, NULL,
                2,   /* lowest priority — runs only when core tasks are idle */
                NULL);

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
 * Continuously captures frames from OV5647 and queues them for processing
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
                    /* Copy camera frame buffer safely using its actual length */
                    memcpy(s_detection_fb_buf, fb->buf, fb->len);
                    
                    static camera_fb_t fb_copy;
                    fb_copy = *fb;
                    fb_copy.buf = s_detection_fb_buf;
                    
                    frame.fb = &fb_copy;
                    frame.timestamp_ms = esp_timer_get_time() / 1000;
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
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* Run face detection */
        detection_result_t det_result;
        if (face_detector_run(frame.fb, &det_result) != ESP_OK) {
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* If no face detected, update UI and continue */
        if (det_result.face_count == 0) {
            if (ui_acquire()) {
                ui_update_detection_bounding_box(0, 0, 0, 0, false);
                ui_release();
            }
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* Take the largest face (first in list) */
        detected_face_t *face = &det_result.faces[0];
        
        /* Skip low confidence detections */
        if (face->confidence < FACE_DETECT_CONFIDENCE_MIN) {
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* Align face to 112x112 canonical view */
        if (face_alignment_align(frame.fb, face, &aligned_face) != ESP_OK) {
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* Extract embedding */
        if (feature_extractor_run(&aligned_face, &embedding) != ESP_OK) {
            face_alignment_free(&aligned_face);
            camera_return_frame(frame.fb);
            continue;
        }
        
        /* Match against database */
        recognizer_identify(&embedding, &matched_user, &confidence);
        
        ESP_LOGI(TAG, "Recognition run: matched_user=%s, confidence=%.3f (threshold=%.2f)",
                 (matched_user != NULL) ? matched_user->name : "None/Unknown", confidence, RECOGNITION_THRESHOLD);
        
        /* Update UI with bounding box and recognition result (scaled to 480x360 UI preview size) */
        if (ui_acquire()) {
            int scaled_x = (frame.fb->width > 0) ? (face->x * 480 / frame.fb->width) : face->x;
            int scaled_y = (frame.fb->height > 0) ? (face->y * 360 / frame.fb->height) : face->y;
            int scaled_w = (frame.fb->width > 0) ? (face->w * 480 / frame.fb->width) : face->w;
            int scaled_h = (frame.fb->height > 0) ? (face->h * 360 / frame.fb->height) : face->h;
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
        camera_return_frame(frame.fb);
        

        
        /* Progress log */
        static int det_cnt = 0;
        if (++det_cnt % 10 == 0) esp_rom_printf("P");
    }
}

/**
 * @brief Process successful recognition and log attendance
 * Issue 3.2: Heap-allocates log data so pointer survives queue transit.
 * Issue 3.1: Generates proper hex UUID string.
 * Issue 3.3: Uses strncpy for status field instead of string literal initializer.
 */
static esp_err_t process_recognition_result(user_t *user, float confidence) {
    /* Heap-allocate so the pointer remains valid after this function returns (Issue 3.2) */
    attendance_log_t *log = calloc(1, sizeof(attendance_log_t));
    if (!log) {
        ESP_LOGE(TAG, "Failed to allocate attendance log");
        return ESP_ERR_NO_MEM;
    }

    log->user_id = user->id;
    log->schedule_id = db_get_current_schedule_id();
    log->timestamp = time(NULL);
    strncpy(log->status, "present", sizeof(log->status) - 1);  /* Issue 3.3 */
    log->status[sizeof(log->status) - 1] = '\0';
    log->synced = 0;
    
    /* Generate proper hex UUID (Issue 3.1) */
    generate_uuid_hex(log->uuid, sizeof(log->uuid));
    
    /* Queue database insert — db_task will free the data */
    db_request_t req = {
        .type = DB_REQUEST_INSERT_LOG,
        .data = log,
        .data_len = sizeof(attendance_log_t),
        .free_data = true
    };
    
    if (xQueueSend(g_db_request_queue, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(log);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Start enrollment mode
 * Called from UI when user taps "Enroll" button
 */
static esp_err_t process_enrollment_frames_for_user(user_t* new_user) {
    /* Heap-allocate large arrays to prevent stack overflow */
    camera_fb_t **frames = (camera_fb_t**)calloc(ENROLL_FRAMES_TOTAL, sizeof(camera_fb_t *));
    aligned_face_t *aligned_frames = (aligned_face_t*)calloc(ENROLL_FRAMES_TOTAL, sizeof(aligned_face_t));
    face_embedding_t *embeddings = (face_embedding_t*)calloc(ENROLL_FRAMES_TOTAL, sizeof(face_embedding_t));
    float *quality_scores = (float*)calloc(ENROLL_FRAMES_TOTAL, sizeof(float));

    if (!frames || !aligned_frames || !embeddings || !quality_scores) {
        ESP_LOGE(TAG, "Failed to allocate enrollment buffers");
        free(frames); free(aligned_frames); free(embeddings); free(quality_scores);
        return ESP_ERR_NO_MEM;
    }

    int kept = 0;
    esp_err_t ret = ESP_OK;
    
    /* Drain any stale frame already queued in the binary semaphore */
    xSemaphoreTake(camera_get_frame_sem(), 0);
    
    ESP_LOGI(TAG, "Starting enrollment for %s, capturing %d frames", new_user->name, ENROLL_FRAMES_TOTAL);
    
    camera_set_framesize(CAMERA_ENROLL_FRAME_SIZE);

    #if 0  /* ENABLE_BLE_ENROLLMENT removed 2026-06-12 — Web AP is the only transport */
    if (false) {
        ble_registration_update_status(ENROLL_STATUS_CAPTURING, "Capturing face frames...");
    }
    #endif
    
    /* Capture burst of frames. Wait if face is not detected. */
    int i = 0;
    int progress_update_count = 0;  /* Only update UI every 3 iterations to reduce LVGL contention */
    
    while (i < ENROLL_FRAMES_TOTAL) {
        if (g_enrollment_cancel) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        
        /* ✅ Update UI only every 3 iterations to reduce LVGL mutex contention */
        if (++progress_update_count % 3 == 0) {
            if (ui_acquire()) {
                ui_enrollment_set_capture_progress(i, ENROLL_FRAMES_TOTAL);
                ui_release();
            }
        }
        
        /* Capture frame with autofocus */
        camera_fb_t* fb = camera_capture_with_autofocus();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Frame %d capture failed", i);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        ESP_LOGI(TAG, "Captured frame %d successfully, width=%d height=%d", i, fb->width, fb->height);
        
        /* ✅ Update camera preview (heavy operation, mutex held briefly for invalidation only) */
        ui_update_enrollment_camera_frame(fb->buf, fb->width, fb->height);
        
        /* ✅ Yield briefly after frame capture to let LVGL task render */
        vTaskDelay(pdMS_TO_TICKS(5));
        
        /* Detect face in frame - expensive ML inference */
        detection_result_t det_result;
        esp_err_t det_err = face_detector_run(fb, &det_result);
        if (det_err != ESP_OK || det_result.face_count == 0) {
            camera_return_frame(fb);
            if (ui_acquire()) {
                ui_enrollment_set_face_detected(false);
                ui_release();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue; /* Do not increment 'i', force user to show face */
        }
        
        /* ✅ Yield after face detection to prevent LVGL task starvation */
        vTaskDelay(pdMS_TO_TICKS(5));
        
        if (ui_acquire()) {
            ui_enrollment_set_face_detected(true);
            ui_release();
        }

        detected_face_t *face = &det_result.faces[0];
        
        /* Compute quality metrics */
        float sharpness = face_detector_compute_sharpness(fb, face);
        float brightness = face_detector_compute_brightness(fb, face);
        float yaw = face_detector_compute_yaw(face);
        
        quality_scores[i] = (sharpness / 100.0f) * 0.5f;
        quality_scores[i] += (1.0f - (fabs(yaw) / ENROLL_YAW_MAX_DEG)) * 0.3f;
        quality_scores[i] += (brightness >= ENROLL_BRIGHTNESS_MIN && 
                              brightness <= ENROLL_BRIGHTNESS_MAX) ? 0.2f : 0.0f;
        
        /* ✅ Align face - yield after heavy operation */
        if (face_alignment_align(fb, face, &aligned_frames[i]) != ESP_OK) {
            camera_return_frame(fb);
            continue;
        }
        
        /* ✅ Yield after alignment to let LVGL task render */
        vTaskDelay(pdMS_TO_TICKS(5));
        
        frames[i] = fb;
        i++; /* Frame successfully captured and aligned */
    }
    
    /* (BLE status update removed 2026-06-12) */
    
    /* Select best frames */
    int selected_indices[ENROLL_FRAMES_KEEP];
    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        selected_indices[j] = -1;
    }
    
    for (int j = 0; j < ENROLL_FRAMES_TOTAL; j++) {
        if (frames[j] == NULL) continue;
        for (int k = 0; k < ENROLL_FRAMES_KEEP; k++) {
            if (selected_indices[k] == -1 || quality_scores[j] > quality_scores[selected_indices[k]]) {
                for (int m = ENROLL_FRAMES_KEEP - 1; m > k; m--) {
                    selected_indices[m] = selected_indices[m-1];
                }
                selected_indices[k] = j;
                break;
            }
        }
    }
    
    /* Extract embeddings - yield after each heavy operation */
    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        int idx = selected_indices[j];
        if (idx == -1) continue;
        if (feature_extractor_run(&aligned_frames[idx], &embeddings[idx]) != ESP_OK) {
            selected_indices[j] = -1;
            continue;
        }
        kept++;
        /* ✅ Yield after each embedding extraction to let LVGL task render */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (kept == 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    /* Compute target norm as the average norm of all kept embeddings,
     * and compute the quality-weighted average vector. */
    float target_norm = 0.0f;
    float sum_norms = 0.0f;
    float weighted_sum[EMBEDDING_DIM] = {0.0f};
    float total_weight = 0.0f;

    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        int idx = selected_indices[j];
        if (idx == -1) continue;

        /* Calculate norm of this embedding */
        float norm_sq = 0.0f;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            float val = (float)embeddings[idx].values[m];
            norm_sq += val * val;
        }
        sum_norms += sqrtf(norm_sq);

        /* Weighted accumulation */
        float weight = quality_scores[idx];
        total_weight += weight;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            weighted_sum[m] += (float)embeddings[idx].values[m] * weight;
        }
    }

    if (kept > 0) {
        target_norm = sum_norms / (float)kept;
    }

    face_embedding_t final_embedding;
    memset(&final_embedding, 0, sizeof(face_embedding_t));

    if (total_weight > 0.0f) {
        float avg_vector[EMBEDDING_DIM] = {0.0f};
        float avg_norm_sq = 0.0f;

        /* Calculate raw weighted average and its norm */
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            avg_vector[m] = weighted_sum[m] / total_weight;
            avg_norm_sq += avg_vector[m] * avg_vector[m];
        }
        float avg_norm = sqrtf(avg_norm_sq);

        /* Scale raw average to target norm, round, clip strictly, and store */
        float scale = (avg_norm > 1e-6f) ? (target_norm / avg_norm) : 0.0f;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            float scaled_val = avg_vector[m] * scale;
            int rounded = (int)roundf(scaled_val);
            if (rounded < -128) rounded = -128;
            if (rounded > 127)  rounded = 127;
            final_embedding.values[m] = (int8_t)rounded;
        }
        ESP_LOGI(TAG, "Master vector generated: target_norm=%.3f, avg_norm=%.3f, scale=%.3f", target_norm, avg_norm, scale);
        ESP_LOGI(TAG, "Master embedding sample: %d %d %d %d %d", 
                 final_embedding.values[0], final_embedding.values[1], 
                 final_embedding.values[2], final_embedding.values[3], 
                 final_embedding.values[4]);
    }
    
    /* Check if user is already enrolled */
    user_t *existing_user = NULL;
    float existing_confidence = 0.0f;
    recognizer_identify(&final_embedding, &existing_user, &existing_confidence);
    if (existing_user != NULL && existing_confidence >= RECOGNITION_THRESHOLD) {
        ESP_LOGW(TAG, "Face already enrolled under user: %s (confidence: %.2f)", existing_user->name, existing_confidence);
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    
    /* Store in database */
    new_user->embedding = final_embedding;
    new_user->created_at = time(NULL);
    generate_uuid_hex(new_user->uuid, sizeof(new_user->uuid));
    
    ret = db_insert_user(new_user);
    if (ret == ESP_OK) {
        recognizer_add_to_cache(new_user);
        /* ✅ Yield after cache update */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
cleanup:
    camera_set_framesize(CAMERA_FRAME_SIZE);
    for (int j = 0; j < ENROLL_FRAMES_TOTAL; j++) {
        if (frames[j]) camera_return_frame(frames[j]);
        if (aligned_frames[j].data) face_alignment_free(&aligned_frames[j]);
    }
    free(frames);
    free(aligned_frames);
    free(embeddings);
    free(quality_scores);
    
    camera_set_framesize(CAMERA_FRAME_SIZE);
    return ret;
}

void start_single_capture_task(void *pvParam) {
    int student_idx = (int)(intptr_t)pvParam;
    
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
 * @brief Deferred network initialisation task.
 *
 * Runs at the lowest priority after app_main completes. Waits for the
 * ESP-Hosted SDIO transport to settle, then initialises the enrollment queue,
 * Wi-Fi manager, and cloud sync. Any failure is logged but does NOT affect
 * core attendance functions.
 */
static void network_ble_init_task(void *pvParameters) {
    /* Give the SDIO transport task time to attempt its first connection.
     * The hosted tasks start during component init (before app_main);
     * 5 s is more than enough for a connected C6 to complete handshake. */
    ESP_LOGI(TAG, "NET_BLE: waiting 5s for SDIO transport to settle...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    esp_err_t r;

    /* --- Enrollment queue --- */
    r = ble_registration_init();
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "NET: Enrollment queue init failed (%d)", r);
    } else {
        ESP_LOGI(TAG, "NET: Enrollment queue ready");
    }

    /* --- Cloud Sync / Wi-Fi --- */
    #if ENABLE_CLOUD_SYNC
    r = cloud_sync_init();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "NET_BLE: Cloud sync init failed (%d)", r);
    }

    r = wifi_manager_init();
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "NET_BLE: Wi-Fi init failed (%d) - cloud sync disabled", r);
    } else {
        ESP_LOGI(TAG, "NET_BLE: Wi-Fi ready; starting network sync task");
        xTaskCreate(network_sync_task, "network_sync", TASK_NETWORK_STACK_SIZE,
                    NULL, TASK_NETWORK_PRIORITY, NULL);
    }
    #endif

    ESP_LOGI(TAG, "NET_BLE: init task done");
    vTaskDelete(NULL);
}

/**
 * @brief Network sync task - periodic and manual cloud synchronization
 */
static void network_sync_task(void *pvParameters) {
    while (1) {
        uint32_t sync_interval_ms = CLOUD_SYNC_INTERVAL_MS;
        nvs_handle_t nvs_int;
        if (nvs_open("storage", NVS_READONLY, &nvs_int) == ESP_OK) {
            nvs_get_u32(nvs_int, "sync_ms", &sync_interval_ms);
            nvs_close(nvs_int);
        }
        
        ESP_LOGI(TAG, "Sync task waiting. Interval: %lu ms", (unsigned long)sync_interval_ms);
        
        if (sync_interval_ms == 0) {
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
                /* 2. Sync Time (SNTP) if not already synchronized */
                bool sync_ok = false;
                if (sntp_sync_is_synchronized()) {
                    sync_ok = true;
                } else {
                    esp_err_t init_err = sntp_sync_init();
                    if (init_err == ESP_OK || init_err == ESP_ERR_INVALID_STATE) {
                        if (sntp_sync_wait_for_sync(10000) == ESP_OK) {
                            sync_ok = true;
                        } else {
                            ESP_LOGW(TAG, "SNTP time sync timed out, proceeding with cloud sync anyway");
                            sync_ok = true;
                        }
                    } else {
                        ESP_LOGW(TAG, "SNTP initialization failed, proceeding with cloud sync anyway");
                        sync_ok = true;
                    }
                }

                
                if (sync_ok) {
                    /* 3. Run Cloud Sync (Telegram) — clear the done bit first */
                    xEventGroupClearBits(g_system_event_group, SYSTEM_EVENT_CLOUD_SYNC_DONE);
                    cloud_sync_start();

                    /* [Fix M2] Wait on event bit set by cloud_sync_task instead of
                     * a hardcoded vTaskDelay(10000). Use a 30s ceiling as a safety timeout. */
                    xEventGroupWaitBits(g_system_event_group, SYSTEM_EVENT_CLOUD_SYNC_DONE,
                                        pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
                }
                
                /* 4. Disconnect Wi-Fi to save power ONLY if we connected it in this task */
                if (!already_connected) {
                    wifi_manager_disconnect();
                }
            } else {
                ESP_LOGE(TAG, "Wi-Fi connection failed or timed out. Status: %d", wifi_manager_get_status());
            }
            
            if (ui_acquire()) {
                ui_set_sync_status(false);
                ui_release();
            }
            set_system_state(SYSTEM_STATE_NORMAL);
            
            ESP_LOGI(TAG, "Sync cycle completed and disconnected");
        }
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

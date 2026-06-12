# SmartAttendance — Unified Consumer Readiness Analysis & Fix Guide
> Combined audit evaluation + loophole analysis | April 2026
> **33 issues** — all with concrete, production-grade fixes

---

## Legend

| Badge | Meaning |
|---|---|
| ✅ | Already fixed in current code |
| 💥 | Crash / data loss |
| 😤 | Silent failure |
| 🔐 | Security theatre |
| 🧱 | UX dead-end |
| ♻️ | Reliability trap |
| 🟡 | Partially fixed / incomplete |
| 🔴 | Audit item still open |

---

## Executive Summary

| Category | Count |
|---|---|
| ✅ Already Fixed (audit items) | 16 |
| 💥 Crashes / Data Loss | 4 |
| 😤 Silent Failures | 7 |
| 🔐 Security Theatre | 4 |
| 🧱 UX Dead-Ends | 5 |
| ♻️ Reliability Traps | 5 |
| 🟡 Partially Fixed | 8 |
| 🔴 Audit Items Still Open | 6 |

---

## What Is Already Working ✅

The following audit items are fully resolved in the current codebase and require no further action:

| Item | Status |
|---|---|
| `recognizer_load_cache()` populated from DB | ✅ |
| Feature extractor pixel format (`RGB565LE`) | ✅ |
| Real sharpness / brightness quality metrics | ✅ |
| BLE data read into enrollment struct | ✅ |
| Duplicate attendance guard (SQL pre-check) | ✅ |
| Detection task gated during enrollment | ✅ |
| LVGL tick driver + mutex-guarded handler | ✅ |
| SNTP init after Wi-Fi connect | ✅ |
| HTTP response buffer off-by-one | ✅ |
| Admin PIN + face-unlock session | ✅ |
| SD card missing banner | ✅ |
| LVGL thread safety (`ui_acquire`/`ui_release`) | ✅ |
| All task stack sizes updated | ✅ |
| WAV RIFF chunk search parser | ✅ |
| `esp_hosted` dependency declared | ✅ |
| Brief-connect Wi-Fi sync flow | ✅ |

---

## TIER 1 — Ship Blockers
> Fix before any hardware testing. These fail immediately on first use.

---

### 💥 [T1-1] Enrollment Close Button Crashes the Device

**Files:** `ui_enrollment.cpp:173`, `main.c:730–791`

**Problem:** Tapping Close deletes all LVGL objects and nulls their pointers while `start_enrollment_task` still runs in another FreeRTOS task. Subsequent calls to `ui_update_enrollment_progress()` dereference `s_progress_bar` → `NULL` → guaranteed crash.

**Fix:**
```c
// main.c — add flag:
volatile bool g_enrollment_cancel = false;

// start_enrollment_task — check at top of loop:
for (int i = 0; i < ENROLL_FRAMES_TOTAL; i++) {
    if (g_enrollment_cancel) goto cleanup;
    // ... existing frame capture ...
}

cleanup:
    g_enrollment_cancel = false;
    set_system_state(SYSTEM_STATE_NORMAL);
    ui_acquire();
    ui_close_enrollment_screen();
    ui_release();
    vTaskDelete(NULL);

// close_btn_event_handler — only set the flag, never touch LVGL here:
static void close_btn_event_handler(lv_event_t* e) {
    g_enrollment_cancel = true;  // task will clean up + close screen
}
```

---

### 😤 [T1-2] Setup Wizard PIN Is Silently Discarded — All Devices Ship With "12345678"

**File:** `ui_setup_wizard.cpp:154–178`, `main.c:69`

**Problem:** "Finish Setup" calls `next_btn_event_handler` which writes `setup_done=1` but never reads the PIN textarea. `DEFAULT_ADMIN_PIN = "12345678"` is the permanent PIN for every device.

**Fix:**
```c
// Add at file scope:
static lv_obj_t* s_pin_ta = NULL;

// create_pin_page() — store reference:
s_pin_ta = pin_ta;

// Replace finish button callback:
static void wizard_finish_handler(lv_event_t* e) {
    const char* pin = s_pin_ta ? lv_textarea_get_text(s_pin_ta) : NULL;
    if (!pin || strlen(pin) < 4) {
        ui_show_notification(NOTIFY_WARNING, "PIN Required",
                             "Enter a 4-8 digit PIN", 2000);
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "admin_pin", pin);
        nvs_set_u8(nvs, "setup_done", 1);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ui_return_to_main();
}
lv_obj_add_event_cb(finish_btn, wizard_finish_handler, LV_EVENT_CLICKED, NULL);
```

Also add a "Change PIN" option in the Settings device card for post-setup changes.

---

### 😤 [T1-3] Setup Wizard Admin Enrollment Is a No-Op

**File:** `ui_setup_wizard.cpp:133–152`

**Problem:** "Enroll Admin Face" button immediately calls `next_btn_event_handler`. No face capture occurs. `setup_done=1` is written with zero admin users in the database.

**Fix:**
```c
// Add global:
static volatile char g_enrollment_role_override[16] = {0};

// Replace button handler:
static void wizard_enroll_admin_event(lv_event_t* e) {
    strncpy((char*)g_enrollment_role_override, "admin",
            sizeof(g_enrollment_role_override));
    set_system_state(SYSTEM_STATE_ENROLLMENT);
    xTaskCreate(start_enrollment_task, "enroll_admin",
                TASK_DETECTION_STACK_SIZE, NULL, 5, NULL);
    // Wizard advances when task sets WIZARD_ENROLL_DONE_BIT
}
lv_obj_add_event_cb(start_btn, wizard_enroll_admin_event, LV_EVENT_CLICKED, NULL);

// In start_enrollment_task, after db_insert_user() succeeds:
if (strlen((char*)g_enrollment_role_override) > 0) {
    memset((char*)g_enrollment_role_override, 0,
           sizeof(g_enrollment_role_override));
    xEventGroupSetBits(g_system_event_group, WIZARD_ENROLL_DONE_BIT);
}
```

---

### 😤 [T1-4] Setup Wizard Wi-Fi Credentials Are Silently Discarded

**File:** `ui_setup_wizard.cpp:96–98`

**Problem:** "Connect & Next" simply increments `s_current_step`. No reference to the SSID/password textareas is held. The user's credentials are discarded; the device never connects to Wi-Fi via the wizard.

**Fix:**
```c
static lv_obj_t* s_wizard_ssid_ta = NULL;
static lv_obj_t* s_wizard_pass_ta = NULL;

// create_wifi_page() — store references:
s_wizard_ssid_ta = ssid_ta;
s_wizard_pass_ta = pass_ta;

static void wizard_wifi_connect_handler(lv_event_t* e) {
    const char* ssid = s_wizard_ssid_ta
                       ? lv_textarea_get_text(s_wizard_ssid_ta) : "";
    const char* pass = s_wizard_pass_ta
                       ? lv_textarea_get_text(s_wizard_pass_ta) : "";
    if (ssid && strlen(ssid) > 0) {
        wifi_manager_connect(ssid, pass);  // save + initiate (non-blocking)
    }
    next_btn_event_handler(NULL);  // advance wizard regardless
}
lv_obj_add_event_cb(next_btn, wizard_wifi_connect_handler, LV_EVENT_CLICKED, NULL);
```

---

### 😤 [T1-5] Telegram Bot Token and Chat ID Are Placeholder Strings

**File:** `config.h:106–107`

**Problem:** `TELEGRAM_BOT_TOKEN = "YOUR_BOT_TOKEN"` is a compile-time literal. All sync HTTP calls get 401. No user notification. Cloud sync permanently non-functional out of the box.

**Fix — load from NVS at runtime:**
```c
// cloud_sync.c:
static char s_telegram_token[128] = {0};
static char s_telegram_chat[32]   = {0};

esp_err_t cloud_sync_init(void) {
    nvs_handle_t nvs;
    if (nvs_open("telegram", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(s_telegram_token);
        nvs_get_str(nvs, "token", s_telegram_token, &len);
        len = sizeof(s_telegram_chat);
        nvs_get_str(nvs, "chat_id", s_telegram_chat, &len);
        nvs_close(nvs);
    }
}

// Guard all sync operations:
if (strlen(s_telegram_token) == 0) {
    ESP_LOGW(TAG, "Telegram token not configured — skipping sync");
    ui_acquire();
    ui_show_notification(NOTIFY_WARNING, "Sync",
                         "Telegram bot not configured", 3000);
    ui_release();
    return ESP_ERR_INVALID_STATE;
}
```

Add **Bot Token** and **Chat ID** text fields to the Settings Wi-Fi card that write to NVS namespace `"telegram"`. Show a "Test Connection" button that calls `getMe` to validate the token.

---

### ♻️ [T1-6] Wi-Fi Retry Counter Permanently Breaks Sync After ~5 Sessions

**File:** `wifi_manager.c:44, 298–301`

**Problem:** `wifi_manager_disconnect()` fires `WIFI_EVENT_STA_DISCONNECTED`, incrementing `s_retry_count`. After 5 successful sync cycles, the counter equals `WIFI_MAX_RETRY` and reconnection is permanently blocked — silently.

**Fix:**
```c
esp_err_t wifi_manager_disconnect(void) {
    s_retry_count = 0;           // ← must reset before disconnect
    s_wifi_status = WIFI_STATUS_DISCONNECTED;
    return esp_wifi_disconnect();
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password) {
    s_retry_count = 0;           // ← reset on every new connect attempt
    // ... rest unchanged
}
```

---

### 💥 [T1-7] Factory Reset Runs While All Tasks Are Still Active

**File:** `ui_settings.cpp:410–414`

**Problem:** `db_factory_reset()` and `nvs_flash_erase()` run from an LVGL event callback while `camera_task`, `detection_recognition_task`, and `db_task` are all alive and potentially mid-write. Risk of SQLite WAL corruption and partial NVS erase.

**Fix:**
```c
static void factory_reset_confirm_cb(lv_event_t* e) {
    if ((uintptr_t)lv_event_get_user_data(e) == 0) {
        // 1. Stop all activity
        set_system_state(SYSTEM_STATE_SHUTDOWN);
        vTaskDelay(pdMS_TO_TICKS(250));  // let tasks drain

        // 2. Flush WAL and close DB cleanly
        db_manager_flush();

        // 3. Wipe DB tables
        db_factory_reset();

        // 4. Erase NVS
        nvs_flash_erase();

        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        lv_msgbox_close(lv_obj_get_parent(
            lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e))));
    }
}
```

Add guard at top of `detection_recognition_task` and `db_task`:
```c
if (get_system_state() == SYSTEM_STATE_SHUTDOWN) {
    vTaskDelay(pdMS_TO_TICKS(50));
    continue;
}
```

---

## TIER 2 — Reliability
> Fix before first deployment. These work once and then break.

---

### ♻️ [T2-1] Blocking Wi-Fi Scan Starves the System Event Task

**File:** `wifi_manager.c:122, 317–319`

**Problem:** `WIFI_EVENT_STA_START` → `wifi_manager_connect_saved()` → `esp_wifi_scan_start(NULL, true)`. The `true` flag blocks the system event task for 2–5 seconds, starving all other pending network events. Can trigger task watchdog.

**Fix — offload scan to a dedicated worker task:**
```c
static void connect_saved_task(void* arg) {
    wifi_ap_record_t scan_results[20];
    int count = wifi_manager_scan(scan_results, 20);
    // ... match and connect logic (existing wifi_manager_connect_saved body) ...
    vTaskDelete(NULL);
}

// In event_handler, WIFI_EVENT_STA_START:
xTaskCreate(connect_saved_task, "wifi_conn", 4096, NULL, 3, NULL);
// Remove direct wifi_manager_connect_saved() call
```

---

### ♻️ [T2-2] Sync Task Has No Handle — Double Execution Corrupts CSV

**File:** `cloud_sync.c:77–78`

**Problem:** `xTaskCreate(cloud_sync_task, ..., NULL)` discards the handle. Rapid button taps or a race between manual sync and periodic sync spawns two parallel tasks that both write the same CSV file simultaneously.

**Fix:**
```c
static TaskHandle_t s_sync_task_handle = NULL;

esp_err_t cloud_sync_start(void) {
    if (s_sync_task_handle != NULL) {
        ESP_LOGW(TAG, "Sync already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(cloud_sync_task, "cloud_sync",
                    TASK_NETWORK_STACK_SIZE, NULL,
                    TASK_NETWORK_PRIORITY,
                    &s_sync_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// At the END of cloud_sync_task, before vTaskDelete(NULL):
s_sync_task_handle = NULL;
vTaskDelete(NULL);
```

---

### ♻️ [T2-3] Deep Sleep Triggers During Active Operations

**File:** `battery_monitor.c:221–233`

**Problem:** `battery_monitor_check_idle_sleep()` calls `esp_deep_sleep_start()` without checking system state, DB queue depth, or sync status. A sync mid-write gets killed, risks WAL corruption. Also creates a perpetual 6-minute reboot loop when the room is empty (sleep 5 min → wake after 1 min → full boot → sleep again).

**Fix — gate sleep and switch to light sleep:**
```c
void battery_monitor_check_idle_sleep(void) {
    if (s_charging) return;
    if ((esp_timer_get_time() - s_last_activity_time) < IDLE_TIMEOUT_US) return;

    // Don't sleep during active operations
    if (get_system_state() != SYSTEM_STATE_NORMAL) return;
    if (cloud_sync_get_status() == SYNC_STATUS_IN_PROGRESS) return;
    if (uxQueueMessagesWaiting(g_db_request_queue) > 0) return;

    // Light sleep: RAM preserved, wakes on touch interrupt
    board_backlight_set(0);
    gpio_wakeup_enable(TOUCH_INT_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();

    // Execution resumes here on wake
    board_backlight_set(ui_get_brightness());
    battery_monitor_update_activity();
}
```

---

### ♻️ [T2-4] SQLite Accessed Concurrently Without a Database Mutex

**Files:** `db_manager.c`, `ui_user_manager.cpp`, `cloud_sync.c`

**Problem:** `db_task` (INSERT), LVGL event handlers (DELETE + SELECT for cache reload), and `cloud_sync_task` (SELECT JOIN) can all run on SQLite simultaneously. `SQLITE_BUSY` errors are returned as `ESP_FAIL` with no retry — attendance records are silently dropped.

**Fix — add a single serialisation mutex in db_manager:**
```c
// db_manager.c — add:
static SemaphoreHandle_t s_db_mutex = NULL;

esp_err_t db_manager_init(void) {
    s_db_mutex = xSemaphoreCreateMutex();
    configASSERT(s_db_mutex);
    // ...
}

// Helper macros for callers:
#define DB_LOCK()   xSemaphoreTake(s_db_mutex, pdMS_TO_TICKS(5000))
#define DB_UNLOCK() xSemaphoreGive(s_db_mutex)

// Wrap every public function, e.g.:
esp_err_t db_insert_attendance_log(attendance_log_t* log) {
    if (DB_LOCK() != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret = _db_insert_attendance_log_impl(log);
    DB_UNLOCK();
    return ret;
}
```

---

### 💥 [T2-5] Single Shared `camera_fb_t` Is a Frame Race Condition

**File:** `camera_driver.c:142, 275–288`

**Problem:** `s_native_fb` is a single static struct. `camera_task` enqueues `&s_native_fb` then immediately captures the next frame, overwriting `s_native_fb.buf`. `detection_recognition_task` is now processing stale or corrupted data.

**Fix — reduce queue to depth 1 and copy the struct on enqueue:**
```c
// config.h:
#define CAMERA_FRAME_QUEUE_SIZE  1   // only one frame in-flight

// main.c camera_task — the double-buffer in camera_driver already
// alternates s_frame_buffer_1 / s_frame_buffer_2 via DMA callback.
// Copy the *struct* (not the pixel bytes) into the queue:
camera_fb_t fb_copy = *fb;  // struct copy — buf still points to DMA buffer
camera_frame_t frame = { .fb = &fb_copy, .timestamp_ms = esp_timer_get_time() / 1000 };
if (xQueueSend(g_camera_frame_queue, &frame, 0) != pdTRUE) {
    camera_return_frame(fb);
}
```

With queue depth 1, a new frame is only captured after the previous one is consumed, eliminating the race.

---

### ♻️ [T2-6] Blocking Audio Freezes Enrollment Frame Capture

**File:** `main.c:736–745`, `audio_player.c:142–146`

**Problem:** `audio_play(..., false)` called from the enrollment loop blocks on I2S DMA `write()` with `portMAX_DELAY`. Camera frames accumulate / overflow the queue during 4 guidance prompts. Also, the fixed 500 ms `vTaskDelay` for blocking mode is wrong for most WAV files.

**Fix part 1 — fire-and-forget via queue in enrollment:**
```c
// In process_enrollment_frames():
audio_request_t req = {.blocking = false};
strncpy(req.filename, AUDIO_PROMPTS_PATH "look_straight.wav",
        sizeof(req.filename) - 1);
xQueueSend(g_audio_request_queue, &req, 0);  // audio_task handles it independently
```

**Fix part 2 — compute actual blocking duration from WAV header:**
```c
// audio_player.c — after finding "data" chunk:
uint32_t bytes_per_second = sample_rate * channels * (bits_per_sample / 8);
uint32_t duration_ms = (bytes_per_second > 0)
    ? (uint32_t)((uint64_t)data_chunk_size * 1000 / bytes_per_second)
    : 500;
if (blocking) {
    vTaskDelay(pdMS_TO_TICKS(duration_ms + 50));
}
```

---

## TIER 3 — Security
> Fix before any public or institutional deployment.

---

### 🔐 [T3-1] All Devices Share Admin PIN "12345678"

**Compound issue of T1-2 (wizard discards PIN) + hardcoded default.**

T1-2 fixes the wizard. Additionally, add a "Change PIN" button in Settings:
```c
// ui_settings.cpp — add to device_card:
lv_obj_t* change_pin_btn = ui_create_button(device_card, "Change PIN", 150, 40);
lv_obj_set_pos(change_pin_btn, 200, 45);
lv_obj_add_event_cb(change_pin_btn, [](lv_event_t* e) {
    // Show current PIN prompt first, then new PIN entry
    ui_show_pin_prompt(false, [](bool ok) {
        if (!ok) return;
        ui_show_new_pin_dialog([](const char* new_pin) {
            if (!new_pin || strlen(new_pin) < 4) return;
            nvs_handle_t nvs;
            if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
                nvs_set_str(nvs, "admin_pin", new_pin);
                nvs_commit(nvs); nvs_close(nvs);
                ui_show_notification(NOTIFY_SUCCESS, "PIN",
                                     "PIN updated", 2000);
            }
        });
    });
}, LV_EVENT_CLICKED, NULL);
```

---

### 🔐 [T3-2] BLE Enrollment PIN Is a Compile-Time Binary Constant

**File:** `ble_registration.c:40`

**Problem:** `#define ENROLLMENT_PIN "123456"` is visible in `strings firmware.bin`. Any student who obtains the binary can self-enroll over BLE without an admin present.

**Fix — random per-session PIN displayed on screen:**
```c
static char s_session_pin[7] = {0};

void ble_registration_start_advertising(void) {
    uint32_t r = esp_random() % 1000000;
    snprintf(s_session_pin, sizeof(s_session_pin), "%06" PRIu32, r);

    // Show PIN on device screen (admin reads it aloud to student)
    ui_acquire();
    char msg[32];
    snprintf(msg, sizeof(msg), "BLE PIN: %s", s_session_pin);
    ui_show_notification(NOTIFY_INFO, "Enrollment PIN", msg, 0);
    ui_release();
}

// In gatt_svr_chr_access CHAR_PIN handler:
s_authenticated = (strcmp(recv_pin, s_session_pin) == 0);
if (!s_authenticated) {
    ESP_LOGW(TAG, "Wrong BLE PIN attempt");
}
```

---

### 🔐 [T3-3] 5-Minute Admin Session Applies to the Enroll Button

**File:** `main.c:231–241`

**Problem:** Once an admin logs in or is recognised, the 5-minute session window also unlocks the Enroll button. Any student who walks up during this window can self-enroll.

**Fix — always require PIN for Enroll, regardless of active session:**
```c
case NAV_ENROLL:
    if (get_system_state() == SYSTEM_STATE_NORMAL) {
        // Always re-auth for the destructive enroll action
        s_target_nav_after_auth = NAV_ENROLL;
        ui_acquire();
        ui_show_pin_prompt(false, pin_auth_callback);
        ui_release();
    }
    break;
// Settings can still use session bypass — only Enroll requires re-auth
```

---

### 🔐 [T3-4] Wi-Fi Passwords Stored Plaintext in Unencrypted NVS

**File:** `partitions.csv`, `main.c:285`

**Problem:** NVS encryption code path is dead (`#ifdef CONFIG_NVS_ENCRYPTION` guard exists but `nvs_keys` partition is absent). Wi-Fi passwords are recoverable via USB with standard IDF tools.

**Fix — add the `nvs_keys` partition:**
```csv
# partitions.csv — add this line before the nvs entry:
nvs_keys, data, nvs_keys, 0x9000, 0x1000,
nvs,      data, nvs,      0xa000, 0x4000,
```

Remove the `#ifdef CONFIG_NVS_ENCRYPTION` guard in `main.c:285` and enable unconditionally. In `sdkconfig`, set:
```
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME_HMAC_DERIVED=y
```

---

## TIER 4 — UX Dead-Ends
> Fix before user testing.

---

### 🧱 [T4-1] Wi-Fi "Connected" Toast Is Always Green, Even on Wrong Password

**File:** `ui_settings.cpp:295–307`

**Problem:** `esp_wifi_connect()` returns `ESP_OK` when the attempt is *initiated*, not when it succeeds. The UI immediately shows success regardless of outcome.

**Fix — async polling on an LVGL timer:**
```c
static void wifi_connect_btn_event(lv_event_t* e) {
    const char* ssid = lv_textarea_get_text(s_wifi_ssid_input);
    const char* pass = lv_textarea_get_text(s_wifi_pass_input);
    if (!ssid || strlen(ssid) == 0) { /* warn */ return; }

    lv_label_set_text(s_wifi_status_label, "Connecting...");
    lv_obj_set_style_text_color(s_wifi_status_label,
                                lv_color_hex(0xFFAA44), 0);
    wifi_manager_connect(ssid, pass);

    // Poll result — do NOT show Connected yet
    lv_timer_create([](lv_timer_t* t) {
        wifi_status_t st = wifi_manager_get_status();
        if (st == WIFI_STATUS_CONNECTED) {
            lv_label_set_text(s_wifi_status_label, "Connected \u2713");
            lv_obj_set_style_text_color(s_wifi_status_label,
                                        lv_color_hex(0x44FF44), 0);
            ui_show_notification(NOTIFY_SUCCESS, "Wi-Fi",
                                 "Connected successfully", 2000);
            lv_timer_del(t);
        } else if (st == WIFI_STATUS_CONNECTION_FAILED) {
            lv_label_set_text(s_wifi_status_label, "Failed - check password");
            lv_obj_set_style_text_color(s_wifi_status_label,
                                        lv_color_hex(0xFF4444), 0);
            ui_show_notification(NOTIFY_ERROR, "Wi-Fi",
                                 "Connection failed", 3000);
            lv_timer_del(t);
        }
        // WIFI_STATUS_CONNECTING — keep polling
    }, 500, NULL);
}
```

---

### 🧱 [T4-2] Enrollment Failure Has No Retry Path

**File:** `ui_enrollment.cpp:253–256`

**Problem:** Enrollment failure shows a red message and a comment `/* Show retry button? For now, just leave message */`. The user must close and re-enter admin PIN to try again.

**Fix:**
```c
void ui_enrollment_set_status(bool success, const char* message) {
    lv_obj_clear_flag(s_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_status_label, message);
    if (success) {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x44FF44), 0);
        lv_timer_create(close_enrollment_timer_cb, 2000, NULL);
    } else {
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xFF4444), 0);
        lv_obj_t* retry = ui_create_button(s_enroll_screen, "Try Again", 160, 50);
        lv_obj_align(retry, LV_ALIGN_BOTTOM_MID, -90, -20);
        lv_obj_add_event_cb(retry, [](lv_event_t* e) {
            g_enrollment_cancel = false;
            xTaskCreate(start_enrollment_task, "enroll_retry",
                        TASK_DETECTION_STACK_SIZE, NULL, 5, NULL);
            lv_obj_del(lv_event_get_target(e));  // remove retry button
        }, LV_EVENT_CLICKED, NULL);
    }
}
```

---

### 🧱 [T4-3] Setup Wizard Triggers at Shutdown, Not on First Boot

**File:** `main.c:~1085` (`graceful_shutdown`)

**Problem:** `ui_show_setup_wizard()` is called from `graceful_shutdown()`, which only runs on critically low battery. A freshly flashed device with adequate battery never shows the wizard.

**Fix — check NVS `setup_done` flag in `app_main` before the main loop:**
```c
// app_main() — after all subsystems initialise, before while(1):
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
    }
}
```

Remove the duplicate call from `graceful_shutdown()`.

---

### 🧱 [T4-4] Sync Interval Dropdown Has No Event Handler and No Effect

**File:** `ui_settings.cpp:195–208`

**Problem:** Dropdown is rendered but has no `lv_obj_add_event_cb`. Selection change does nothing. `CLOUD_SYNC_INTERVAL_MS` is a compile-time constant.

**Fix:**
```c
static const uint32_t k_interval_ms[] = {
    3600000, 21600000, 43200000, 86400000, 0
};

lv_obj_add_event_cb(s_sync_interval_dropdown, [](lv_event_t* e) {
    uint16_t sel = lv_dropdown_get_selected(s_sync_interval_dropdown);
    uint32_t ms  = k_interval_ms[sel];
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u32(nvs, "sync_ms", ms);
        nvs_commit(nvs); nvs_close(nvs);
    }
    cloud_sync_set_interval(ms);  // runtime update
}, LV_EVENT_VALUE_CHANGED, NULL);

// Load saved value when screen opens:
uint32_t saved = 21600000;
nvs_handle_t nvs;
if (nvs_open("storage", NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u32(nvs, "sync_ms", &saved);
    nvs_close(nvs);
}
for (int i = 0; i < 5; i++) {
    if (k_interval_ms[i] == saved) {
        lv_dropdown_set_selected(s_sync_interval_dropdown, i);
        break;
    }
}
```

---

### 🧱 [T4-5] Screen Brightness Reverts to 100% on Every Boot

**File:** `ui_main.cpp:161`

**Problem:** `backlight_set(100)` is always called at init, overriding any saved preference. In a darkened room, this is a blinding flash on every boot and deep-sleep wake.

**Fix — one-line change:**
```c
// Replace:
backlight_set(100);

// With:
backlight_set(ui_get_brightness());  // reads NVS, default 80 if not set
```

---

## TIER 5 — Functional Completeness
> Required for the device to fulfil its core purpose.

---

### 🔴 [T5-1] Attendance CSV Not Actually Sent via Telegram

**File:** `cloud_sync.c:286–329`

**Problem:** `sync_attendance_logs()` calls `sendMessage` sending a text notification ("Attendance logs exported to SD card"). The CSV file never reaches the admin.

**Fix — implement `sendDocument` multipart POST:**
```c
static esp_err_t upload_csv_to_telegram(const char* file_path) {
    FILE* f = fopen(file_path, "rb");
    if (!f) return ESP_FAIL;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    char* csv_data = heap_caps_malloc(file_size + 1, MALLOC_CAP_SPIRAM);
    if (!csv_data) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(csv_data, 1, file_size, f);
    csv_data[file_size] = '\0';
    fclose(f);

    const char* boundary = "ESP32Boundary";
    size_t body_cap = file_size + 512;
    char* body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM);
    if (!body) { free(csv_data); return ESP_ERR_NO_MEM; }

    int body_len = snprintf(body, body_cap,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"document\"; "
        "filename=\"attendance.csv\"\r\n"
        "Content-Type: text/csv\r\n\r\n%s\r\n"
        "--%s--\r\n",
        boundary, s_telegram_chat,
        boundary, csv_data,
        boundary);
    free(csv_data);

    char url[256];
    snprintf(url, sizeof(url), "%s%s/sendDocument",
             TELEGRAM_API_BASE, s_telegram_token);

    char ct[64];
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", ct);
    esp_http_client_set_post_field(client, body, body_len);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    free(body);
    return err;
}
```

---

### 🔴 [T5-2] Schedule CSV Import Is a Stub — No DB INSERT

**File:** `db_manager.c:372–417`

**Problem:** `db_import_schedule_csv()` parses lines but the comment `// logic to manage SQLite inserts...` shows the INSERT was never written.

**Fix:**
```c
if (name && code && start_s && end_s) {
    uint32_t start_t = strtoul(start_s, NULL, 10);
    uint32_t end_t   = strtoul(end_s,   NULL, 10);

    sqlite3_stmt* stmt;
    // Upsert course
    const char* sql_c =
        "INSERT OR IGNORE INTO courses "
        "(uuid, name, code, created_at) "
        "VALUES (lower(hex(randomblob(16))), ?, ?, "
        "strftime('%s','now'))";
    if (sqlite3_prepare_v2(s_db, sql_c, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, code, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // Insert schedule entry
    const char* sql_s =
        "INSERT OR REPLACE INTO schedule "
        "(uuid, course_id, start_time, end_time, location, created_at) "
        "VALUES (lower(hex(randomblob(16))), "
        " (SELECT id FROM courses WHERE code=?), "
        " ?, ?, ?, strftime('%s','now'))";
    if (sqlite3_prepare_v2(s_db, sql_s, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, code,           -1, SQLITE_STATIC);
        sqlite3_bind_int (stmt, 2, (int)start_t);
        sqlite3_bind_int (stmt, 3, (int)end_t);
        sqlite3_bind_text(stmt, 4, loc ? loc : "", -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    ESP_LOGI(TAG, "Imported schedule entry: %s (%s)", name, code);
}
```

---

### 🔴 [T5-3] Clock Displays "Jan 1, 1970" Until SNTP Synchronises

**File:** `ui_main.cpp:326–333`

**Fix:**
```c
static void update_time_task(lv_timer_t* timer) {
    time_t now = time(NULL);
    // Epoch guard: any timestamp before 2024-01-01 is invalid
    if (now < 1704067200UL) {
        lv_label_set_text(s_time_label, "--:--");
        lv_label_set_text(s_date_label, "Syncing time...");
        return;
    }
    struct tm* tm_info = localtime(&now);
    if (!tm_info) return;
    ui_update_time(tm_info->tm_hour, tm_info->tm_min);
    ui_update_date(tm_info->tm_mday,
                   tm_info->tm_mon + 1,
                   tm_info->tm_year + 1900);
}
```

---

### 🔴 [T5-4] Wi-Fi Multi-Network Support Is Broken — Only Slot 0 Ever Written

**File:** `wifi_manager.c:258–277`

**Problem:** The rotation logic is replaced with a comment. Every call to `wifi_manager_save_credentials()` overwrites `s_0`. `count` increments, creating phantom entries in the Known Networks list.

**Fix — proper LRU rotation:**
```c
esp_err_t wifi_manager_save_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return ESP_FAIL;

    int32_t count = 0;
    nvs_get_i32(nvs, NVS_KEY_COUNT, &count);

    // Check if SSID already exists — update password in-place
    for (int i = 0; i < count; i++) {
        char key[16]; char existing[32]; size_t len = sizeof(existing);
        snprintf(key, sizeof(key), "s_%d", i);
        if (nvs_get_str(nvs, key, existing, &len) == ESP_OK
                && strcmp(existing, ssid) == 0) {
            snprintf(key, sizeof(key), "p_%d", i);
            nvs_set_str(nvs, key, password ? password : "");
            nvs_commit(nvs); nvs_close(nvs);
            return ESP_OK;
        }
    }

    // New network: shift all entries up by 1, write at slot 0
    int slots = MIN(count, MAX_KNOWN_NETWORKS - 1);
    for (int i = slots; i > 0; i--) {
        char src[16], dst[16]; char buf[64]; size_t l = sizeof(buf);
        snprintf(src, sizeof(src), "s_%d", i-1);
        snprintf(dst, sizeof(dst), "s_%d", i);
        if (nvs_get_str(nvs, src, buf, &l) == ESP_OK)
            nvs_set_str(nvs, dst, buf);
        l = sizeof(buf);
        snprintf(src, sizeof(src), "p_%d", i-1);
        snprintf(dst, sizeof(dst), "p_%d", i);
        if (nvs_get_str(nvs, src, buf, &l) == ESP_OK)
            nvs_set_str(nvs, dst, buf);
    }
    nvs_set_str(nvs, "s_0", ssid);
    nvs_set_str(nvs, "p_0", password ? password : "");
    if (count < MAX_KNOWN_NETWORKS)
        nvs_set_i32(nvs, NVS_KEY_COUNT, count + 1);

    nvs_commit(nvs); nvs_close(nvs);
    return ESP_OK;
}
```

---

### 🟡 [T5-5] VGA Enrollment Framesize Is Never Applied

**File:** `camera_driver.c:304–306`

**Problem:** `camera_set_framesize()` is an empty stub on the ESP32-P4 path. All 20 enrollment frames are captured at 320×240 (QVGA) despite `CAMERA_ENROLL_FRAME_SIZE = FRAMESIZE_VGA` in `config.h`.

**Fix:**
```c
// camera_driver.c (ESP32-P4 path):
void camera_set_framesize(framesize_t framesize) {
    if (!s_cam_dev || !s_cam_handle) return;
    uint16_t w = (framesize == FRAMESIZE_VGA) ? 640 : 320;
    uint16_t h = (framesize == FRAMESIZE_VGA) ? 480 : 240;

    esp_cam_ctlr_stop(s_cam_handle);

    esp_cam_sensor_format_t fmt = { .width = w, .height = h };
    esp_cam_sensor_ioctl(s_cam_dev, ESP_CAM_SENSOR_IOC_S_FMT, &fmt);

    // Update frame buffer sizes
    size_t new_size = w * h * 2;  // RGB565
    heap_caps_free(s_frame_buffer_1);
    heap_caps_free(s_frame_buffer_2);
    s_frame_buffer_1 = heap_caps_malloc(new_size, MALLOC_CAP_SPIRAM);
    s_frame_buffer_2 = heap_caps_malloc(new_size, MALLOC_CAP_SPIRAM);

    esp_cam_ctlr_start(s_cam_handle);
    ESP_LOGI(TAG, "Framesize changed to %dx%d", w, h);
}
```

Call in `process_enrollment_frames()`:
```c
camera_set_framesize(CAMERA_ENROLL_FRAME_SIZE);
// ... 20-frame loop ...
camera_set_framesize(CAMERA_FRAME_SIZE);  // restore after done
```

---

## Production Build Checklist

| Item | State | Action |
|---|---|---|
| Setup wizard triggers on first boot | ❌ | T4-3 |
| Admin PIN unique per unit | ❌ | T1-2 |
| BLE PIN dynamic per session | ❌ | T3-2 |
| Telegram credentials configurable | ❌ | T1-5 |
| Wi-Fi feedback is accurate | ❌ | T4-1 |
| Sync retry counter resets | ❌ | T1-6 |
| SQLite mutex serialisation | ❌ | T2-4 |
| Enrollment cancel is safe | ❌ | T1-1 |
| CSV uploaded via `sendDocument` | ❌ | T5-1 |
| Schedule import INSERTs to DB | ❌ | T5-2 |
| Clock epoch guard | ❌ | T5-3 |
| NVS encryption active | ❌ | T3-4 |
| Factory reset safe shutdown | ❌ | T1-7 |
| Sync task guarded against duplication | ❌ | T2-2 |
| Enrollment framesize applied | ❌ | T5-5 |
| Log level `ESP_LOG_WARN` | ✅ | — |
| Task stack sizes updated | ✅ | — |
| WAV RIFF chunk parser | ✅ | — |
| `esp_hosted` dependency declared | ✅ | — |
| LVGL tick driver + mutex | ✅ | — |
| Duplicate attendance guard | ✅ | — |
| Recognizer cache load from DB | ✅ | — |
| GPIO 9 conflict resolved | ✅ | — |

---

## Sprint Execution Plan

```
Sprint A — Before touching hardware
  T1-1  Enrollment cancel flag
  T1-2  Wizard PIN save to NVS
  T1-3  Wizard admin enroll wiring
  T1-4  Wizard Wi-Fi credential save
  T1-5  Telegram NVS credentials + settings UI
  T1-6  Wi-Fi retry counter reset
  T1-7  Factory reset task suspension

Sprint B — Before first deployment
  T2-1  Blocking scan → worker task
  T2-2  Sync task handle guard
  T2-3  Deep sleep state gating + light sleep
  T2-4  SQLite mutex
  T2-5  Camera queue depth = 1
  T2-6  Audio via queue / computed duration

Sprint C — Before institutional deployment
  T3-1  "Change PIN" Settings UI
  T3-2  BLE random per-session PIN
  T3-3  Enroll always requires re-auth
  T3-4  NVS encryption partition

Sprint D — Before user acceptance testing
  T4-1  Async Wi-Fi connection feedback
  T4-2  Enrollment retry button
  T4-3  Setup wizard at boot
  T4-4  Sync interval dropdown wired
  T4-5  Brightness init from NVS

Sprint E — Before shipping
  T5-1  CSV sendDocument to Telegram
  T5-2  Schedule CSV import DB INSERT
  T5-3  Epoch time guard in clock
  T5-4  Wi-Fi LRU network rotation
  T5-5  Light sleep instead of deep sleep
  T5-6  VGA enrollment framesize
```

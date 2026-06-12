# SmartAttendance — Revised Consumer Readiness Audit

> **Design Philosophy (Confirmed):**
> - **Recognition is 100% offline** — Edge AI on ESP32-P4 using ESP-DL. No cloud dependency for face scanning.
> - **Target performance:** Face scan < 3 seconds, enrollment 15–20 seconds.
> - **Connectivity is brief and admin-triggered** — Device goes online only to sync schedule and export CSV, then returns offline.
> - **Dual Telegram Bot Architecture:**
>   - **Cloud Bot** → pushes lecture notifications to students
>   - **Admin notification** → alerts admin to briefly connect the device for data sync
> - Fingerprint/cloud-biometric alternatives are rejected: hygiene and internet-dependency concerns.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────┐
│               Cloud                          │
│  ┌─────────────┐    ┌─────────────────────┐ │
│  │ Telegram Bot│    │ Sync Server (thin)  │ │
│  │ (notify     │───▶│ - Schedule upload   │ │
│  │  students)  │    │ - CSV pull          │ │
│  └─────────────┘    └──────────┬──────────┘ │
└─────────────────────────────────┼───────────┘
                      Brief admin-triggered
                      HTTPS sync only
                                  │
┌─────────────────────────────────▼───────────┐
│              ESP32-P4 Device                 │
│                                              │
│  Camera ──▶ Face Detect ──▶ Face Recognize  │
│  (OV5647)    (ESP-DL)        (MobileFaceNet) │
│                 │                 │          │
│                 ▼                 ▼          │
│             LVGL UI ◀──── Attendance DB      │
│             (7" touch)     (SQLite / SD)     │
│                                              │
│  BLE Enrollment ──▶ New User Registration   │
└─────────────────────────────────────────────┘
```

---

## Section 1 — Critical Bugs (Core Loop is Broken)

These must be fixed regardless of architecture. The recognition pipeline does not function at all.

### 🔴 [1.1] `recognizer_load_cache()` is Empty — No One is Ever Recognized

```cpp
// recognizer.cpp:50-56
void recognizer_load_cache(void) {
    s_cache_size = 0;  // always empty, db never queried
}
```
**Impact:** The device boots with zero enrolled users. No recognition will ever occur.  
**Fix:** Call `db_get_all_users()` and populate `s_user_cache[]` from the result.

```c
// Proposed implementation
void recognizer_load_cache(void) {
    user_t *users = NULL;
    int count = 0;
    if (db_get_all_users(&users, &count) == ESP_OK && users && count > 0) {
        int to_load = count < s_cache_capacity ? count : s_cache_capacity;
        memcpy(s_user_cache, users, to_load * sizeof(user_t));
        s_cache_size = to_load;
        free(users);
        ESP_LOGI(TAG, "Loaded %d users into recognition cache", s_cache_size);
    }
}
```

---

### 🔴 [1.2] Feature Extractor — Pixel Format Mismatch

```cpp
// feature_extractor.cpp:40
input_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_GRAY;  // wrong
// camera outputs RGB565, MobileFaceNet expects RGB888 or RGB565
```
**Impact:** All computed face embeddings are garbage. Recognition is cryptographically impossible regardless of enrollment.  
**Fix:** Trace the buffer through `camera_capture_frame()` → `face_alignment_align()` → `feature_extractor_run()` and confirm which pixel format lands in the extractor. Set `DL_IMAGE_PIX_TYPE_RGB565` to match.

---

### 🔴 [1.3] `face_detector_compute_sharpness()` / `compute_brightness()` Return Constants

```cpp
float face_detector_compute_sharpness(...) { return 100.0f; }  // stub
float face_detector_compute_brightness(...) { return 128.0f; } // stub
```
**Impact:** Enrollment quality scoring is completely inert. The device cannot reject blurry or poorly-lit enrollment frames.  
**Fix (sharpness — Laplacian variance):**
```c
float face_detector_compute_sharpness(camera_fb_t *fb, detected_face_t *face) {
    // Crop face ROI, compute Laplacian variance over grayscale pixels
    float mean = 0, variance = 0;
    int n = face->w * face->h;
    uint8_t *pixels = ...; // extract ROI from fb
    for (int i = 0; i < n; i++) mean += pixels[i];
    mean /= n;
    for (int i = 0; i < n; i++) variance += (pixels[i] - mean) * (pixels[i] - mean);
    return variance / n;  // higher = sharper
}
```

---

### 🔴 [1.4] Enrollment Ignores BLE Data — Hardcoded "New User"

```c
// main.c:566-568
strcpy(new_user.name, "New User");  // BLE data is collected but never read
```
**Fix:**
```c
enrollment_data_t ble_data;
if (ble_registration_get_pending_data(&ble_data)) {
    strncpy(new_user.name, ble_data.name, sizeof(new_user.name) - 1);
    strncpy(new_user.student_id, ble_data.student_id, sizeof(new_user.student_id) - 1);
    strncpy(new_user.role, ble_data.role, sizeof(new_user.role) - 1);
} else {
    // Fallback: show on-screen name entry form
    ui_show_enrollment_name_entry(&new_user);
}
```

---

### 🔴 [1.5] No Duplicate Attendance Guard

The same person recognized twice in the same session logs two records.  
**Fix:** Add to `db_insert_attendance_log()`:
```sql
INSERT INTO attendance ... 
WHERE NOT EXISTS (
    SELECT 1 FROM attendance 
    WHERE user_id=? AND schedule_id=? 
    AND date(timestamp,'unixepoch')=date('now')
)
```

---

### 🔴 [1.6] Detection Task Runs During Enrollment

`detection_recognition_task` continues consuming camera frames while enrollment is in progress, causing race conditions on the frame queue and spurious attendance logs.  
**Fix:** Add state check at the top:
```c
if (get_system_state() != SYSTEM_STATE_NORMAL) {
    camera_return_frame(frame.fb);
    continue;
}
```

---

### 🔴 [1.7] LVGL Has No Tick Driver

`lv_init()` is called, but there is no `lv_tick_inc()` source and no `lv_task_handler()` in the main loop. LVGL animations and timers will not advance.  
**Fix:**
```c
// In app_main, before task creation:
esp_timer_handle_t lv_tick_timer;
esp_timer_create_args_t timer_args = { .callback = [](void*){ lv_tick_inc(1); } };
esp_timer_create(&timer_args, &lv_tick_timer);
esp_timer_start_periodic(lv_tick_timer, 1000);  // 1 ms

// In main loop, replace system_state_machine() with:
while (1) {
    lv_task_handler();
    system_state_machine();
    vTaskDelay(pdMS_TO_TICKS(5));
}
```

---

## Section 2 — Revised Cloud Architecture

> **The original audit flagged cloud sync stubs as critical bugs. This is re-categorized as a design implementation task, not a bug fix.**

### Current State (Stubs to Replace)

The existing `cloud_sync.c` implements a traditional always-online REST API model which was correctly identified as inappropriate for this use case.

### Proposed: Telegram-Triggered Sync Architecture

```
Admin Telegram message: "sync"
        │
        ▼
Telegram Cloud Bot (Webhook)
        │
        ├──▶ Sends schedule JSON to device when device connects
        │
        └──▶ Receives attendance CSV from device
                  │
                  ▼
           Forwards to admin
```

#### Implementation Plan

**Phase A — Schedule Push (Cloud → Device)**
```c
// Replace sync_schedule() with:
esp_err_t sync_schedule_from_telegram(void) {
    // GET /api/schedule?device_id=<mac>
    // Server has already received schedule from Telegram bot command
    // Parse JSON, store into 'schedule' table
}
```

**Phase B — CSV Attendance Export (Device → Telegram)**
```c
esp_err_t export_attendance_csv_to_telegram(void) {
    // 1. Query all unsynced attendance records from SQLite
    // 2. Build CSV string in memory
    // 3. POST multipart/form-data to Telegram Bot API
    //    sendDocument endpoint with the CSV file
    // 4. Mark records as synced
}
```
This replaces the need to build a full REST backend. Telegram's Bot API is the server.

**Phase C — SNTP Time Sync (Required for accurate timestamps)**
```c
// Must be initialized after Wi-Fi connects
esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0, "pool.ntp.org");
esp_sntp_init();
```

---

## Section 3 — Security Issues

### 🔴 [3.1] BLE Enrollment Has No Authentication

Any phone within range can write name/student_id/department and trigger enrollment.  
**Fix:** Implement 4-digit PIN displayed on screen that must be written to a `CHAR_PIN_UUID` characteristic before other writes are accepted. Store PIN in NVS.

---

### 🔴 [3.2] HTTP Response Buffer Off-by-One (Heap Overflow)

```c
// cloud_sync.c:217 — writes one byte past allocated capacity
resp_ctx.buffer[resp_ctx.len] = '\0';
```
**Fix:** Change `malloc(ctx->capacity)` to `malloc(ctx->capacity + 1)`.

---

### 🟠 [3.3] `nvs_keys` Partition Missing

The code path for `CONFIG_NVS_ENCRYPTION` references a `"nvs_keys"` partition that does not exist in `partitions.csv`, causing silent fallback to unencrypted NVS.  
**Fix:** Either add the partition or remove the dead `#ifdef CONFIG_NVS_ENCRYPTION` block until encryption is properly implemented.

---

### 🟠 [3.4] LVGL UI Modified from Non-UI Task (Thread Safety)

```c
// detection_recognition_task calls:
ui_show_recognition_result(matched_user->name, confidence);
ui_update_detection_bounding_box(...);
```
LVGL is single-threaded. These must be dispatched to the UI thread.  
**Fix:** Post results as events or use a shared result struct with a mutex, applied only inside `lv_task_handler()` context.

---

## Section 4 — UX Gaps

### 🔴 [4.1] No First-Time Setup Flow

Device boots directly into scan mode with no users and no Wi-Fi.  
**Fix:** On first boot (detected via NVS flag), show setup wizard:
1. Language selection (future)
2. Admin face enrollment (mandatory first user)
3. Wi-Fi configuration
4. Time zone setting

---

### 🔴 [4.2] Wi-Fi Scan Not Implemented

```cpp
ui_show_notification(NOTIFY_INFO, "Wi-Fi", "Scan feature not yet implemented", 2000);
```
**Fix:** `wifi_manager_scan()` already exists. Show modal list. On item tap, fill SSID field.

---

### 🔴 [4.3] No Admin Access Control

Anyone can open Settings and change Wi-Fi, or tap Enroll.  
**Fix:** Require admin face verification before navigating to Settings or Enroll screens.

---

### 🟠 [4.4] Brightness Not Persisted

`ui_get_brightness()` hardcodes `80`.  
**Fix:** Read from NVS key `"brightness"` at init, default to 80 if absent.

---

### 🟠 [4.5] Clock Displays "Jan 1, 1970" Without SNTP

**Fix:** Show "Time not set" in status bar until SNTP sync completes. Store last-known time in NVS as power-cycle fallback.

---

### 🟡 [4.6] No Visual Feedback When SD Card is Missing

**Fix:** Check SD card presence at boot and after every failed DB query. Display persistent banner and block attendance recording with user notification.

---

### 🟡 [4.7] BLE Enrollment Status Notifications Not Sent

The status value is updated in RAM but `ble_gap_notify()` is never called.  
**Fix:** Store connection handle in GAP event handler. Call `ble_gattc_indicate()` or `ble_gattc_notify_custom()` when status changes.

---

## Section 5 — Stack Sizes and Reliability

| Task | Current Stack | Recommended | Reason |
|---|---|---|---|
| `camera_task` | 4 096 B | 8 192 B | Camera DMA callbacks |
| `detection_recognition_task` | 8 192 B | 16 384 B | C++ STL vector heap + ESP-DL output |
| `audio_task` | 2 048 B | 8 192 B | `fread` + I2S write will overflow |
| `battery_task` | 1 024 B | 4 096 B | ADC calibration API |
| `db_task` | 4 096 B | 8 192 B | SQLite statement prep uses heap |

**Fix:** Increase all per the table. Profile with `uxTaskGetStackHighWaterMark()`.

---

### 🟠 Audio `blocking` Mode Uses Fixed 500 ms Sleep

**Fix:** Compute duration from WAV file header: `duration_ms = (file_size - 44) / (sample_rate * channels * bit_depth / 8) * 1000`.

---

### 🟠 WAV Header Parsing Uses Fixed Offset 44

**Fix:** Parse RIFF/fmt/data chunks properly:
```c
// Find 'data' sub-chunk instead of assuming offset 44
while (fread(&chunk_id, 4, 1, f) == 1) {
    fread(&chunk_size, 4, 1, f);
    if (memcmp(&chunk_id, "data", 4) == 0) break;
    fseek(f, chunk_size, SEEK_CUR);
}
```

---

## Section 6 — Hardware Issues

### 🔴 GPIO Conflicts

| GPIO | Conflicting uses | Impact |
|---|---|---|
| 9 | `LCD_PCLK_GPIO` AND `CAM_PIN_PWDN` | Display pixel clock drives camera power-down |
| 12 | `LCD_DATA2_GPIO` AND `CAM_PIN_SCCB_SDA` | Display data corrupts camera I2C |
| 13 | `LCD_DATA3_GPIO` AND `CAM_PIN_SCCB_SCL` | Display data corrupts camera I2C clock |
| 41 | `LCD_RESET_GPIO` AND `LCD_DATA12_GPIO` | Reset line doubles as pixel data |

**Fix:** Pull the actual CrowPanel Advanced 7" ESP32-P4 schematic from Elecrow and verify every pin assignment against it before any hardware testing.

---

### 🟠 Voltage Divider Factor Assumed to Be 2

```c
voltage_mv = voltage_mv * 2;  // "Assuming typical" — must be verified
```
**Fix:** Measure the actual resistor values on the PCB or from the schematic. Define as `BATTERY_VOLTAGE_DIVIDER_RATIO`.

---

### 🟠 `esp_hosted_fg` Not Listed as a Dependency

Wi-Fi and BLE on ESP32-P4 require `esp_hosted_fg` for the C6 co-processor, but it is absent from `idf_component.yml`.  
**Fix:** Add `espressif/esp_hosted: ">=0.0.8"` to `idf_component.yml` and document the C6 firmware flash procedure.

---

## Section 7 — Missing Consumer Features

| Feature | Priority | Notes |
|---|---|---|
| On-device CSV export via Telegram | 🔴 Core | Replaces always-online sync |
| Schedule import via Telegram trigger | 🔴 Core | Replaces always-online sync |
| Admin face-unlock for sensitive screens | 🔴 Core | Security |
| User management (delete/edit) | 🟠 High | Cannot remove former students |
| Late vs. present threshold (e.g. +5 min) | 🟠 High | Core academic use case |
| Factory reset option | 🟠 High | For device redeployment |
| OTA trigger from Telegram | 🟡 Medium | Field updates |
| Volume control (software gain on I2S) | 🟡 Medium | Accessibility |
| Power-save: camera/display off when idle | 🟡 Medium | Battery life |

---

## Section 8 — Production Build Checklist

| Setting | Current | Production Target |
|---|---|---|
| Optimization | `-Og` (debug) | `-Os` (size) |
| Log level | `ESP_LOG_INFO` | `ESP_LOG_WARN` |
| Task WDT | Unconfirmed | Enable for all tasks |
| Flash encryption | Off | Enable for shipped units |
| Secure boot | Off | Enable for shipped units |
| Heap corruption | Off | Enable `light` mode for testing |

---

## Recommended Implementation Order

```
Sprint 1 — Make Recognition Work (Local, Offline)
  1. Fix recognizer_load_cache() [1.1]
  2. Fix pixel format in feature_extractor [1.2]
  3. Fix sharpness/brightness compute [1.3]
  4. Fix BLE → enrollment user data flow [1.4]
  5. Add LVGL tick driver [1.7]
  6. Fix detection task state guard [1.6]

Sprint 2 — Make the Device Safe to Use
  7. BLE PIN authentication [3.1]
  8. Fix heap off-by-one [3.2]
  9. Resolve GPIO conflicts with schematic [Hardware 6]
  10. Admin face-unlock for Enroll/Settings [4.3]
  11. Fix LVGL thread safety [3.4]

Sprint 3 — Telegram Sync Architecture
  12. SNTP initialization after Wi-Fi [2]
  13. Schedule JSON import from bot endpoint
  14. CSV export → Telegram Bot API sendDocument
  15. Admin trigger flow (Telegram → brief connect → sync → offline)

Sprint 4 — UX Polish
  16. First-time setup wizard [4.1]
  17. Wi-Fi scan modal [4.2]
  18. Brightness NVS persistence [4.4]
  19. Duplicate attendance guard [1.5]
  20. User management (delete/edit)
  21. SD card missing banner [4.6]

Sprint 5 — Hardening & Production
  22. Increase stack sizes [5]
  23. WAV header parser [5]
  24. Voltage divider verification [6]
  25. esp_hosted_fg dependency [6]
  26. Production build flags [8]
```

---

## Summary

The **recognition engine and enrollment pipeline are completely non-functional** due to stub code — this is the highest priority fix regardless of the architecture. The cloud sync strategy has been correctly redesigned around the **dual Telegram bot model**, which is more resilient, cheaper, and appropriate for the target environment. The device should function fully offline, connect briefly only when the admin initiates a sync, and deliver attendance records as CSV via Telegram. This is a sound architecture that simplifies the backend considerably.

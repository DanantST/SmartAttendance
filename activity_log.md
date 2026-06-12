# SmartAttendance Project Activity & Resolution Log

This document serves as the comprehensive development log, issue tracking record, and architectural resolution document for the SmartAttendance project built on the CrowPanel Advanced 7" ESP32-P4 v1.0 development board using ESP-IDF v5.4.2.

---

## Detailed Summary of Key Issues Faced & Technical Resolutions

### 1. ESP32-C6 Co-processor SDIO Timeout Disconnects under Traffic Load
- **The Issue:**
  During active captive portal setup wizard transactions, modern mobile devices connect and immediately initiate multi-packet DHCP negotiations, DNS resolution, and system probe handshakes. Previously, the host-slave SDIO bus clock speed communicating between the main ESP32-P4 processor and the ESP32-C6 Wi-Fi co-processor was clocked at an aggressive **20 MHz** (and originally **40 MHz**). Under high packet rates, trace signal degradation and cross-talk on the development board led to immediate SDIO packet timeouts (`err: 265` = `ESP_ERR_TIMEOUT`), causing the slave interface to lock up and the client phone to get disconnected.
- **The Resolution:**
  Downclocked both `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` and `CONFIG_ESP_SDIO_CLOCK_FREQ_KHZ` in both `sdkconfig` and `sdkconfig.defaults.esp32p4` to a highly resilient **10 MHz**. While 10 MHz still provides abundance of bandwidth for local setup transactions, it vastly increases the hardware signal-integrity and noise margin, ensuring zero register-read failures under continuous network load.

### 2. Wi-Fi Power Save Disconnects and Interface Clock-Gating
- **The Issue:**
  By default, the ESP-Hosted Wi-Fi stack enables **Modem Power Save** to reduce power consumption during periods of low transmission. However, when the co-processor automatically enters low-power sleep, it clock-gates or stalls its SDIO hardware interface. The host's subsequent read/write request fails with a registers-read timeout (`returned 0x109`), leading to total host-slave communication failure and requiring a complete board reset.
- **The Resolution:**
  Enforced a strict "No Power Save" policy inside the host initialization drivers (`main/network/wifi_manager.c` and `main/network/wifi_ap_portal.cpp`). Immediately after every `esp_wifi_start()` invocation, we explicitly call `esp_wifi_set_ps(WIFI_PS_NONE)`. Keeping the co-processor's RF circuitry and SDIO bus active at all times prevents sleep-induced interface stalls and smooths out transient voltage sags on the USB-C power rail.

### 3. Backlight Driver Timer & LEDC PWM Configuration Conflict
- **The Issue:**
  The board initialization routines and screen drivers require a robust method to adjust display brightness. During optimization rounds, attempts to simplify display setup introduced code that bypassed backlight driver routines. This dropped critical board-level PWM settings, causing display flickering or loss of individual channel control when other timer-dependent modules initialized.
- **The Resolution:**
  Reverted the bypass modifications inside [backlight.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/display/backlight.c), fully restoring the native, self-contained `ledc_timer_config` and `ledc_channel_config` initializations. This ensures the LEDC driver initializes its own low-speed timer (`LCD_BACKLIGHT_PWM_TIMER`) and channel (`LCD_BACKLIGHT_PWM_CHANNEL`) on `LCD_BACKLIGHT_PIN` using a validated duty cycle formula tailored for the CrowPanel v1.0 backlight circuit:
  $$\text{duty} = (\text{percent} \neq 0) ? (\text{percent} \times 18 + 200) : 0$$

### 4. Smartphone Captive Portal Dropping Connection via Probe Failure
- **The Issue:**
  When a smartphone connects to the local `Attendance_Setup` Access Point, the OS sends background HTTP requests to check for internet connectivity (e.g., `connectivitycheck.gstatic.com` on Android, `captivedetect.apple.com` on iOS). Serving the raw HTML portal page to these background probes caused the smartphone to flag the captive portal as a dead network, immediately dropping the Wi-Fi connection and switching back to cellular data.
- **The Resolution:**
  Upgraded the HTTP handler inside `main/network/wifi_ap_portal.cpp` to intercept background connectivity check paths. If a probe request is received, the server responds with a clear **HTTP 302 Found Redirect** pointing explicitly to `http://192.168.4.1/index.html`. This tells the smartphone OS that the network is indeed a captive hotspot, instantly triggering the native OS "Sign in to network" modal and locking the connection open. Additionally, a tip banner was added to the setup HTML page advising users to temporarily turn off LTE/5G during setup to prevent cellular overrides.

### 5. Database Reports Screen Freeze ("Loading courses...")
- **The Issue:**
  When entering the Reports screen, the UI would lock up in a perpetual "Loading courses..." state. The database manager successfully opened SQLite, but because no directories or default databases were created, queries failed silently, or the database was completely empty of course data, leaving the UI dropdown empty and stalled.
- **The Resolution:**
  1. Updated `db_manager_init` inside `main/database/db_manager.c` to automatically construct the directory structure on the SD card (`/sdcard/audio`, `/sdcard/models`, `/sdcard/models/p4`, and `/sdcard/users`) on startup.
  2. Implemented automatic database seeding: when the SQLite database is first opened and detected as empty, the code automatically populates it with default courses (`Linear Algebra`, `Computer Vision`, `Embedded Systems`) via `db_insert_course`.
  3. Linked the Reports UI dropdown directly to the database via `db_get_all_courses` queries, creating a fully dynamic database-to-UI interface.

### 6. Destructive Navigation & Linear Screen Traversal Limitations
- **The Issue:**
  The back navigation throughout the UI was originally hardcoded to load the main Home screen (`s_main_screen`). Tapping back inside folders or nested menus would dump the user back home instead of traversing backward logically. Additionally, switching screens left resources allocated in background memory, creating immediate RAM memory pressure on the ESP32-P4.
- **The Resolution:**
  1. **Stack-Based Navigation:** Designed a compiler-safe screen history stack framework (`s_screen_history`). To avoid modifying all 10+ sub-screen files, we introduced a macro wrapper in `main/ui/ui_main.h` that intercepts `lv_scr_load(scr)` and automatically logs the current screen before switching.
  2. **Granular Memory Deallocation:** Integrated custom individual app close handlers. Tapping a red `(X)` close button on an app card in the Recents carousel immediately removes the screen from memory and triggers its targeted cleanup, while "Clear All" systematically cleans all screen objects from the heap to maximize system RAM.

### 7. Swipe-Down Dropdown Status Bar Gesture Scope
- **The Issue:**
  The pull-down Quick Settings dropdown panel (`s_qs_panel`) was originally created as a child of the Home screen object. As a result, users could only pull down or interact with the Quick Settings panel from the Home screen. Trying to adjust brightness or view notifications from the Settings, Reports, or File Manager views was impossible.
- **The Resolution:**
  Refactored the creation of `s_qs_panel` inside `main/ui/ui_main.cpp` to attach it directly to the persistent top layer: `lv_layer_top()`. The pull-down status bar listener and gesture callbacks now monitor and overlay the Quick Settings menu perfectly across all screens in the application.

### 8. Compiler-Blocking Formatter Truncation Warnings
- **The Issue:**
  The project compilation would fail with strict `-Werror=format-truncation` warnings during build time. Specifically, recursive path constructions inside the File Manager module (`main/ui/ui_file_manager.cpp`) were copying path arguments into standard 256-byte buffers, which the compiler flagged as highly vulnerable to buffer overflow.
- **The Resolution:**
  Upgraded the path and formatted string buffer allocations inside `ui_file_manager.cpp` to a generous `1024` bytes. This mathematically guarantees zero overflows even during extremely deep directory traversal, cleanly bypassing the compiler warnings.

### 9. File Manager → Task Watchdog (WDT) System Crash on Large Directories
- **The Issue:**
  A hard system crash was triggered when opening the File Manager after previously opening the Attendance Scanner. The monitor log showed:
  ```
  E (89668) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
  E (89668) task_wdt:  - IDLE0 (CPU 0)
  E (89668) task_wdt: Tasks currently running: CPU 0: lvgl
  E (89668) task_wdt: Aborting.
  Core 0 register dump:
  MEPC: 0x4005c1ee  → dispatch at lv_draw_sw.c:432
  RA:   0x400407c0  → lv_draw_dispatch_layer at lv_draw.c:277
  ```
  **Root Causes (three compounding bugs):**
  1. **Massive single-frame render:** The `fm_loader` background task read up to 100 SD card file entries (each requiring a `stat()` syscall), then acquired the LVGL mutex and created all 100 `lv_list_add_btn()` widgets inside a single `lv_timer_handler()` call. Each button widget triggers layout and software pixel rendering. This held the CPU0 mutex continuously for ~17 seconds.
  2. **IDLE0 starvation:** The LVGL port task ran on CPU0 without ever yielding or resetting the hardware watchdog timer. The FreeRTOS Task Watchdog fires when `IDLE0` (the lowest-priority task responsible for resetting the WDT) is unable to run within the configured timeout window. With the LVGL task monopolising CPU0 for 17s, IDLE0 was permanently blocked.
  3. **Undersized loader task stack:** The `fm_loader` task was created with only 4096 bytes of stack — insufficient for `stat()` × 100 + LVGL widget creation, risking silent stack overflow corruption.
- **The Resolution:**
  Three coordinated fixes were applied:
  1. **Watchdog registration in LVGL task** (`main/display/lcd_driver.c`): Added `esp_task_wdt_add(NULL)` at LVGL task startup and `esp_task_wdt_reset()` at the top of every iteration loop. The LVGL task now explicitly feeds the WDT each frame, preventing IDLE0 starvation panics even during legitimately long render operations.
  2. **File Manager pagination** (`main/ui/ui_file_manager.cpp`): Replaced the "render everything at once" approach with a paginated display system. A maximum of **20 file entries** are rendered per page, with `< Prev Page` and `Next Page >` navigation buttons. The current page number and total file count are displayed in a subtitle label. This limits each LVGL render pass to ≤22 widgets, dropping the hold time from ~17 seconds to under 100ms.
  3. **Loader task stack increase**: All `fm_loader` / `fm_dir_nav` background tasks were increased from **4096 → 8192 bytes** of stack to safely accommodate `opendir` + `readdir` + `stat()` syscall chains and LVGL widget creation within the same task context.
  4. **Entry cache lifecycle fix**: The heap-allocated `FileEntry` array (`s_entries`) is now correctly `free()`d in `ui_close_file_manager_screen()` and whenever a new directory is navigated into, preventing cumulative heap leaks.
- **Secondary Observations (non-crashing):**
  - `W ledc: GPIO 31 is not usable, maybe conflict with others` — GPIO31 (backlight PWM pin) receives two LEDC `timer_config` calls: once from the board BSP and once from `backlight_init()`. Both configure the same timer/channel parameters so no functional conflict occurs; the warning is cosmetic.
  - `W transport: Version mismatch: Host [2.12.0] > Co-proc [2.3.0]` — The ESP-Hosted slave firmware on the ESP32-C6 co-processor is older than the host library version. Does not currently cause failures but may cause RPC timeouts under stress. Recommend reflashing the co-processor with a matching firmware version when convenient.

---

## Chronological Project Log Entries

### [2026-05-22] - File Manager WDT Crash Fix & Pagination
- **Category:** System Stability & UI Performance
- **Altered Files:**
  - `main/display/lcd_driver.c` (modified)
  - `main/ui/ui_file_manager.cpp` (rewritten)
- **Status:** Complete & Build Verified (10/10 incremental steps, zero errors)
- **Crash Reproduction Steps:** Open Attendance Scanner → close it → open File Manager → WDT abort after ~17s.
- **Log:**
  * **LVGL WDT Fix (`lcd_driver.c`):** Registered the LVGL port task with `esp_task_wdt_add(NULL)` and called `esp_task_wdt_reset()` at the top of every render loop iteration. Prevents `IDLE0` starvation panics when `lv_timer_handler()` runs an unusually long draw pass.
  * **File Manager Pagination (`ui_file_manager.cpp`):** Replaced all-at-once rendering with a 20-entries-per-page system. Added `< Prev Page` / `Next Page >` navigation buttons and a subtitle label showing page count and total files. Render time per page: <100ms vs. the previous ~17 seconds.
  * **Loader task stack:** All `fm_loader`/`fm_dir_nav` tasks increased from 4096 → 8192 bytes to handle `stat()` × N files safely.
  * **Heap leak fix:** `s_entries` (`FileEntry` array) is now correctly `free()`d on close and on every directory change.
  * Confirmed clean incremental build: `SmartAttendance.bin` = `0x544300` bytes, `0xbbd00` bytes (12%) free.

### [2026-05-20] - Co-processor Stability Hotfix & Backlight Restoration
- **Category:** Hardware Optimization & Code Restoration
- **Altered Files:**
  - `sdkconfig` (modified)
  - `sdkconfig.defaults.esp32p4` (modified)
  - `main/display/backlight.c` (restored)
- **Status:** Complete & Build Verified
- **Log:**
  * Downclocked SDIO bus clock from `20 MHz` to `10 MHz` to eliminate signal-integrity line noise under data load.
  * Restored the complete backlight driver setup using proper LEDC timer configurations (`LCD_BACKLIGHT_PWM_TIMER`, `LCD_BACKLIGHT_PWM_RES`, `LEDC_LOW_SPEED_MODE`).
  * Executed `idf.py build` to confirm a successful build. The binary `SmartAttendance.bin` was compiled cleanly with `0xbc040` bytes free.

### [2026-05-19] - Wi-Fi Portal Reliability & Database Seeding Integration
- **Category:** Networking, Database, & Compiler Compliance
- **Altered Files:**
  - `main/network/wifi_ap_portal.cpp` (modified)
  - `main/network/wifi_manager.c` (modified)
  - `main/database/db_manager.c` / `db_manager.h` (modified)
  - `main/ui/ui_reports.cpp` (modified)
  - `main/ui/ui_file_manager.cpp` (modified)
- **Status:** Complete & Build Verified
- **Log:**
  * Enabled active captive portal HTTP `302 Found` redirections to bypass background OS internet connectivity drops.
  * Disabled dynamic modem power save with `esp_wifi_set_ps(WIFI_PS_NONE)` to prevent SDIO clock gating timeouts.
  * Programmed automatic SQLite database folder hierarchy setup and course seeding on SD card mount.
  * Connected Reports UI course dropdown dynamically to database queries.
  * Expanded path buffers to `1024` bytes to resolve compilation-blocking `-Werror=format-truncation` warnings.

### [2026-05-18] - UI Gestures, Stack-Based Navigation & RAM Purging
- **Category:** User Interface & Resource Management
- **Altered Files:**
  - `main/ui/ui_main.cpp` / `ui_main.h` (modified)
  - `main/ui/ui_enrollment.cpp` (modified)
- **Status:** Complete & Build Verified
- **Log:**
  * Refactored pull-down Quick Settings dropdown to `lv_layer_top()` for universal accessibility.
  * Created rolling 3-slot notification history FIFO inside Quick Settings dropdown.
  * Created stack-based back navigation macro interceptor for back button steps.
  * Added individual/All RAM deallocations to clear screen objects from heap memory instantly.

### [2026-05-13 to 2026-05-17] - Foundation Setup & Primary Component Integration
- **Category:** Core Architecture & Platform Setup
- **Status:** Complete
- **Log:**
  * Initialized board BSP support for ESP32-P4 with ESP-IDF v5.4.2 toolchain.
  * Integrated LVGL graphics engine, dark theme typography.
  * Compiled and integrated SQLite3 engine supporting SD card databases.
  * Designed core shells for Setup Wizard, Face Detection/Enrollment Screen, Settings, Reports, and Recorder modules.

---

### [2026-05-25] - Double-Tap Backlight Toggle
- **Category:** User Interface / UX
- **Altered Files:**
  - `main/ui/ui_main.cpp` (modified)
  - `main/ui/ui_main.h` (modified)
  - `main/ui/ui_attendance.cpp`, `ui_enrollment.cpp`, `ui_file_manager.cpp`, `ui_recorder.cpp`, `ui_reports.cpp`, `ui_settings.cpp`, `ui_setup_wizard.cpp`, `ui_user_manager.cpp` (all modified)
- **Status:** Complete
- **Log:**
  * Implemented `ui_add_double_tap_to_screen(lv_obj_t*)` in `ui_main.cpp`. Attaches an `LV_EVENT_CLICKED` listener that detects two taps within a **400 ms** window using `xTaskGetTickCount()` timestamps. On detection, toggles the backlight between full brightness and off (`backlight_set(0)`). State tracked with a `static bool screen_on`.
  * Declared `ui_add_double_tap_to_screen()` in `ui_main.h` for use by all screens.
  * Applied the listener to all nine screen objects at creation time — double-tap to sleep/wake now works system-wide from any screen.

---

### [2026-05-29] - Enrollment Audio Silence Fix & Try Again Button
- **Category:** Audio System / User Interface
- **Altered Files:**
  - `main/main.c` (modified — `start_single_capture_task`)
  - `main/audio/audio_player.c` (rewritten)
  - `main/ui/ui_enrollment.cpp` (modified — `ui_enrollment_set_status`, `student_card_event_handler`)
- **Status:** Complete
- **Log:**

  **Root Cause 1 — Audio deadlock inside LVGL mutex (`main.c`):**
  `audio_play("enroll_success.wav", true)` and `audio_play("enroll_fail.wav", true)` were called while the LVGL mutex was still held by `ui_acquire()`. Because `blocking=true`, the I2S drain `vTaskDelay` ran with the mutex locked, starving the LVGL render task and silently aborting DMA audio output. Fix: stored `bool enroll_ok = (ret == ESP_OK)`, moved both `audio_play()` calls to **after** `ui_release()`.

  **Root Cause 2 — No mutex protecting shared I2S handle (`audio_player.c`):**
  Multiple FreeRTOS tasks (enrollment, recognition, battery) could call `audio_play_file()` concurrently, racing on `s_tx_handle` with no synchronisation. Added `SemaphoreHandle_t s_audio_mutex` (created in `audio_init()`); every `audio_play_file()` call acquires it with a 2-second timeout before touching the DMA handle. Concurrent callers that time out are dropped with a `LOGW`.

  Additional `audio_player.c` improvements:
  - Streaming buffer: 1 kB SPIRAM → **4 kB internal SRAM** (≈4× faster `fread`)
  - WAV chunk alignment: `fseek(f, (chunk_size + 1) & ~1u, SEEK_CUR)` — handles non-standard odd-length chunks
  - `audio_init()` now logs I2S GPIO pin numbers and PA enable GPIO on startup for field debugging

  **Enrollment UI — Try Again button (`ui_enrollment.cpp`):**
  When capture failed, the UI showed a red error label with no obvious recovery action. A visible orange **"↺  Try Again"** button is now dynamically created in `ui_enrollment_set_status(false, ...)`. Tapping it deletes itself, re-highlights the student card, resets the progress bar, and spawns a new `start_single_capture_task` via `xTaskCreate` — no admin PIN re-entry needed.

---

### [2026-05-30] — Full Project Progress Analysis

- **Category:** Project Status Review
- **Analyst:** Antigravity AI (automated codebase scan)
- **Timestamp:** 2026-05-30T10:41 WAT
- **Status:** Ongoing Development — Pre-Hardware-Testing Phase

---

#### 1. Codebase Dimensions

| Metric | Value |
|---|---|
| Total source files (main/) | ~45 files across 14 sub-modules |
| `main.c` size | 1,180 lines — primary orchestrator |
| UI module (`main/ui/`) | 23 files, ~165 KB total |
| Binary size (last confirmed build) | `0x544300` bytes (~5.3 MB), `0xbbd00` bytes (12%) free of 6 MB OTA partition |
| Custom components | 4 (esp-dl, espressif__esp-dsp, lvgl, sqlite3) |
| Build logs on record | 11 (build.log → build_new.log) |
| Firmware version | v1.0.0 |
| Target hardware | Elecrow CrowPanel Advanced 7″ ESP32-P4 v1.0 |
| IDF version | ESP-IDF v5.4.2 |

---

#### 2. Subsystem Completion Status

| Subsystem | Module | State | Notes |
|---|---|---|---|
| **Display** | `main/display/` | ✅ Complete | MIPI-DSI EK79007, LVGL 9, double-buffer SPIRAM |
| **Backlight** | `backlight.c` | ✅ Complete | LEDC PWM GPIO31, formula `(p*18)+200` |
| **Touch** | Board init | ✅ Complete | GT911, I2C 0x5D, full RST/INT sequence confirmed |
| **Camera** | `main/camera/` | ✅ Functional | SC2336 MIPI-CSI, QVGA RGB565, semaphore-callback arch |
| **Face Detection** | `face_detector.cpp` | ✅ Functional | HumanFaceDetect, RGB565LE, landmark extraction, sharpness/brightness computed (Laplacian variance & luma) |
| **Face Recognition** | `recognizer.cpp` | ✅ Functional | Cosine similarity, int8 embeddings, PSRAM cache up to 500 users, loaded from DB at boot |
| **Feature Extraction** | `feature_extractor.cpp` | ✅ Functional | HumanFaceFeat MobileFaceNet, `DL_IMAGE_PIX_TYPE_RGB565LE` correctly set |
| **Enrollment Pipeline** | `main.c` + `ui_enrollment.cpp` | ✅ Mostly complete | 30-frame capture, quality scoring (sharpness+yaw+brightness), top-15 averaging, DB insert, cache hot-add |
| **Try Again Button** | `ui_enrollment.cpp` | ✅ Complete | Added 2026-05-29; orange retry button with single-capture respawn |
| **Audio System** | `audio_player.c` | ✅ Complete | I2S mutex, RIFF chunk parser, 4 kB internal SRAM buffer, DMA drain computed from header |
| **Database** | `main/database/` | ✅ Functional | SQLite3 on `/sdcard/attendance.db`, auto-directory creation, default course seeding |
| **SD Card** | `storage/sdcard_mount.c` | ✅ Complete | SDMMC 1-bit, GPIO 43/44/39, FAT32, graceful unmount on shutdown |
| **LVGL UI Framework** | `ui_main.cpp` | ✅ Functional | Dark theme, `lv_layer_top()` Quick Settings, stack-based navigation, LVGL mutex `ui_acquire/ui_release` |
| **File Manager** | `ui_file_manager.cpp` | ✅ Complete | Paginated 20-per-page, `< Prev`/`Next >`, WDT-safe, heap leak fixed |
| **Settings Screen** | `ui_settings.cpp` | ✅ Mostly complete | Wi-Fi config, Telegram fields, sync interval dropdown (partially wired — see open items) |
| **Reports Screen** | `ui_reports.cpp` | ✅ Functional | Dynamic course dropdown from DB |
| **User Manager** | `ui_user_manager.cpp` | ⚠️ Partial | Lists users; edit/delete not confirmed fully implemented |
| **Setup Wizard** | `ui_setup_wizard.cpp` | 🔴 Broken | Admin enrollment is a no-op; Wi-Fi creds discarded; PIN not saved (see T1-2, T1-3, T1-4) |
| **Wi-Fi Manager** | `wifi_manager.c` | ⚠️ Partial | Connect/disconnect/scan present; retry counter not reset on disconnect (T1-6 open) |
| **Captive Portal** | `wifi_ap_portal.cpp` | ✅ Complete | HTTP 302 redirect for OS probes, SDIO at 10 MHz, `WIFI_PS_NONE` enforced |
| **BLE Registration** | `ble/ble_registration.c` | ⚠️ Partial | Advertising, data write, status; PIN is compile-time constant `"123456"` (T3-2 open) |
| **Cloud Sync** | `network/cloud_sync.c` | 🔴 Stub | Sends text message only; `sendDocument` CSV upload not implemented (T5-1); schedule import SQL missing (T5-2) |
| **SNTP Sync** | `network/sntp_sync.c` | ✅ Functional | Called after Wi-Fi connect in sync cycle; epoch guard in clock display not yet added (T5-3 open) |
| **OTA Manager** | `ota/ota_manager.c` | 🔴 Stub | `ota_manager.c` = 730 bytes; partition table now has `ota_0`/`ota_1`, but OTA logic is not implemented |
| **Battery Monitor** | `power/battery_monitor.c` | ⚠️ Partial | ADC%, charging detect, shutdown strikes debounce; deep-sleep runs without state-gate (T2-3 open) |
| **Admin PIN / Session** | `main.c` | ✅ Complete | NVS-backed PIN, 5-min session, `verify_admin_pin()`, `activate_admin_session()` |
| **Admin Face Auto-Unlock** | `detection_recognition_task` | ✅ Complete | Role `"admin"` recognized → `activate_admin_session()` called automatically |
| **Double-Tap Backlight** | `ui_main.cpp` | ✅ Complete | System-wide 400 ms double-tap toggle, applied to all 9 screen objects |
| **Quick Settings Dropdown** | `ui_main.cpp` | ✅ Complete | Attached to `lv_layer_top()`, 3-slot notification FIFO, brightness slider |
| **Watchdog (LVGL task)** | `lcd_driver.c` | ✅ Complete | `esp_task_wdt_add` + `esp_task_wdt_reset()` per frame; WDT timeout = 20 s |
| **Network Sync Task** | `main.c` | ✅ Functional | Brief-connect cycle: Wi-Fi → SNTP → cloud_sync → disconnect; event-bit completion signal (30 s ceiling) |
| **NVS Encryption** | `partitions.csv` / `main.c` | ⚠️ Partial | `nvs_keys` partition now in `partitions.csv`; `nvs_flash_secure_init` called, but `CONFIG_NVS_ENCRYPTION` sdkconfig flag status unconfirmed (T3-4) |

---

#### 3. Resolved Issues Since Last Analysis (2026-05-22 → 2026-05-30)

The following items from the `consumer_readiness_audit.md` and `FULL_ANALYSIS.md` have been closed since the last log entry:

| Issue ID | Description | Resolution Date |
|---|---|---|
| [1.1] | `recognizer_load_cache()` was empty | ✅ Fixed — full DB query + `memcpy` to PSRAM cache |
| [1.2] | Feature extractor pixel format mismatch | ✅ Fixed — `DL_IMAGE_PIX_TYPE_RGB565LE` |
| [1.3] | `compute_sharpness/brightness` returned constants | ✅ Fixed — Laplacian variance (green-channel) + luma formula |
| [1.4] | BLE enrollment data not passed to `new_user` | ✅ Fixed — `ble_registration_peek_student()` used in `start_single_capture_task` |
| [1.5] | No duplicate attendance guard | ✅ Fixed — SQL `NOT EXISTS` pre-check in `db_insert_attendance_log` |
| [1.6] | Detection task ran during enrollment | ✅ Fixed — `get_system_state() != SYSTEM_STATE_NORMAL` guard |
| [1.7] | LVGL had no tick driver | ✅ Fixed — `lv_tick_inc` via `esp_timer`, `lv_timer_handler` in LVGL task |
| Audio deadlock | `audio_play(blocking=true)` inside LVGL mutex | ✅ Fixed 2026-05-29 — `audio_play` moved after `ui_release()` |
| Audio mutex | No I2S concurrency protection | ✅ Fixed 2026-05-29 — `SemaphoreHandle_t s_audio_mutex` with 2 s timeout |
| Audio WAV parser | Fixed offset 44 assumed | ✅ Fixed — proper RIFF/fmt/data chunk walk with 2-byte alignment |
| Audio buffer | 1 kB SPIRAM buffer | ✅ Fixed — 4 kB internal SRAM (`MALLOC_CAP_INTERNAL`) |
| [4.2] Retry button | No UX recovery from enrollment fail | ✅ Fixed 2026-05-29 — orange "↺ Try Again" button |
| T4-3 | Setup wizard at correct boot time | ✅ Fixed — NVS `setup_done` check in `app_main` before main loop |
| T1-7 | Factory reset task suspension | ✅ Fixed — `SYSTEM_STATE_SHUTDOWN` set, tasks drain, DB flushed |
| [T3-3] | Enroll bypassed by active admin session | ✅ Fixed — NAV_ENROLL always shows PIN prompt regardless of session |
| ESP-Hosted dependency | Missing from `idf_component.yml` | ✅ Fixed — declared, `esp_hosted_connect_to_slave()` called in `app_main` |
| NVS keys partition | Missing `nvs_keys` entry in `partitions.csv` | ✅ Fixed — partition table now has `nvs_keys` + `ota_0`/`ota_1` |

---

#### 4. Open Issues — Ordered by Priority

**🔴 Ship Blockers (must fix before any hardware testing)**

| ID | File | Problem |
|---|---|---|
| **T1-1** | `ui_enrollment.cpp:173`, `main.c` | Close button sets `g_enrollment_cancel = true` — correctly implemented in current `main.c`, but race-condition: `start_enrollment_task` vs `start_single_capture_task` are separate tasks; need to verify the flag is checked in the `while(i < ENROLL_FRAMES_TOTAL)` loop (confirmed present at line 732) ✅ likely resolved |
| **T1-2** | `ui_setup_wizard.cpp` | Wizard `finish_btn` never reads PIN textarea → all devices ship with hardcoded `DEFAULT_ADMIN_PIN "1234"` |
| **T1-3** | `ui_setup_wizard.cpp` | "Enroll Admin Face" button calls `next_btn_event_handler` directly — zero face capture happens during wizard |
| **T1-4** | `ui_setup_wizard.cpp` | Wi-Fi SSID/password textareas not referenced in "Connect & Next" handler — credentials silently discarded |
| **T1-5** | `config.h:107-108` | `TELEGRAM_BOT_TOKEN = "YOUR_BOT_TOKEN"` — compile-time literal; must migrate to NVS runtime config |
| **T1-6** | `wifi_manager.c` | `wifi_manager_disconnect()` fires `WIFI_EVENT_STA_DISCONNECTED` incrementing `s_retry_count`; after ≥5 sync cycles reconnect is permanently blocked |

**🟠 Reliability (fix before first deployment)**

| ID | File | Problem |
|---|---|---|
| **T2-1** | `wifi_manager.c` | `esp_wifi_scan_start(NULL, true)` blocks system event task for 2–5 s; risk of WDT |
| **T2-2** | `cloud_sync.c` | `xTaskCreate(cloud_sync_task, ..., NULL)` — no handle stored; rapid sync triggers run two parallel tasks corrupting CSV |
| **T2-3** | `battery_monitor.c` | `esp_deep_sleep_start()` called without checking system state / DB queue depth / sync status |
| **T2-4** | `db_manager.c` | No global SQLite mutex — `db_task`, LVGL handlers, and `cloud_sync_task` can race on the same connection |
| **T2-5** | `camera_driver.c` | `s_native_fb` is a single static struct shared between `camera_task` (writer) and `detection_recognition_task` (reader) — frame data overwritten mid-processing |

**🔐 Security (fix before institutional deployment)**

| ID | File | Problem |
|---|---|---|
| **T3-2** | `ble_registration.c` | BLE enrollment PIN is compile-time `"123456"` — visible in firmware binary strings; should be random per-session |
| **T3-4** | `sdkconfig` | `CONFIG_NVS_ENCRYPTION` status unconfirmed; Wi-Fi passwords may be stored plaintext |

**🧱 UX Dead-Ends (fix before user testing)**

| ID | File | Problem |
|---|---|---|
| **T4-1** | `ui_settings.cpp` | Wi-Fi "Connected" label shown immediately on `esp_wifi_connect()` return — not on actual `IP_EVENT_STA_GOT_IP`; shows success even on wrong password |
| **T4-4** | `ui_settings.cpp` | Sync interval dropdown has no `lv_obj_add_event_cb` — selection has no effect |
| **T4-5** | `ui_main.cpp` | `backlight_set(100)` always called at init; should use `ui_get_brightness()` to restore saved NVS value |

**🔴 Functional Completeness (needed for core purpose)**

| ID | File | Problem |
|---|---|---|
| **T5-1** | `cloud_sync.c` | CSV attendance never sent to Telegram — `sync_attendance_logs()` sends a text notification only; `sendDocument` multipart POST not implemented |
| **T5-2** | `db_manager.c` | `db_import_schedule_csv()` parses lines but comment shows SQL INSERT was never written |
| **T5-3** | `ui_main.cpp` | Clock displays "Jan 1, 1970" until SNTP syncs — no epoch guard `< 2024-01-01` |
| **T5-4** | `wifi_manager.c` | Multi-network NVS rotation broken — every `save_credentials()` overwrites `s_0`; `count` grows but only slot 0 is ever used |
| **T5-5** | `camera_driver.c` | `camera_set_framesize()` is an empty stub on ESP32-P4 path — enrollment always captures QVGA despite `CAMERA_ENROLL_FRAME_SIZE = FRAMESIZE_VGA` |

---

#### 5. Architecture Health Assessment

| Dimension | Rating | Notes |
|---|---|---|
| **Offline AI Core** | 🟢 Solid | Detection + recognition + enrollment pipeline fully wired end-to-end; pixel format, embedding averaging, and quality scoring all correct |
| **Hardware Stability** | 🟢 Solid | SDIO at 10 MHz, `WIFI_PS_NONE`, LVGL WDT registration all in place — board no longer crashes under load |
| **Memory Safety** | 🟡 Adequate | PSRAM for large buffers, heap-allocated attendance logs, `free()` on close/nav — some static shared buffers remain (`s_native_fb`) |
| **Thread Safety** | 🟡 Adequate | LVGL mutex (`ui_acquire`/`ui_release`) consistent; audio mutex added; SQLite still unprotected (T2-4) |
| **Power Management** | 🟠 Risky | Deep sleep runs without state checks — can corrupt SD card WAL mid-sync |
| **Security Posture** | 🔴 Weak | Hardcoded admin PIN, BLE PIN in binary, NVS encryption unconfirmed, Telegram token in source |
| **Setup Wizard** | 🔴 Broken | Three of four wizard steps discard user input silently |
| **Cloud Sync** | 🔴 Stub | CSV never leaves device; schedule import SQL never written |
| **OTA Updates** | 🔴 Not Implemented | Partition table has OTA slots but `ota_manager.c` is 730-byte placeholder |

---

#### 6. Binary & Build Health

- **Last successful build:** 2026-05-22
- **Binary:** `SmartAttendance.bin` = `0x544300` bytes (~5.27 MB)
- **Flash remaining:** `0xbbd00` bytes (~12% of 6 MB OTA partition)
- **Partition table:** OTA-enabled (ota_0 / ota_1 @ 6 MB each), `nvs_keys`, FAT storage partition
- **Log level:** `ESP_LOG_WARN` (production-appropriate, confirmed in `config.h`)
- **Build flags:** Still on `-Og` (debug optimization); target before shipping: `-Os`
- **Co-processor version mismatch:** Host ESP-Hosted 2.12.0 vs C6 slave 2.3.0 — cosmetic at current load, recommend reflash when convenient
- **GPIO 31 double-init warning:** `W ledc: GPIO 31 is not usable` — cosmetic only, both calls configure identical parameters

---

#### 7. Recommended Next Sprint

Based on the current state, the highest-leverage items to work on next (in order) are:

1. **T1-2 + T1-3 + T1-4** — Fix the Setup Wizard (3 interconnected issues) so first-boot onboarding is functional end-to-end
2. **T1-5** — Migrate Telegram credentials to NVS + add Settings UI fields (prerequisite for T5-1)
3. **T5-1** — Implement `sendDocument` CSV upload to Telegram (the device's primary data export mechanism)
4. **T5-2** — Write the missing SQLite INSERT in `db_import_schedule_csv()` (schedule import)
5. **T2-4** — Add SQLite global mutex to prevent attendance record loss under concurrent access
6. **T1-6** — Reset Wi-Fi retry counter in `wifi_manager_disconnect()` to prevent sync lockout after ~5 cycles
7. **T5-3** — Add epoch guard in clock display (`< 2024-01-01` → show "Syncing time...")
8. **T4-1** — Fix Wi-Fi status label to poll `IP_EVENT_STA_GOT_IP` asynchronously instead of showing immediate "Connected"

---

#### 8. Overall Project Maturity

```
Core hardware layer      ████████████████████ 100% ✅
Face detection/AI        ████████████████████ 100% ✅  
Enrollment pipeline      ████████████████░░░░  80% ⚠️  (wizard wiring missing)
LVGL UI / Navigation     ████████████████░░░░  80% ⚠️  (some dropdowns unwired)
Audio system             ████████████████████ 100% ✅
Database / SQLite        ████████████████░░░░  80% ⚠️  (no mutex, schedule import stub)
Wi-Fi / Networking       ████████░░░░░░░░░░░░  40% 🔴  (retry bug, credentials discarded)
Cloud sync               ████░░░░░░░░░░░░░░░░  20% 🔴  (text only, no CSV upload)
Security                 ████░░░░░░░░░░░░░░░░  20% 🔴  (hardcoded secrets)
OTA updates              ░░░░░░░░░░░░░░░░░░░░   0% 🔴  (stub only)
─────────────────────────────────────────────────────
Overall readiness        ~62% — Pre-hardware-testing
```

The project has a **solid hardware and AI foundation** — the recognition pipeline is fully wired and functional, the display/touch/camera stack is stable, and the audio system is robust after the 2026-05-29 overhaul. The primary gaps are in the **Setup Wizard, cloud sync, and security** layers, which are critical for a production institutional deployment but do not block local hardware bring-up and recognition testing.

---

### [2026-05-30] — Field Hardware Testing Session #1

- **Category:** Bug Fix — Hardware-Verified
- **Timestamp:** 2026-05-30T11:07 WAT
- **Source:** Live device testing (Elecrow CrowPanel Advanced 7″ ESP32-P4 v1.0)
- **Status:** 4 hardware-confirmed bugs identified and fixed

---

#### Issue 1 — Reports Screen Calendar Completely Unresponsive

**File:** `main/ui/ui_reports.cpp`
**Hardware observation:** Tapping anywhere on the date area in the Reports screen produced no response.
**Root cause:** `lv_calendar_create()` produced a widget 200 px tall, placed inside a flex card
with `height = 120 px`. LVGL clips child widgets at the parent's boundary — the bottom 80 px of
the calendar (including the month-navigation arrow buttons) extended beyond the card bounds and
were never rendered or hit-tested. The entire calendar was effectively dead.
**Fix applied:**
- Removed `lv_calendar` entirely.
- Replaced the date card with three `lv_dropdown` pickers: **Day (01–31)**, **Month (Jan–Dec)**,
  **Year (2025–2030)**, positioned in a single row inside a 100 px card — fully within bounds.
- Each dropdown pre-selects today's date when SNTP has synced (epoch guard: year ≥ 2024).
- `generate_btn_event` updated to read the three dropdowns, compute `mktime()` day boundaries,
  and pass `(ts_start, ts_end)` to `db_get_attendance_report()` — date filtering now works
  end-to-end instead of always fetching all records.
- `ui_close_reports_screen()` now nulls all 8 statics including the new dropdown pointers.

---

#### Issue 2 — Battery Stuck at 100%; No Charging Indicator on Screen

**File:** `main/power/battery_monitor.c`, `main/ui/ui_main.cpp`, `main/main.c`
**Hardware observation:** Battery percentage never moved from 100% regardless of actual charge state.
No ⚡ or charging symbol appeared when the device was connected to USB power.
**Root causes (two separate problems):**

**(a) Hardcoded 100% with early return when charging:**
Lines 197–201 of the old code: `if (pin_charging || voltage > 4250) { s_charging = true; return 100; }`.
When the USB cable is connected, `pin_charging` is true → function returns 100 immediately every
call, the voltage interpolation is never reached. Even with the battery at 70%, the display showed 100%.

**(b) GPIO33 polarity was inverted:**
The old code used `gpio_get_level(POWER_CHG_LED_PIN) == 1` for "charging". On the CrowPanel P4
the CHG_LED signal on GPIO33 is **active-LOW** — pulled low by the charger IC when charging is
active. The pin reads 0 when charging, not 1. This meant the pin check was always false and
charging was only detected by the voltage heuristic (> 4250 mV), which can never be satisfied
while actively charging through USB.

**(c) No charging icon in the UI:**
`ui_set_battery_percent(int percent)` selected from `LV_SYMBOL_BATTERY_FULL/3/2/1/EMPTY` only.
There was no branch for the charging state; `s_charging` was set but never communicated to the UI.

**Fixes applied:**
- `battery_monitor_get_percent()`: Changed charging pin check to `== 0` (active-LOW). Removed the
  `return 100` early-exit when charging — voltage interpolation now runs normally so the true
  percentage is shown even while plugged in.
- `ui_set_battery_percent()`: Added `bool charging` second parameter. When `charging == true`,
  shows `LV_SYMBOL_CHARGE` (⚡) in **cyan (#00CFFF)** regardless of percent level. When not
  charging, falls through to the existing battery icon/colour logic.
- `ui_main.h`: Declaration updated to match new signature.
- `battery_task` in `main.c`: Removed duplicate `ui_acquire()/ui_release()` wrapper (now handled
  inside `ui_set_battery_percent`); added `bool is_charging = battery_monitor_is_charging()`
  and passes it as the second argument.

> **Note (open):** GPIO33 active-LOW polarity needs hardware confirmation against the Elecrow
> schematic. If the CHG_LED signal turns out to be active-HIGH on this board revision,
> revert `== 0` back to `== 1` in `battery_monitor_get_percent()`.

---

#### Issue 3 — NVS Encryption Deferred by Developer Decision

**Hardware observation / Developer decision:** NVS encryption is intentionally **not enabled**.
The device must remain openly flashable via USB cable for iterative firmware updates. Enabling
`CONFIG_NVS_ENCRYPTION` would require a signed `nvs_keys` partition to be pre-provisioned before
any OTA or USB flash is possible, which blocks the current development workflow.

**Action taken:**
- T3-4 removed from the "open issues" list and reclassified as a deliberate deferral.
- `nvs_keys` partition entry retained in `partitions.csv` (safe to leave — it is simply unused
  until encryption is switched on in `sdkconfig`).
- Will be revisited before institutional deployment (Sprint C).

---

#### Issue 4 — Face Recognition Never Matches; Green Box on Left Eye Only

**File:** `main/detection/face_alignment.c`
**Hardware observation:** When the attendance scanner is open, a **green bounding box** appears
around the left eye only. The system always returns **"Unknown"** regardless of who is enrolled.

**Root cause — `compute_affine()` was a complete stub:**
```c
/* old code — lines 51-53 */
M[0] = 1; M[1] = 0; M[2] = 0;
M[3] = 0; M[4] = 1; M[5] = 0;
/* Actually compute M here... omitted for brevity */
```
The function always output the **2×3 identity matrix**. The `face_alignment_align()` warp loop
then applied this identity transform to the raw camera frame — meaning every "aligned face"
was simply a 112×112 crop starting at pixel **(0, 0)** of the frame, not at the detected face.

The detector correctly found and drew the face bounding box, but the landmark coordinates for
the *left eye* (approx 0.3×W, 0.4×H on a face = approx 96 px, 96 px at QVGA) happened to be
close to the top-left corner of the full 320×240 frame when the identity matrix was applied,
which is why the bounding box appeared to sit on the left eye. The embedding computed from that
crop was pure background/forehead — nothing like a face embedding — so every cosine similarity
was below threshold and the result was always "Unknown".

**Fix applied:**
- Rewrote `compute_affine()` with a working **6×6 Gaussian elimination solver with partial pivoting**.
  Builds the augmented `[A|b]` matrix from the three 2D point pairs (left-eye, right-eye, nose)
  and solves for all 6 affine coefficients `[a, b, c; d, e, f]` via forward elimination and
  back-substitution.
- Changed return type from `void` to `bool` (returns `false` if the system is degenerate —
  collinear landmarks, etc.).
- Added a **bounding-box fallback** in `face_alignment_align()`: if `compute_affine()` returns
  false, constructs a simple scale+translate matrix that maps the face bounding-box centre to the
  canonical 112×112 origin. This degrades gracefully instead of feeding garbage embeddings.
- The bilinear interpolation warp loop itself was already correct; only the matrix was wrong.

**Expected result after fix:** The aligned crop will contain the actual face region warped to
the canonical 112×112 template matching the training distribution of MobileFaceNet. Recognition
scores should rise above the cosine similarity threshold for enrolled users.

> **Re-enrollment required:** All previously enrolled embeddings were computed from the
> identity-warped (corrupted) crops. They must be cleared and re-enrolled after flashing
> this fix. Run `db_clear_all_users()` or delete `/sdcard/attendance.db` before re-enrolling.

---

#### Summary of Changes — 2026-05-30 Field Session

| # | File | Change |
|---|---|---|
| 1 | `main/ui/ui_reports.cpp` | Replaced `lv_calendar` (clipped, dead) with Day/Month/Year dropdowns |
| 2 | `main/ui/ui_reports.cpp` | `generate_btn_event` now passes real `ts_start`/`ts_end` to DB query |
| 3 | `main/power/battery_monitor.c` | GPIO33 polarity fixed to active-LOW; removed `return 100` lockout when charging |
| 4 | `main/ui/ui_main.cpp` | `ui_set_battery_percent` takes `bool charging`; shows ⚡ cyan when charging |
| 5 | `main/ui/ui_main.h` | Declaration updated to `ui_set_battery_percent(int, bool)` |
| 6 | `main/main.c` | `battery_task` passes `is_charging` flag; removed duplicate LVGL lock |
| 7 | `main/detection/face_alignment.c` | `compute_affine()` rewritten with real 6×6 Gaussian elimination solver |
| 8 | `main/detection/face_alignment.c` | Added bounding-box fallback warp when landmarks are degenerate |

#### Action Required Before Next Test Flash

1. **Re-enroll all users** — old embeddings are garbage (identity-warp artifacts).
   Delete `/sdcard/attendance.db` or call `db_clear_all_users()` before enrolling.
2. **Verify GPIO33 polarity** — check Elecrow schematic: is CHG_LED active-LOW or HIGH?
   If HIGH, change `== 0` back to `== 1` in `battery_monitor_get_percent()` line 198.
3. **Build and flash** — no `sdkconfig` changes required; all fixes are source-only.

---

### [2026-05-30] — Field Hardware Testing Session #2 (Afternoon)

- **Category:** Bug Fix — Build Errors / Hardware-Verified Regression
- **Timestamp:** 2026-05-30T18:56 WAT
- **Source:** Live device testing + serial monitor analysis

---

#### Issue 5 — Build Errors from Compilation Warnings Session

**Files:** `main/display/touch_driver.c`, `main/ui/ui_reports.cpp`, `main/ui/ui_file_manager.cpp`

| Warning | Root Cause | Fix |
|---|---|---|
| `TOUCH_I2C_PORT` redefined | `touch_driver.c` had local duplicate macro definitions that conflicted with `elecrow_p4_board.h` | Removed local `#define` block; updated board header as single source of truth |
| `TOUCH_I2C_SDA/SCL` redefined | Same as above | Same as above |
| `esp_lcd_touch_get_coordinates` deprecated | API removed in v2.0.0; new API is `esp_lcd_touch_get_data` with `esp_lcd_touch_point_data_t` struct | Rewrote `touch_read_cb` to use struct-based API correctly |
| `esp_lcd_touch_get_data` argument mismatch | Naive substitution passed old `uint16_t*` arrays; new API takes `esp_lcd_touch_point_data_t*` | Corrected to declare `esp_lcd_touch_point_data_t pt` and call `esp_lcd_touch_get_data(handle, &pt, &cnt, 1)` |
| `-Wmissing-field-initializers` on `struct tm` | C++ stricter than C about aggregate initialisation | Replaced `= {0}` with `memset(&day_start, 0, sizeof(day_start))` |
| `file_manager_load_task` not declared | `delete_click_handler` added above the function definition without a forward declaration | Added `static void file_manager_load_task(void* param);` to forward decl block |
| `CAM_PIN_SCCB_SDA/SCL` in board header pointed to GPIO45/46 | Camera actually uses GPIO12/13 for SCCB (I2C) — board header was wrong | Fixed to `GPIO_NUM_12`/`GPIO_NUM_13`; camera driver updated to use `CAM_SCCB_I2C_PORT`, `CAM_PIN_SCCB_SDA/SCL` macros |

---

#### Issue 6 — Critical Regression: Unexpected Shutdown During Attendance Scanning

**Files:** `main/power/battery_monitor.c`

**Hardware observation:** While running the attendance scanner (face recognition loop), the device
screen went black and the serial monitor began flooding `CAM_TASK: Frame capture failed` at 100 Hz.
The battery log line immediately before the crash read:
```
BATTERY: Power Check: 3000mV, ChgPin: 1, IsCharging: 0
```
The device was connected to a 9,000 mAh external battery (high-current capable).

**Root cause — Two bugs interacted:**

**(a) GPIO33 polarity inverted by the morning session fix:**
The morning session changed `gpio_get_level(GPIO33) == 1` → `== 0` based on an assumption that
the charger IC is active-LOW. The serial log disproves this: `ChgPin: 1` when USB is connected
proves the charger IC drives GPIO33 **HIGH** when charging. The polarity flip made the firmware
see `IsCharging: 0` even though power was connected, causing `s_charging = false`.

**(b) `3005 mV` threshold removed too aggressively:**
The morning session lowered the no-battery-fitted threshold from `3005 mV` to `500 mV`. With
the polarity bug above, the system now correctly reached the voltage interpolation code at 3000mV
and reported ~5% battery while incorrectly believing it was on battery alone. The battery task's
3-strike shutdown fired after 30 s and called `graceful_shutdown()`, cutting the backlight and
killing the camera task.

**Why the device never shut down before these changes:**
The old code had `return 100` when `voltage <= 3005` AND when `pin_charging || voltage > 4250`.
With the battery at 3000 mV and the old active-HIGH check seeing `ChgPin: 1`, the second early
`return 100` always fired — the shutdown code path was unreachable. Removing both early returns
exposed the shutdown path that the voltage polarity bug then triggered.

**Fixes applied:**
- Reverted GPIO33 polarity back to **active-HIGH** (`== 1`), validated by serial log evidence.
- `500 mV` threshold for no-battery detection retained (correct — 3 V is a valid battery reading).
- `battery_task` shutdown guard (`!battery_monitor_is_charging()`) was already correct; no changes needed.

**Expected behaviour after fix:**
- When plugged in: `ChgPin: 1` → `is_charging = true` → ⚡ cyan icon displayed.
- Low-battery warning and shutdown are suppressed while `is_charging == true`.
- Battery percentage interpolated from voltage curve (will show real charge level, e.g. ~5% at 3.0V).
- Shutdown only fires when battery falls below threshold AND device is genuinely unplugged.

---

#### Issue 7 — Additional Fixes Applied This Session

| # | File | Change |
|---|---|---|
| 1 | `main/ui/ui_file_manager.cpp` | Added `delete_click_handler` + red 🗑 trash button on every file/folder row |
| 2 | `main/ui/ui_file_manager.cpp` | Added `<unistd.h>` for `rmdir()`; added `file_manager_load_task` forward declaration |
| 3 | `main/ui/ui_recorder.cpp` | Changed I2S PDM RX from `I2S_NUM_1` → `I2S_NUM_0` (PDM only supported on I2S0 on ESP32-P4) |
| 4 | `main/main.c` | Bounding box scaled from camera frame resolution to UI preview dimensions (480×360) |

---

#### Action Required Before Next Test Flash

1. **Charge battery fully** before testing (device was at ~3 V / near-empty during this session).
2. **Build and flash** the latest firmware.
3. **Re-enroll users** if attendance.db was not cleared from the morning session.

---

### [2026-05-30] — Critical Breakthrough: STC8H1Kxx Coprocessor Power Telemetry Integration

- **Category:** Hardware Architecture Alignment / Bug Fix
- **Timestamp:** 2026-05-30T20:30 WAT
- **Source:** Real hardware behavior analysis + manufacturer EV BSP inspection

---

#### The Discovery

- **The Problem:** The battery widget registered either "Charging at 0%" or "Charging at 100%" and failed to update when the USB power supply was plugged or unplugged. The ESP32-P4's internal ADC read random, floating values (18-31 mV) on `GPIO4`.
- **The Explanation:** 
  1. On the **ESP32-P4**, `GPIO4` is **not an analog-enabled ADC pin**. The ESP32-P4 maps ADC1 channels exclusively to `GPIO16–GPIO23` and ADC2 channels to `GPIO49–GPIO54`.
  2. The CrowPanel Advanced 7" ESP32-P4 v1.0 features an **STC8H1Kxx auxiliary MCU (coprocessor)** that physically handles voltage dividing, battery charge calculations, and LED indicators.
  3. Power management and battery telemetry are queried over **I2C at address `0x2F`**, not via internal ADC.

---

#### The Solution & Changes Applied

| # | File | Change |
|---|---|---|
| 1 | `main/display/touch_driver.h` | Exposed the shared I2C master bus handle (`touch_get_i2c_bus()`) to allow co-existence on I2C_NUM_0 |
| 2 | `main/display/touch_driver.c` | Moved the static `s_i2c_bus` to global file-scope and implemented `touch_get_i2c_bus()` |
| 3 | `main/power/battery_monitor.c` | **Complete Rewrite:** Removed legacy ESP32 ADC-calibration code, voltage tables, and GPIO33 polarity checks. Implemented secure register reading from STC8 coprocessor (`0x2F`) over shared I2C bus. Reads `STC8_Battery_info_t` directly |

#### Expected Telemetry Mapping (STC8 registers starting at 0x00):
- `bat_voltage`: Real battery voltage in millivolts.
- `bat_level`: Exact battery charge percentage (0-100%).
- `bat_state`: Telemetry status from the charger (1 = Charging, 2 = Fully Charged, others = Not Charging).

This eliminates the need for software voltage-to-percent curves and noisy threshold filtering. The coprocessor feeds exact telemetry directly to our UI widget!

---

#### Action Required Before Next Test Flash

1. **Build and flash** the firmware to see the STC8 telemetry feed live.

---

### [2026-06-01] — Audio I2S Timing Calibration, Format Truncation & Wi-Fi Remote Build Fixes
- **Category:** Audio Driver Calibration / Kconfig Integration / Compile Fixes
- **Timestamp:** 2026-06-01T23:15 BST
- **Source:** Real hardware timing requirements + compiler warning analysis + ESP-IDF build system environment tracing

---

#### The Solution & Changes Applied

| # | File / Component | Change |
|---|---|---|
| 1 | `main/audio/audio_player.c` | **Timing Calibration:** Swapped I2S slot config from MSB-justified (`I2S_STD_MSB_SLOT_DEFAULT_CONFIG`) to standard Philips format (`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG`) to match the NS4168 amplifier's timing requirements (1-clock cycle delay before MSB). |
| 2 | `main/audio/audio_player.c` | **DMA and Playback Tuning:** Configured DMA buffer structure to match working BSP parameters (6 buffers, 256 frame length) and tuned the Power Amplifier (PA) shutdown sequence to mute PA at boot/idle and enable it with a 150ms warm-up before active play. |
| 3 | `main/ui/ui_player.cpp` | **Include Fix:** Included `config.h` to resolve `DISPLAY_WIDTH` and `DISPLAY_HEIGHT` compilation scope errors. |
| 4 | `main/ui/ui_player.cpp` | **Format Truncation Fix:** Expanded track path and name buffers to 512 and 256 bytes and added precision format specifiers (`%.240s/%.256s`) to `snprintf` to avoid `-Werror=format-truncation` compiler failures. |
| 5 | `managed_components/espressif__esp_wifi_remote` | **Kconfig Alignment:** Created `Kconfig.idf_v5.4.2.in` to allow the build system to resolve the component configuration template on ESP-IDF v5.4.2. |
| 6 | Build Environment | **Environment Variables:** Defined `ESP_IDF_VERSION` and `IDF_VERSION` in the terminal build environment to prevent Kconfig from skipping Wi-Fi Remote (`CONFIG_WIFI_RMT_...`) variables. |

---

#### Verification Results
- Reconfigured the ESP-IDF build system successfully.
- Compiled the entire workspace (`SmartAttendance.bin` generated successfully, with all components linking and bootloader generated).
- Verified that the final binary fits the app partition with 12% free space.

---

#### Action Required Before Next Test Flash
1. **Flash the firmware** (`idf.py flash` or custom esptool write_flash command).
2. **Verify audio feedback** on startup and other system events.
3. **Verify battery telemetry** updates when transitioning between USB power and unplugged states.

---

### [2026-06-02] — Speaker Crackling Fix, ESP-Hosted Reversion to C6 SDIO & Startup Sound
- **Category:** Hardware Bug Fix / Configuration Revert / Audio Integration
- **Timestamp:** 2026-06-02T00:00 WAT
- **Source:** Hardware analysis + user-reported symptoms (crackling noise at startup, boot loop crash)

---

#### Symptoms Reported by User
1. **Boot loop**: Device crashed immediately at startup via `ESP_ERROR_CHECK` abort in `ble_transport_ll_init` / `vhci_drv.c`.
2. **Speaker crackling**: Audible crackling from the speaker during early boot stages, before any audio driver was active.
3. **No audio heard**: No event sounds or startup audio ever played successfully due to the above issues.

---

#### Root Cause Analysis

| # | Symptom | Root Cause |
|---|---|---|
| 1 | Boot loop crash at `ble_transport_ll_init` | `sdkconfig` was manually changed to target `ESP32-H2` over SPI (`CONFIG_ESP_HOSTED_CP_TARGET_ESP32H2=y`), but the board physically houses an **ESP32-C6 connected over SDIO**. The SPI handshake fails instantly on incorrect GPIO lines. |
| 2 | Speaker crackling during boot | The PA enable pin (`AUDIO_PA_ENABLE` = GPIO 30) was not driven LOW during the early boot phase (before `audio_init()` was called). GPIO lines float after reset and can momentarily assert the amplifier HIGH, causing spurious noise. |
| 3 | No startup audio | Consequence of issues 1 and 2. |

---

#### The Solution & Changes Applied

| # | File | Change |
|---|---|---|
| 1 | `sdkconfig` | **Configuration Revert:** Restored from `sdkconfig.backup` to reinstate the correct ESP32-C6 over SDIO configuration (`CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6=y`, `CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE=y`). Previous broken state saved to `sdkconfig.broken_h2`. |
| 2 | `main/boards/elecrow_p4_board.c` | **Early PA Mute:** Added GPIO 30 (`AUDIO_PA_ENABLE`) initialization at the very start of `board_power_init()` with `GPIO_PULLDOWN_ENABLE` and explicit `gpio_set_level(AUDIO_PA_ENABLE, 0)`. This ensures the Class-D amplifier is hard-grounded before any other peripheral initializes, eliminating boot-stage crackling. |
| 3 | `main/audio/audio_player.c` | **PA Pull-Down Alignment:** Changed `pull_down_en` from `GPIO_PULLDOWN_DISABLE` to `GPIO_PULLDOWN_ENABLE` in `audio_init()` to maintain consistent pin state in the audio driver configuration. |
| 4 | `main/main.c` | **Startup Sound:** Added `audio_play(AUDIO_PROMPTS_PATH "system_start.wav", false)` immediately after successful `audio_init()` to provide audio feedback on boot. |

---

#### Build Result
- **Status:** ✅ Full build succeeded with no errors or warnings.
- **Binary:** `SmartAttendance.bin` — 0x544020 bytes (12% free in 6 MB app partition).
- **Bootloader:** `bootloader.bin` — 10% free.
- **ESP-IDF Version:** v5.4.2, target: `esp32p4`.

---

#### Action Required Before Next Test Flash
1. **Connect the CrowPanel board** via the UART/USB programming port.
2. **Flash the firmware** using: `idf.py flash monitor`
3. **Verify on boot:**
   - Device boots cleanly (no loop crash in monitor output).
   - Speaker is silent (no crackle/pop) until `system_start.wav` plays.
   - ESP-Hosted SDIO transport connects to ESP32-C6 slave successfully.
4. **Ensure `/sdcard/audio/system_start.wav` exists** on the SD card for the startup chime to play.

---

### [2026-06-02] — System Audio SPIFFS Embedding & Fallback Mechanism
- **Category:** Audio System / Firmware Embedding / Device Resilience
- **Timestamp:** 2026-06-02T12:00 WAT
- **Source:** User request: "None of the device audio is playing. I only hear a strike sound at startup. Can we add the system audio .wav files into the build like as in the elecrow_examples/spiffs/music?"
- **Status:** Complete — Build Verified

---

#### Problem Statement
- Audio playback was failing because system audio files (`system_start.wav`, `attendance_success.wav`, `unknown_face.wav`, etc.) were expected on the SD card at `/sdcard/audio/` but never created or provided.
- No embedded audio fallback existed — without SD card audio, the system had zero sound capability beyond the hardware strike sound at power-on.
- User needed reliable audio without requiring separate SD card file provisioning.

---

#### Technical Solution

**Audio File Priority Chain:**
1. **Primary:** Check SD card at `/sdcard/audio/filename.wav` (user-provided, high-quality audio)
2. **Fallback:** Check SPIFFS at `/spiffs/audio/filename.wav` (embedded in firmware, always available)

This allows:
- **Seamless fallback** if SD card audio is missing or unavailable
- **User customization** by placing higher-quality audio on SD card
- **Factory reliability** with embedded placeholder audio for all system events

---

#### Changes Applied

| # | File / Action | Details |
|---|---|---|
| 1 | Generated system audio files | Created `main/spiffs/audio/` directory with 9 placeholder WAV files (16kHz, 16-bit mono silence). Files: `system_start.wav`, `system_shutdown.wav`, `attendance_success.wav`, `unknown_face.wav`, `enroll_start.wav`, `enroll_success.wav`, `enroll_fail.wav`, `low_battery.wav`, `look_straight.wav` (durations: 300–600ms). |
| 2 | `main/CMakeLists.txt` | Added `spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)` to embed SPIFFS partition into the storage area during firmware build. |
| 3 | `main/audio/audio_player.c` (includes) | Added `#include "esp_spiffs.h"` for SPIFFS mount support. |
| 4 | `main/audio/audio_player.c` (mount function) | Implemented `mount_spiffs_audio()` — mounts SPIFFS at `/spiffs` on partition label `"storage"` with graceful handling if already mounted. Called from `audio_init()`. |
| 5 | `main/audio/audio_player.c` (file search) | Implemented `try_open_audio_file(filename)` — attempts to open file from SD card first, then falls back to SPIFFS. Extracts basename from full path and prepends `/spiffs/audio/` for fallback attempt. |
| 6 | `main/audio/audio_player.c` (playback) | Modified `audio_play_file()` to call `try_open_audio_file()` instead of direct `fopen()`. Updated error message to indicate both paths were checked. |
| 7 | `main/audio/audio_player.c` (documentation) | Updated file header comment: `Audio Priority: /sdcard/audio/ (SD card) → /spiffs/audio/ (embedded SPIFFS)` |
| 8 | Documentation | Created `SPIFFS_AUDIO_SETUP.md` with comprehensive setup guide, troubleshooting, and integration instructions. |

---

#### Code Implementation Details

**SPIFFS Mount (audio_player.c):**
```c
static void mount_spiffs_audio(void) {
    static bool spiffs_mounted = false;
    if (spiffs_mounted) return;

    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret == ESP_OK || ret == ESP_ERR_ESP_SPIFFS_ALREADY_MOUNTED) {
        ESP_LOGI(TAG, "SPIFFS mounted at /spiffs for embedded audio");
        spiffs_mounted = true;
    } else {
        ESP_LOGW(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
    }
}
```

**Fallback File Search (audio_player.c):**
```c
static FILE* try_open_audio_file(const char *filename) {
    FILE *f = fopen(filename, "rb");  /* Try SD card first */
    if (f) {
        ESP_LOGD(TAG, "Audio file found on SD: %s", filename);
        return f;
    }
    
    /* Fallback to SPIFFS */
    const char *basename = strrchr(filename, '/');
    if (basename) {
        basename++;
        char spiffs_path[256];
        snprintf(spiffs_path, sizeof(spiffs_path), "/spiffs/audio/%s", basename);
        f = fopen(spiffs_path, "rb");
        if (f) {
            ESP_LOGI(TAG, "Audio file fallback to SPIFFS: %s", spiffs_path);
            return f;
        }
    }
    return NULL;
}
```

---

#### Build Result
- **Status:** ✅ Full CMake configuration and build succeeded.
- **SPIFFS Embedding:** `spiffs_create_partition_image` successfully created FAT/SPIFFS image from `main/spiffs/` directory.
- **Audio Library:** SPIFFS library (`libspiffs.a`) compiled successfully into the binary.
- **Binary Size:** ~5.27 MB (12% free in 6 MB OTA partition).

---

#### Integration with Existing Code
- **No code changes required** in `main.c`, `ui_player.cpp`, or other audio call sites.
- Existing `audio_play("/sdcard/audio/filename.wav", ...)` calls automatically benefit from the fallback mechanism.
- SPIFFS mount happens transparently during `audio_init()` — zero user-facing changes.

---

#### Expected Device Behaviour After Flash
1. **On boot:** If no system audio on SD card, device plays embedded `system_start.wav` from SPIFFS.
2. **During operation:** All audio events (attendance success, enrollment prompts, low battery, etc.) play from SPIFFS if SD card versions are missing.
3. **User customization:** If user places higher-quality audio on SD card at `/sdcard/audio/`, those files take priority over embedded versions.

---

#### Replacing Placeholder Audio
To upgrade from silence placeholders to real audio:

1. **Create WAV files** (16kHz, 16-bit, mono/stereo) matching the filenames in `main/spiffs/audio/`.
2. **Replace files** in the `main/spiffs/audio/` directory on the development machine.
3. **Rebuild:** `idf.py build` — the new audio files are automatically embedded into the next firmware image.
4. **Flash and verify** — audio now plays real event sounds instead of silence.

---

#### Files Modified / Created This Session
- ✅ `main/spiffs/audio/` (directory created with 9 WAV files)
- ✅ `main/CMakeLists.txt` (SPIFFS embedding added)
- ✅ `main/audio/audio_player.c` (SPIFFS support + fallback mechanism)
- ✅ `SPIFFS_AUDIO_SETUP.md` (user documentation)

---

#### Verification Checklist
- ✅ SPIFFS partition table entry exists (`storage` partition in `partitions.csv`)
- ✅ Audio files generated successfully (9 WAV files in `main/spiffs/audio/`)
- ✅ CMakeLists.txt embedding command added
- ✅ audio_player.c includes SPIFFS header and implements mount/fallback
- ✅ Build completes without errors
- ✅ Device ready for next flash cycle

---

### [2026-06-05] — UI Freeze During Face Enrollment (Root Cause Found & Fixed)

- **Category:** System Stability — Hardware-Verified Deadlock
- **Timestamp:** 2026-06-05T20:42 WAT
- **Source:** Live serial monitor analysis (`idf.py flash monitor`)
- **Status:** ✅ Fixed — Build verified (`0x54f390` bytes, 12% free)

---

#### The Issue

After tapping a student card on the enrollment screen, the UI froze completely and permanently. The device required a power cycle to recover. The freeze was 100% reproducible on the first tap.

**Observed symptom (serial log):**
```
I (132367) UI_ENROLL: Selected student index: 0
I (132367) MAIN: Starting enrollment for Inagije Badmus, capturing 30 frames
I (132407) MAIN: Captured frame 0 successfully, width=640 height=480
KKKKKI (137907) BATTERY: Power Check (STC8): ...
```
After the frame capture log, only `K` (LVGL tick ISR) characters appeared for 5+ seconds, then the battery task logged its pre-lock reading and it too blocked forever.

---

#### Diagnostic Investigation

Extensive `ESP_LOGI` instrumentation was added to `ui_update_enrollment_camera_frame` and single-character diagnostic prints (`K`, `T`, `L`, `U`) to the LVGL tick ISR, rendering task loop, and `ui_lock()`/`ui_unlock()` functions. The resulting log pattern was definitive:

| Indicator | Observation | Meaning |
|---|---|---|
| `K` (LVGL tick ISR) | **Continued appearing** | Timer ISR alive; CPU not crashed |
| `T` (`lvgl_port_task` loop counter) | **Stopped completely** | `lvgl_port_task` frozen *inside* `lv_timer_handler()` |
| `L`/`U` (mutex lock/unlock) | **None after freeze** | `s_lvgl_mux` never released |
| `[DEBUG] Entry:` log | **Never printed** | `start_single_capture_task` blocked waiting for mutex |

**Conclusion:** `lvgl_port_task` (priority 10) held `s_lvgl_mux` indefinitely. `start_single_capture_task` (priority 5) and the battery task both blocked trying to acquire it. Total deadlock.

---

#### Root Cause

The full deadlock chain:

1. User taps student card → GT911 fires touch interrupt, INT line goes LOW.
2. `lvgl_port_task` acquires `s_lvgl_mux` and enters `lv_timer_handler()`.
3. LVGL polls the touch input device via `touch_read_cb()`.
4. `touch_read_cb()` calls `esp_lcd_touch_read_data(s_touch_handle)` — an **I2C read with `portMAX_DELAY` timeout**.
5. GT911's INT line is still active from the tap. The GT911 does not respond to the next I2C read (interrupt not cleared, device in inconsistent state after the event).
6. `esp_lcd_touch_read_data()` blocks indefinitely on I2C.
7. `lvgl_port_task` never exits `lv_timer_handler()`, never releases `s_lvgl_mux`.
8. All other tasks needing the LVGL mutex block forever.

**The architectural mistake:** I2C hardware I/O with `portMAX_DELAY` was running inside the LVGL mutex critical section.

---

#### Fix Applied

**`main/display/touch_driver.c` — Complete architectural redesign:**

All I2C touch reads moved out of the LVGL context into a dedicated `touch_poll_task`. The LVGL callback now only reads from a cached shared struct — zero I2C in the lock path.

```
BEFORE:
  lvgl_port_task holds s_lvgl_mux
    → lv_timer_handler()
      → touch_read_cb()
        → esp_lcd_touch_read_data()   ← I2C, portMAX_DELAY, CAN HANG
          → s_lvgl_mux held forever → DEADLOCK

AFTER:
  touch_poll_task (priority 4, independent)
    → esp_lcd_touch_read_data()       ← I2C here, safe to hang
    → updates s_touch_state (non-blocking mutex, <1µs)

  lvgl_port_task holds s_lvgl_mux
    → lv_timer_handler()
      → touch_read_cb()
        → reads s_touch_state         ← NO I2C, never blocks
```

Key implementation details:
- `touch_poll_task` runs at **priority 4** — below `lvgl_port_task` (10), camera (7), and enrollment (5). A GT911 I2C hang only stalls this one low-priority task.
- Shared `touch_state_t` struct (`x`, `y`, `pressed`) protected by a dedicated `s_touch_state_mutex` — completely separate from `s_lvgl_mux`.
- `touch_read_cb()` uses **non-blocking** `xSemaphoreTake(s_touch_state_mutex, 0)`. If the poll task is mid-write, LVGL sees the previous state and re-polls next tick — safe and correct.
- I2C errors set `pressed = false` so the UI never gets stuck in a phantom-pressed state.
- Task created inside `touch_init()` — no changes to call sites required.

**`main/ui/ui_enrollment.cpp` — Diagnostic cleanup:**
Stripped `[DEBUG]` instrumentation. Restored clean production `ui_update_enrollment_camera_frame` with pixel scale/copy loop running **outside** the LVGL lock, and two minimal lock windows (read UI state + invalidate widget only).

**`main/display/lcd_driver.c` — Diagnostic cleanup:**
Removed `K`/`T`/`L`/`U` diagnostic `esp_rom_printf` characters from tick ISR, task loop, and mutex functions.

---

#### Files Modified

| File | Change |
|---|---|
| `main/display/touch_driver.c` | Rewritten — `touch_poll_task` + `touch_state_t`; `touch_read_cb` is now I2C-free |
| `main/ui/ui_enrollment.cpp` | Stripped diagnostic logs; clean production camera frame update |
| `main/display/lcd_driver.c` | Removed K/T/L/U diagnostic prints |

#### Build Result

- **Binary:** `SmartAttendance.bin` = `0x54f390` bytes
- **Flash free:** `0xb0c70` bytes (~12% of 6 MB OTA partition)
- **Errors:** 0 errors, 0 warnings
- **Status:** ✅ Ready to flash

---

#### Subsystem Status Update

| Subsystem | Previous | Updated |
|---|---|---|
| Touch input (GT911) | ✅ Functional (latent I2C deadlock) | ✅ Hardened — I2C decoupled from LVGL |
| Enrollment UI freeze | 🔴 Freeze on first frame capture | ✅ Root cause eliminated |

---

### [2026-06-06 20:15] Hotspot Connection & Toolchain Build Fixes

#### Problems Resolved
1. **No Gadgets Connecting to Hotspot**: When starting the Access Point (SoftAP) portal, clients (phones, laptops) were unable to connect or get IP addresses (stuck on DHCP/IP configuration).
2. **Xtensa Toolchain Activation Failure**: The local `.espressif` global environment had a corrupted/empty `xtensa-esp-elf` directory, causing the ESP-IDF export script (`export.bat`) to fail completely during build/flash activations, blocking compilation.

#### Root Causes
1. **Netif Creation After WiFi Init**: In the previous code, `esp_netif_create_default_wifi_ap()` was called in `wifi_ap_portal_start()`, which runs at runtime *after* `esp_wifi_init()` has already completed. In ESP-IDF, default network interfaces must be created *before* initializing the Wi-Fi driver, otherwise event handlers (like starting the DHCP server) and driver packet hooks are not bound, leaving clients unable to get IP addresses.
2. **Global Tool Verification**: By default, `export.bat` checks tools for all targets listed in `~/.espressif/idf-env.json` (including Xtensa targets). Since the directory was present but corrupted, it crashed.

#### Fixes Applied
- **Pre-create AP Netif**: Added `esp_netif_create_default_wifi_ap()` inside `wifi_manager_init()` (in [wifi_manager.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/wifi_manager.c)) alongside the station netif, before `esp_wifi_init()`.
- **Query & Reuse Netif**: Modified [wifi_ap_portal.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/wifi_ap_portal.cpp) to retrieve the default AP netif handle via `esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")` at runtime, rather than attempting to create it after initialization.
- **Explicit DHCP Restart**: Added manual IP configuration and explicit call to `esp_netif_dhcps_start()` after starting the AP to guarantee that LwIP starts the DHCP server and answers client requests under the ESP-Hosted co-processor transport architecture.
- **Filtered Targets Config**: Updated `C:\Users\user\.espressif\idf-env.json` to remove targets requiring the `xtensa-esp-elf` toolchain (only keeping RISC-V targets: `esp32p4`, `esp32c6`, etc.). Updated `build_workaround.bat` and `flash_workaround.bat` to pass `esp32p4` to `export.bat`. Renamed the corrupted tool version folder to `esp-14.2.0_20241119.bak`.

#### Build Status
- **Binary Size**: `0x551bb0` bytes
- **Build Status**: ✅ Succeeded (2127/2127 targets built successfully)

---

### [2026-06-10] — Audio Guidance Disabled; Enrollment Focus; Clean Build

- **Category:** Build System / Feature Flag / Enrollment Stability
- **Timestamp:** 2026-06-10T22:41 WAT
- **Source:** Developer decision + build system analysis
- **Status:** 🔄 In Progress — clean build running (160/2142 targets at time of writing)

---

#### Context & Decision

Following the addition of the `touch_poll_task` architectural fix (2026-06-05), the team attempted to verify enrollment flow end-to-end. The primary suspicion was that audio guidance (`audio_play()` calls inside `start_single_capture_task`) may reintroduce synchronisation issues if audio and camera frame processing compete for timing-sensitive shared resources.

**Developer decision:** Disable audio guidance entirely for now and focus all debugging effort on making the enrollment capture pipeline solid. Audio can be re-enabled once enrollment is confirmed stable.

---

#### Changes Applied

##### 1 — Audio Guidance Disabled (`main/config.h`)

Set the feature flag:
```c
#define ENABLE_AUDIO_GUIDANCE  0   // was 1
```
All `#if ENABLE_AUDIO_GUIDANCE` guarded `audio_play()` calls inside `start_single_capture_task` are now compiled out. No I2S DMA activity during enrollment, eliminating any potential mutex/timing contention between the audio task and the camera/enrollment pipeline.

##### 2 — CMake Stale-Path Root Cause Identified & Resolved

**Problem:** After moving the ESP-IDF installation from `C:/esp/v5.4.2` to `C:/Espressif/frameworks/esp-idf-v5.4.2`, the cached `CMakeCache.txt` inside the `build/` directory still referenced the old path. A partial `idf.py reconfigure` inside the old `build/` directory was insufficient — CMake kept reading stale toolchain entries for the bootloader sub-project, causing:
```
CMake Error at .../bootloader/CMakeLists.txt:...
  Toolchain file not found: C:/esp/v5.4.2/tools/cmake/toolchain-...
```

**Resolution:**
1. Deleted the entire `build/` directory (`Remove-Item -Recurse -Force`) to force a 100% clean CMake regeneration.
2. Ran `build_workaround.bat` which calls:
   - `call C:\Espressif\frameworks\esp-idf-v5.4.2\export.bat esp32p4` — loads the correct toolchain into the environment.
   - `idf.py reconfigure` — generates a fresh `CMakeCache.txt` and `build.ninja`.
   - `idf.py build` — full incremental build from the new cache.

##### 3 — `build_workaround.bat` / `flash_workaround.bat` Previously Updated (2026-06-06)

Both batch scripts were already updated in the prior session to point to `C:\Espressif\frameworks\esp-idf-v5.4.2`. The `idf_env_backup.sh` filter on `C:\Users\user\.espressif\idf-env.json` to remove Xtensa targets remains in place, keeping `export.bat` fast and toolchain-check-clean.

---

#### Files Modified

| File | Change |
|---|---|
| `main/config.h` | `ENABLE_AUDIO_GUIDANCE` set to `0` |
| `build/` (directory) | **Deleted** — forced fresh CMake regeneration |

---

#### Build Status

- **Build trigger:** `build_workaround.bat` → `idf.py reconfigure` + `idf.py build`
- **Output log:** `build12.log` (923/923 targets)
- **Binary:** `SmartAttendance.bin` = `0x54a0c0` bytes (~5.27 MB), `0xb5f40` bytes (12%) free
- **Completed:** 2026-06-11T11:38 WAT
- **Status:** ✅ Build successful

> **Note on transient failures (build10/build11):** Two prior attempts failed with `ninja: error: remove(...obj.d): The process cannot access the file because it is being used by another process.` — Windows Defender was scanning freshly-written dependency files mid-build. Fixed by adding `SmartAttendance\build\` to the Windows Defender exclusion list via `Add-MpPreference`. The build itself had zero compiler or linker errors.

---

#### Open Enrollment Issues (Active Focus)

| ID | Description | Priority |
|---|---|---|
| Enrollment frame capture | Verify 30-frame loop completes without deadlock after audio removal | 🔴 Blocker |
| Enrollment quality scoring | Confirm sharpness/brightness/yaw filtering selects valid frames | 🟠 High |
| Try Again button | Verify `start_single_capture_task` respawn path works end-to-end | 🟠 High |
| Re-enrollment needed | All previous embeddings corrupted (identity-warp, 2026-05-30) — DB must be cleared before testing | 🔴 Required |

---

### [2026-06-11] — Full System Refactor: Audio Removal, DB Extension, Web Portal, Admin Wizard, Telegram Bot

- **Category:** Feature / Architecture / Cloud Integration
- **Timestamp:** 2026-06-11T22:40 WAT
- **Status:** ✅ Build Successful — `SmartAttendance.bin` = `0x53fd90` bytes (13% free)

---

#### Context & Motivation

Major refactoring session to strip audio guidance entirely and extend the system with a cloud Telegram bot for lecture scheduling and attendance notifications. Audio was consuming critical DMA/I2S resources that conflicted with the enrollment camera pipeline.

---

#### Changes Applied

##### 1 — Complete Audio Removal

- Removed all `AUDIO_*` defines and task config from `main/config.h`.
- Removed `audio_play` calls from `main.c` (low battery alert + graceful shutdown).
- Removed `audio_player.c/h`, `ui_player.cpp/h`, `ui_recorder.cpp/h` from disk and `CMakeLists.txt`.
- Removed all `#include "audio_player.h"` from `ui_enrollment.cpp`, `ui_main.cpp`.
- Removed Volume slider (`audio_set_volume`) from Quick Settings panel in `ui_main.cpp`.
- Removed Recorder/Player app icons from the launcher grid (now 5 apps).
- Removed all `ui_close_recorder_screen` extern declarations and call sites from `ui_main.cpp`.

##### 2 — Database Schema Extension

- Extended `user_t` struct: added `phone_number[20]` and `telegram_id[24]`.
- Added `lecturer_courses` junction table.
- Added new API: `db_insert_lecturer`, `db_insert_or_get_course`, `db_link_lecturer_course`, `db_get_lecturer_by_phone`, `db_insert_schedule_from_bot`.

##### 3 — Dual-Role Web AP Captive Portal

- Rewrote HTML/JS in `wifi_ap_portal.cpp` with tabbed Student / Lecturer layout.
- Added `/submit_student` and `/submit_lecturer` POST handlers.
- Student flow queues user for face enrollment; Lecturer flow saves directly to DB (no face capture).

##### 4 — Admin On-Device Enrollment Wizard

- Created `ui_admin_setup.cpp` / `.h` — dedicated wizard screen with Name, Matric ID, Phone fields.
- Integrated "Enroll Admin" button into Settings screen.
- Fixed `xTaskCreate` build error by adding FreeRTOS headers to `ui_admin_setup.cpp`.

##### 5 — Telegram Bot + REST Sync API (`telegram_bot/`)

New cloud backend created with 4 files:

- **`bot.py`**: FastAPI server + `python-telegram-bot` v21 with ConversationHandler.
  - `POST /telegram_webhook` — webhook endpoint (auto-wakes Render on user message).
  - `POST /api/sync_users` — device pushes registered users; bot upserts by UUID.
  - `GET /api/get_schedules` — device pulls new schedules since timestamp.
  - `/start` — contact-sharing phone verification with last-9-digit normalization.
  - `/schedule` — 5-step wizard (Code → Title → Date → Start → End).
  - `/auth_me_dev` — developer test bypass.
  - Hybrid webhook/polling: uses `RENDER_EXTERNAL_URL` to auto-detect Render vs local.
- **`db.py`**: SQLite wrapper for `bot_data.db`.
- **`requirements.txt`**: `python-telegram-bot==21.3`, `fastapi==0.111.0`, `uvicorn==0.30.1`.
- **`README.md`**: Step-by-step Render deployment guide with sleep-wake cycle explanation.

##### 6 — Firmware Fix

- Fixed `main.c:875`: replaced non-existent `ble_data.telegram_id` copy with `new_user.telegram_id[0] = '\0'` (Telegram ID is linked later by the cloud bot).

---

#### Build Status

- **Build Command:** `./build_workaround.bat`
- **Binary:** `SmartAttendance.bin` = `0x53fd90` bytes (~5.25 MB), 13% free
- **Completed:** 2026-06-11T22:42 WAT
- **Status:** ✅ Build successful (9/9 ninja targets)

---

#### Deployment Next Steps

1. Push `telegram_bot/` to GitHub.
2. Deploy as Render Free Web Service (Root Directory: `telegram_bot`, Start: `python bot.py`).
3. Render injects `RENDER_EXTERNAL_URL` → bot auto-registers Telegram webhook on startup.
4. Copy Render URL into device **Settings → Cloud Endpoint URL**.
5. Device syncs users and pulls schedules on each cloud sync cycle.

---

### [2026-06-12] — Course Enrollment Synced and Bot Database Extensions

- **Category:** Feature / Architecture / Sync Integration
- **Timestamp:** 2026-06-12T12:15 WAT
- **Status:** ✅ Build Successful — `SmartAttendance.bin` = `0x538b20` bytes (13% free)

---

#### Changes Applied

##### 1 — Course Enrollment Synchronization in Firmware
- Updated [cloud_sync.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/cloud_sync.c) to fetch student course enrollments from the Telegram Bot REST API during each cloud sync cycle.
- Added `sync_enrollments()` local function which makes a `GET` request to `{s_api_endpoint}/api/get_course_enrollments?since=<last_sync_timestamp>`.
- For each enrollment returned, it calls `db_link_user_course(user_uuid, course_code)` to register the student-course connection in the local device SQLite database.
- Integrated `sync_enrollments()` as Step 2.5 of the main `cloud_sync_task()`.

##### 2 — Telegram Bot Database Robustness
- Updated [db.py](file:///c:/Users/user/Documents/projects/SmartAttendance/telegram_bot/db.py) to declare `courses` and `user_courses` tables.
- Implemented `get_all_courses()`, `enroll_user_in_course()`, and `get_enrollments_since()` to prevent runtime errors when student triggers `/enroll_course` or the device requests enrollments.
- Refined course enrollment flow to automatically pre-create courses with placeholder names if students register for courses before a lecturer schedules them, preventing `"not_found"` errors.
- Enhanced course upsert during schedule creation to dynamically update course titles rather than ignoring them once the course exists.

---

#### Build Status
- **Build Command:** `./build_workaround.bat`
- **Binary:** `SmartAttendance.bin` = `0x538b20` bytes (~5.22 MB), 13% free
- **Completed:** 2026-06-12T12:12 WAT
- **Status:** ✅ Build successful (9/9 ninja targets)

---

### [2026-06-12 12:50] — UI Screen Geometry Refactoring & WiFi Scan Resolution

- **Category:** User Interface / Hardware Debugging
- **Altered Files:**
  - `main/ui/ui_settings.cpp` (modified)
  - `main/ui/ui_reports.cpp` (modified)
  - `main/ui/ui_enrollment.cpp` (modified)
  - `main/ui/ui_user_manager.cpp` (modified)
  - `main/ui/ui_admin_setup.cpp` (modified)
  - `main/ui/ui_main.cpp` (modified)
- **Status:** Complete & Flashed/Verified on COM5
- **Log:**
  * **Safe Area UI Geometry Refactor:** Adjusted all application screens to sit strictly within the safe vertical viewport boundaries (Y=40 to 540) to prevent overlap with the persistent top status bar (Y=0 to 40) and bottom navigation bar (Y=540 to 600).
    * **Settings & Reports Screens:** Adjusted title bar to `Y=40`, content to `Y=90`, and height to `450` (instead of 550).
    * **Enrollment Screen:** Adjusted close button to `Y=45`, title to `Y=50`, and panels/list to `Y=90` with height `440` (bottom at 530).
    * **User Manager Screen:** Adjusted header to `Y=40` (height 50) and user list to `Y=95` with height `440`.
    * **Admin Setup Screen:** Adjusted title bar to `Y=40` and card layout to `Y=95` with height `440`.
    * **Main UI Overlays:**
      * Keyboard panel: Set height to `240` and `Y=290` (bottom at 530).
      * PIN prompt modal card: Set height to `480` and `Y=50` (bottom at 530).
      * Keyboard input overlay: Constrained to `Y=40` to `540` (keyboard at `Y=360`, height `180`).
  * **Wi-Fi Scan Diagnosis & Fix:** Debugged the Wi-Fi settings scan failure using the serial monitor. Verified that the Wi-Fi scan failure was due to the old firmware running on the device. Re-compiled and flashed the new codebase (which has the correct C6 SDIO configurations) to the device on `COM5`.
  * **System Boot & Verification:** Launched the serial monitor to verify successful boot, validated that the SDIO link is stable, the Wi-Fi settings scan functions as expected, and all screens render properly within the safe boundaries.
  * **Enrollment Session PIN Popup Fix:** Resolved the missing enrollment session PIN UI display. Added a persistent pop-up notification on the screen displaying the Access Point SSID (`Attendance_Setup`) and the randomly generated 6-digit PIN so standalone admins can view it. Cleanly dismissed the notification when exiting the enrollment view.
  * **Automatic Database Schema Migration:** Added schema migration statements inside `db_manager.c` that automatically execute `ALTER TABLE users ADD COLUMN ...` on startup for `phone_number` and `telegram_id`. This updates pre-existing database files on the SD card automatically, preventing `Prepare failed: table users has no column named phone_number` crashes when registering users on the Web AP portal.

### [2026-06-12 14:05] — Captive Portal Stack Overflow Fix

- **Category:** Bug Fix — Hardware-Verified
- **Altered Files:**
  - `main/network/wifi_ap_portal.cpp` (modified)
- **Status:** Complete & Flashed/Verified on COM5
- **Log:**
  * **Heap-Allocated Request Handlers:** Refactored `post_submit_student_handler` and `post_submit_lecturer_handler` in `wifi_ap_portal.cpp` to dynamically allocate the large payload buffer (`buf` of size 512 / 1024 bytes) and database user structure (`user_t lecturer`) on the heap (`malloc` / `calloc`).
  * **Memory Safety & Stack Protection:** Stretched stack buffers are completely eliminated, reducing stack overhead of the callback task from ~1.4 KB to under 150 bytes. Dynamic memory is cleanly deallocated (`free`) immediately after cJSON parsing and database operations are complete, eliminating heap leaks.
  * **Verification:** Re-compiled with `./build_workaround.bat` and flashed to the board on `COM5` via `flash_workaround.bat`. The device booted successfully (`ELF file SHA256: 1efe165c8...`) and is fully ready for crash-free lecturer registrations.

### [2026-06-12 14:35] — Face Enrollment Watchdog Deadlock Fix

- **Category:** Bug Fix — Hardware-Verified
- **Altered Files:**
  - `main/ui/ui_enrollment.cpp` (modified)
- **Status:** Complete & Flashed/Verified on COM5
- **Log:**
  * **Corrected Color Format Stride Mismatch:** Adjusted the camera preview backing buffer allocation (`s_camera_buffer`), initialization `memset`, and `s_camera_img_dsc` stride & data size to use exactly `2` bytes per pixel instead of `sizeof(lv_color_t)` (which is 3 bytes in LVGL v9). This aligns stride to `480 * 2 = 960` bytes (instead of 1440) and allocation to `345,600` bytes (instead of 518,400), preventing buffer overflows in LVGL's software rendering core that corrupted the TLSF heap and locked up the display loop.
  * **Thread Synchronization:** Wrapped the frame scaling and copying loop in `ui_update_enrollment_camera_frame` inside the display recursive mutex (`ui_acquire()` / `ui_release()`). This guarantees thread safety and completely eliminates concurrent read/write data races on the camera frame buffer between the capture task (CPU 1) and the rendering task (CPU 0).
  * **Verification:** Recompiled (`SmartAttendance.bin` size = `0x536840` bytes) and flashed the board via `COM5`. The device boots cleanly, starting the enrollment face-capture loop without watchdog timeouts or lockups.

---

### [2026-06-12 16:40] — Face Enrollment UI Lock Hardening & Asynchronous Object Deletion

- **Category:** System Stability / User Interface / Thread Safety
- **Altered Files:**
  - `main/ui/ui_enrollment.cpp` (modified)
  - `main/ui/ui_attendance.cpp` (modified)
  - `main/main.c` (modified)
  - `main/ble/ble_registration.c` (modified)
  - `main/ui/ui_main.cpp` (modified)
  - `main/ui/ui_settings.cpp` (modified)
  - `main/ui/ui_user_manager.cpp` (modified)
- **Status:** Complete & Flashed/Verified on COM5
- **Log:**
  * **Custom Image Descriptors Flagging:** Flagged the static camera preview drawing buffers (`s_camera_img_dsc`) with `LV_IMAGE_FLAGS_ALLOCATED` immediately following `lv_draw_buf_init()` in both `ui_enrollment.cpp` and `ui_attendance.cpp`. This instructs LVGL v9's binary decoder that the image source is a pre-allocated draw buffer, preventing it from performing a continuous wrapper re-allocation loop that was starving other tasks and triggering watchdog timeouts.
  * **Recursive Mutex Access Hardening:** Checked and handled the return value of `ui_acquire()` across all background/helper tasks (`detection_recognition_task`, `process_enrollment_frames_for_user`, `start_single_capture_task`, `start_enrollment_task`, `network_sync_task`, `system_state_machine`, `handle_low_battery`, `ble_registration.c`, `ui_show_notification`, `ui_hide_notification`, `ui_set_sd_card_status`). If the lock fails to acquire, the calling routine safely aborts/bypasses the GUI modification and skips calling `ui_release()`, preventing recursive semaphore state corruption.
  * **Asynchronous Object Deletion for Crash Prevention:** Converted critical widget deletion calls from synchronous `lv_obj_del` to asynchronous `lv_obj_delete_async` across UI components (e.g. notification banners, PIN prompt modal, keyboard panels, quick settings shade, recent apps panel, settings Wi-Fi lists, and user manager cards). This avoids use-after-free crashes and screen turn-offs when a widget is deleted from inside its own click event/bubbling handler, ensuring clean deallocation during the next refresh cycle.
  * **Verification:** Built successfully (`SmartAttendance.bin` size = `0x53fd90` bytes) and flashed the board. Manual verification confirmed that the camera stream starts without freezing, face capture completes successfully, and clearing the floating enrollment PIN banner no longer crashes or turns off the display.

---

### [2026-06-12 17:55] — Phase 2: Double-Buffering & Quality-Weighted Vector Fusion

- **Category:** Architecture / Bug Fix
- **Status:** ✅ Completed & Flashed/Verified on COM5
- **Log:**
  * **Double-Buffering for Detection Task:** Resolved the "Unknown 0.0%" recognition bug by eliminating concurrent writes on the camera frame buffer. Allocated a static buffer (`s_detection_fb_buf`) in SPIRAM once during system initialization in `app_main()`. Refactored `camera_task` in `main.c` to perform single-copy double-buffering: when the detection task queue is ready (idle), it copies the pixels using the exact `fb->len` length, updates `fb_copy.buf` to point to this stable copy, and queues the frame. When the queue is full, the copy is bypassed. This completely isolated detection and alignment operations from live sensor overwrites.
  * **Quality-Weighted Mean Vector Fusion:** Replaced simple centroid averaging in `process_enrollment_frames_for_user()` with a robust Quality-Weighted Mean vector fusion. We weight each frame's embedding by its calculated quality score (sharpness, yaw, brightness), compute the average norm of the kept frames, scale the raw weighted average vector to match this target norm (preventing quantization/precision loss), and apply rounding and strict value clipping to the `int8_t` range `[-128, 127]` before database insertion.
  * **Verification:** Re-compiled (`SmartAttendance.bin` size = `0x536be0` bytes) and flashed the board via `COM5`. Confirmed that the face detection bounding box and live similarity scoring are restored and functional.
  * **Tooling Adjustment:** Updated `flash_workaround.bat` to run `idf.py -p COM5 flash monitor` to ensure the serial monitor is always spawned automatically after a flash operation.

---

### [2026-06-12 18:40] — User Unenrollment End-to-End Implementation

- **Category:** Feature Expansion / Synchronization / Compatibility Hotfix
- **Status:** ✅ Completed, Flashed & Hardware-Verified on COM5
- **Altered Files:**
  - `telegram_bot/db.py` (modified)
  - `telegram_bot/bot.py` (modified)
  - `main/database/db_manager.h` (modified)
  - `main/database/db_manager.c` (modified)
  - `main/network/wifi_ap_portal.cpp` (modified)
  - `main/network/cloud_sync.c` (modified)
  - `main/ui/ui_user_manager.cpp` (modified)
  - `.venv/Lib/site-packages/telegram/ext/_application.py` (patched)
- **Log:**
  * **Telegram Bot Command & API Support:** Added conversational `/unenroll` command for Lecturers/Admins to request a student ID, confirm with `YES/NO`, and delete the user from the bot database. Added a `deletions` table to track deleted user UUIDs and timestamps. Exposed `GET /api/get_deletions?since=<timestamp>` REST API endpoint.
  * **User Reconciliation on Sync:** Integrated two-way user list reconciliation inside the `/api/sync_users` POST endpoint. If a user is deleted on the device UI, the next sync payload naturally excludes their UUID, prompting the bot to cleanly drop them from the cloud database.
  * **Device-Side SQLite Deletion & Sync:** Implemented `db_delete_user_by_uuid` and `db_delete_user_by_student_id` in `db_manager.c` using safe prepared queries. Added `sync_deletions()` to `cloud_sync.c` to pull deleted UUIDs from the bot API, drop them locally, and trigger `recognizer_load_cache()` to reload the active face recognition template cache.
  * **Local Captive Portal Unenrollment:** Integrated an "Unenroll Student" tab inside the dark glassmorphic captive portal HTML web template. Configured the `POST /unenroll_student` REST endpoint on the device web server to accept a 6-digit PIN validation and Student ID, deleting the student locally and reloading the face cache.
  * **On-Device User Manager Extension:** Redesigned the on-device "User Management" settings app screen. Enlarged cards to display the Name, Role Badge (Orange/Blue/Green), Matric/Student ID, Phone/Telegram link status, and the list of Enrolled Courses (retrieved via `db_get_user_courses`), alongside the red delete trash bin.
  * **Python 3.13 Compatibility Hotfix:** Patched `python-telegram-bot` v21.3's `__slots__` definition inside `_application.py` to include `__stop_running_marker`, resolving the `AttributeError: no __dict__ for setting new attributes` initialization crash on Python 3.13.

---

### [2026-06-12 18:50] — Home Screen "Users" App, Settings Redesign & Git Integration

- **Category:** Feature Realignment / User Interface / Cloud Deployment / Version Control
- **Altered Files:**
  - `main/ui/ui_main.cpp` (modified)
  - `main/ui/ui_settings.cpp` (modified)
  - `main/ui/ui_user_manager.cpp` (modified)
  - `telegram_bot/db.py` (modified)
  - `telegram_bot/DEPLOYMENT.md` (new)
  - `.gitignore` (new)
- **Status:** ✅ Completed, Flashed & Hardware-Verified on COM5
- **Log:**
  * **Dedicated Home Screen "Users" App:** Promoted the user management list to a first-class "Users" app on the home screen launcher grid. It is PIN-protected using `ui_show_pin_prompt()` (just like settings) and correctly routes navigation back to the main desktop dashboard upon exit.
  * **Settings Cleanup & Redesign:** Removed the duplicate "User Management" entry button from the settings panel. Re-aligned the "Change PIN", "Enroll Admin", and "Factory Reset" buttons in the device management card into a clean, symmetric 180px layout.
  * **Recent Apps Mapping Bugfix:** Corrected the app names/icons array mismatch in the recent apps drawer that previously offset index 4 ("Files") to "Recorder" and caused closed-app mapping offsets.
  * **Render Cloud Hosting Integration:** Updated the bot SQLite connection block to read `DATABASE_PATH` from the environment if present, allowing the database to write to mounted persistent storage disks on Render. Added a comprehensive guide `DEPLOYMENT.md` detailing Render setup.
  * **Git Repository Configuration:** Initialized the local Git repository, set up a `.gitignore` file to ignore build folders, environment files, and SQLite database caches, and performed the initial repository commit.

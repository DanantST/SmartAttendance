# SmartAttendance Improvement & Perfection Plan

## Goal

Transform the firmware from **"compiles and links"** to **"production-ready, field-deployable"** on the Elecrow CrowPanel Advanced 7″ ESP32-P4.

---

## Phase 1 — Code Hygiene *(~2 hours)*

Clean every compiler warning so the build log is pristine. Zero warnings = zero surprises.

### Component: Warning Cleanup

#### [MODIFY] [cloud_sync.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/cloud_sync.c)
- Use or remove [logs](file:///c:/Users/user/Documents/projects/SmartAttendance/main/database/db_manager.c#153-172) (line 239) and `entry` (line 319)

#### [MODIFY] [wifi_manager.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/wifi_manager.c)
- Wrap `TAG` (line 19) inside `#if SOC_WIFI_SUPPORTED` to suppress unused-variable

#### [MODIFY] [ov5640_autofocus.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/camera/ov5640_autofocus.c)
- Move `TAG` inside the `#if !defined(CONFIG_IDF_TARGET_ESP32P4)` block

#### [MODIFY] [ui_main.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_main.cpp)
- Fix `lv_anim_exec_xcb_t` cast (line 520) via a tiny wrapper function
- Remove or use [nav_button_handler](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_main.cpp#855-882) (line 855)

#### [MODIFY] [ui_components.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_components.cpp)
- Use or `__attribute__((unused))` for `TAG` (line 11)

#### [MODIFY] [ui_reports.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_reports.cpp)
- Use or suppress `TAG` and `selected`

#### [MODIFY] [main.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/main.c)
- Wire [start_enrollment()](file:///c:/Users/user/Documents/projects/SmartAttendance/main/main.c#405-440) into the state machine or remove the `static` unused function

#### [MODIFY] [face_detector.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/detection/face_detector.cpp)
- Use or remove `cy` (line 68)

#### [MODIFY] [battery_monitor.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/power/battery_monitor.c)
- Remove `#include "driver/adc.h"` (deprecated), keep only `esp_adc/adc_oneshot.h`

### Component: Config Cleanup

#### [MODIFY] [config.h](file:///c:/Users/user/Documents/projects/SmartAttendance/main/config.h)

#### [NEW] [Kconfig.projbuild](file:///c:/Users/user/Documents/projects/SmartAttendance/main/Kconfig.projbuild)
- Move the dummy `CONFIG_BSP_LCD_*` and `CONFIG_DL_*` defines out of [config.h](file:///c:/Users/user/Documents/projects/SmartAttendance/main/config.h) into proper Kconfig menus with real defaults

---

## Phase 2 — Real Persistence *(~1–2 days)*

Replace the no-op `stub_sqlite` with a real SQLite3 port running on the SD card.

### Component: Database

#### [DELETE] [stub_sqlite](file:///c:/Users/user/Documents/projects/SmartAttendance/components/stub_sqlite)
- Remove the entire `components/stub_sqlite` directory

#### [NEW] SD card mount utility
- Create `main/storage/sdcard_mount.c` / `.h`
- Use the SDMMC pins from `elecrow_p4_board.h` (`SDMMC_CLK`, `SDMMC_CMD`, `SDMMC_D0-D3`)
- Mount FAT filesystem at `/sdcard`
- Format on first boot if needed

#### Add real SQLite3
- Add `nickhikollmayer/esp-idf-sqlite3` or Espressif's `sqlite3_port` to `idf_component.yml`
- Database path: `/sdcard/attendance.db`

#### [MODIFY] [db_manager.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/database/db_manager.c)
- Update `db_manager_init()` to mount SD card first, then open SQLite DB
- Implement `db_get_current_schedule_id()` with a real query against the `schedule` table using current time

#### [MODIFY] [CMakeLists.txt](file:///c:/Users/user/Documents/projects/SmartAttendance/main/CMakeLists.txt)
- Replace `stub_sqlite` with real `sqlite3` in `REQUIRES`

---

## Phase 3 — Display & Touch *(~1–2 days)*

Make the 7″ screen and touch panel actually work.

### Component: LCD Driver

#### [NEW] `main/display/lcd_driver.c` / `.h`
- Initialize the ESP32-P4 RGB LCD controller (1024×600, RGB565)
- Allocate frame buffer in PSRAM
- Implement the LVGL `flush_cb` that copies the draw buffer to the LCD frame buffer
- Handle VSYNC for tear-free rendering

#### [MODIFY] [ui_main.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_main.cpp)
- Replace `lv_display_set_flush_cb(disp, NULL)` with `lcd_flush_cb`
- Increase draw buffer height from 20 lines to ≥40 for smoother rendering

### Component: Touch Driver

#### [NEW] `main/display/touch_driver.c` / `.h`
- Initialize I2C for GT911 (addr `0x5D` / `0x14`) on `TOUCH_I2C_SDA` / `TOUCH_I2C_SCL`
- Implement LVGL `read_cb` that returns touch points
- Handle reset pin (`TOUCH_RESET_PIN`) and interrupt pin (`TOUCH_INT_PIN`)

#### [MODIFY] [ui_main.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_main.cpp)
- Replace `lv_indev_set_read_cb(indev, NULL)` with `touch_read_cb`

### Component: Backlight

#### [NEW] `main/display/backlight.c`
- PWM control via `LCD_BACKLIGHT_PIN` using LEDC
- Brightness curve, auto-dim after timeout

---

## Phase 4 — Wi-Fi & BLE via esp_hosted *(~2–3 days)*

The ESP32-P4 has **no native radio**. The Elecrow board pairs it with an ESP32-C6 over SDIO for Wi-Fi + BLE.

### Component: esp_hosted Integration

#### Add dependency
- Add `espressif/esp_hosted` to `idf_component.yml`
- Configure the SDIO transport in `sdkconfig`:
  ```
  CONFIG_ESP_HOST_INTERFACE_SDIO=y
  CONFIG_ESP_HOSTED_ENABLED=y
  ```

#### [MODIFY] [wifi_manager.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/wifi_manager.c)
- Remove the `#if SOC_WIFI_SUPPORTED` / `#else` guard pattern
- Replace with `esp_hosted`-aware initialization that routes Wi-Fi calls through the C6
- The hosted driver exposes the same `esp_wifi_*` API once configured, so most logic stays

#### [MODIFY] [ble_registration.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ble/ble_registration.c)
- Remove the `#if SOC_BLE_SUPPORTED` / `#else` guard pattern
- NimBLE works transparently once the hosted transport is active
- Verify GATT service registration and BLE advertising

### Component: Cloud Sync Activation

#### [MODIFY] [cloud_sync.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/cloud_sync.c)
- Enable TLS cert pinning (embed the server's CA cert using `EMBED_TXTFILES` in CMake)
- Wire `sync_attendance_logs()` to actually query the DB and POST records
- Parse schedule response into the `schedule` table

---

## Phase 5 — Feature Completion *(~2–3 days)*

### Component: Face Quality Metrics

#### [MODIFY] [face_detector.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/detection/face_detector.cpp)

**Sharpness (Laplacian variance):**
```cpp
float face_detector_compute_sharpness(camera_fb_t *fb, detected_face_t *face) {
    // Extract face ROI from fb->buf
    // Apply 3×3 Laplacian kernel: [0,1,0; 1,-4,1; 0,1,0]
    // Return variance of result — higher = sharper
}
```

**Brightness (average luma):**
```cpp
float face_detector_compute_brightness(camera_fb_t *fb, detected_face_t *face) {
    // For RGB565: convert each pixel to Y = 0.299R + 0.587G + 0.114B
    // Return average across face ROI
}
```

### Component: Reports

#### [MODIFY] [ui_reports.cpp](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_reports.cpp)
- Wire the generate button to `db_get_attendance_report()` — query the real DB
- Format the CSV from actual `attendance` + `users` table joins
- Add date range filtering from the dropdown

### Component: Camera Tuning

#### [MODIFY] [camera_driver.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/camera/camera_driver.c)
- Increase CSI resolution from 320×240 to at least 640×480 (QVGA→VGA) for better face recognition
- Implement `camera_set_framesize()` to dynamically reconfigure the CSI controller
- Add ISP (Image Signal Processor) pipeline for RAW→RGB conversion using `esp_driver_isp`

### Component: Enrollment Flow

#### [MODIFY] [main.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/main.c)
- Wire `start_enrollment()` into the state machine when the system receives a BLE enrollment trigger or UI button press
- Capture multiple frames at different angles for robust embedding
- Store averaged embedding vector in the DB

---

## Phase 6 — Hardening & Production Polish *(~2–3 days)*

### Component: OTA Updates

#### [MODIFY] [partitions.csv](file:///c:/Users/user/Documents/projects/SmartAttendance/partitions.csv)
```csv
# Name,   Type, SubType,  Offset,  Size
nvs,      data, nvs,      ,        0x4000,
otadata,  data, ota,      ,        0x2000,
phy_init, data, phy,      ,        0x1000,
ota_0,    app,  ota_0,    ,        0x400000,
ota_1,    app,  ota_1,    ,        0x400000,
storage,  data, fat,      ,        0x3F1000,
```

#### [NEW] `main/network/ota_updater.c` / `.h`
- Implement HTTPS OTA using `esp_https_ota`
- Validate firmware signature before applying
- Rollback on boot failure

### Component: Security

#### [MODIFY] [wifi_manager.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/wifi_manager.c)
- Enable NVS encryption for stored Wi-Fi credentials

#### [MODIFY] [cloud_sync.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/cloud_sync.c)
- Embed server CA certificate, enforce TLS 1.2+
- Add API token rotation support

#### [MODIFY] [ble_registration.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ble/ble_registration.c)
- Implement numeric comparison pairing (MITM protection)
- Add bonding with encrypted storage

### Component: Power Management

#### [MODIFY] [battery_monitor.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/power/battery_monitor.c)
- Implement sleep policy: dim backlight after 30s inactivity, light-sleep after 60s
- Wake on touch interrupt or camera motion
- Auto-shutdown below 5% battery

### Component: Watchdog & Crash Recovery

#### [MODIFY] [main.c](file:///c:/Users/user/Documents/projects/SmartAttendance/main/main.c)
- Enable task watchdog on critical tasks (camera_task, detection_task)
- Configure `espcoredump` to flash for post-mortem analysis
- Add heartbeat LED/log for liveness monitoring

### Component: Logging & Telemetry

#### [NEW] `main/utils/crash_logger.c`
- Store last N crash dumps to SD card
- On next boot, optionally upload to cloud

---

## Verification Plan

### Automated — Build Verification
```bash
# Clean build must complete with ZERO warnings
idf.py fullclean && idf.py build 2>&1 | grep -i "warning"
# Expected: no output
```

### Automated — Flash Size Check
```bash
# Binary must fit in ota_0 partition (4MB)
idf.py size-components
# Check that .bin < 0x400000
```

### Manual — Phase 2 (Database)
1. Flash firmware to the device with an SD card inserted
2. Open serial monitor (`idf.py monitor`)
3. Verify log: `"Database initialized, N users loaded"`
4. Enroll a test user via BLE or UI
5. Power cycle and verify the user persists

### Manual — Phase 3 (Display)
1. Flash firmware
2. The 7″ LCD should show the main attendance screen (not blank)
3. Touch the screen — UI should respond to taps (navigation between screens)
4. Verify backlight dims after inactivity timeout

### Manual — Phase 4 (Wi-Fi/BLE)
1. Flash firmware with esp_hosted and the C6 co-processor connected
2. Monitor logs for `"Wi-Fi connected"` after configuring credentials
3. Trigger cloud sync and verify attendance records arrive at the backend
4. From a phone, scan BLE and connect to the enrollment GATT service
5. Send a name + student ID and verify enrollment succeeds

### Manual — Phase 5 (Features)
1. Aim the camera at a face — verify sharpness/brightness values are non-constant in logs
2. Open the Reports screen, tap Generate — verify real data appears (not dummy CSV)
3. Verify camera captures at VGA resolution (check `fb->width` in log)

### Manual — Phase 6 (Hardening)
1. Trigger an OTA update from the server — verify firmware updates and device reboots
2. Intentionally crash a task — verify core dump is saved and device recovers
3. Drain battery — verify auto-shutdown at <5%

> [!IMPORTANT]
> Each phase should be built and tested independently before proceeding to the next. Phases 1–3 are prerequisites for Phases 4–6.

---

## Summary Timeline

| Phase | Description | Effort | Dependencies |
|-------|-------------|--------|--------------|
| 1 | Warning cleanup + Kconfig | ~2 hrs | None |
| 2 | Real SQLite on SD card | 1–2 days | Phase 1 |
| 3 | LCD + Touch drivers | 1–2 days | Phase 1 |
| 4 | esp_hosted Wi-Fi/BLE | 2–3 days | Phase 3 |
| 5 | Face quality, reports, camera | 2–3 days | Phase 2 + 3 |
| 6 | OTA, security, power, watchdog | 2–3 days | Phase 4 + 5 |
| **Total** | | **~10–15 days** | |

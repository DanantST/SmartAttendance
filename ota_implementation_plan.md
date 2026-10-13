# Safe SD-Card Buffered OTA & Multi-Version Firmware Rollback — Implementation Plan

## Goal
Implement a fail-safe firmware update and version-management system for the CrowPanel Advanced 7" ESP32-P4. The system will:
1. **Buffer remote OTA updates to the SD card first** (`/sdcard/firmware/`), isolating network volatility from critical flash operations.
2. **Support rollback to ANY previously downloaded firmware version** archived on the SD card.
3. **Provide a dedicated "Firmware & Updates" UI screen** on the 7" display showing current firmware status and archived builds with one-tap "Install / Rollback" buttons.
4. **Leverage ESP-IDF Bootloader Auto-Rollback** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`) so that if any newly installed firmware crashes or panics on boot, the device automatically reverts to the previous working slot without needing a USB cable.

---

## Architecture & Data Flow

```text
 ┌────────────────────────────────────────────────────────┐
 │ 1. Remote Download Phase (Safe, Zero Flash Risk)       │
 └────────────────────────────────────────────────────────┘
   Cloud Check ──► "Update vX.Y.Z Available. Download to SD?"
                         │ (User taps Download)
                         ▼
   Streams over Wi-Fi ──► Saves to /sdcard/firmware/update_<ver>.bin.tmp
                         │ (Progress bar shown on screen)
                         ▼
   Network dropped? ────► Delete .tmp; Running flash untouched!
   Download complete? ──► Verify image header & rename to .bin
                         │
 ┌───────────────────────┴────────────────────────────────┐
 │ 2. Local Flashing Phase (Fast & Reliable, ~3 Seconds)  │
 └────────────────────────────────────────────────────────┘
   User taps "Install / Rollback" on any .bin in the list
         │
         ▼
   Reads from SD Card  ──► Flashes directly into standby slot (ota_0/ota_1)
   Integrity Verified  ──► Switches boot slot in otadata & reboots
         │
 ┌───────┴────────────────────────────────────────────────┐
 │ 3. Multi-Version Rollback Library                      │
 └────────────────────────────────────────────────────────┘
   /sdcard/firmware/
     ├── SmartAttendance_v1.0.0.bin  [Installed / Active]
     ├── SmartAttendance_v1.0.1.bin  [Available - Rollback]
     └── SmartAttendance_v1.0.2.bin  [Available - Rollback]
```

---

## Proposed Changes

### 1. Configuration & Safety
- **`sdkconfig`**:
  - Enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
  - Enable `CONFIG_APP_ROLLBACK_ENABLE=y`
- **`main/main.c`**:
  - At the end of `app_main()`, after hardware and UI initialization succeed, call:
    `esp_ota_mark_app_valid_cancel_rollback();`
    This marks the trial firmware as permanently valid. If a panic or crash occurs before this point, the bootloader automatically reverts to the previous slot on the next reboot.

---

### 2. Core OTA Manager ([`main/network/ota_manager.h`](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/ota_manager.h) & [`main/network/ota_manager.c`](file:///c:/Users/user/Documents/projects/SmartAttendance/main/network/ota_manager.c))
Expand the OTA manager with the following APIs:
- `ota_manager_init()`: Ensures `/sdcard/firmware/` directory exists.
- `ota_download_to_sd(const char *url, const char *version, ota_progress_cb_t cb)`:
  - Streams HTTP/HTTPS response body directly into `/sdcard/firmware/<filename>.bin.tmp`.
  - Verifies magic word (`ESP_APP_DESC_MAGIC_WORD` `0xABCD5432`) in the downloaded header.
  - Renames file to `/sdcard/firmware/<filename>.bin`.
- `ota_flash_from_sd(const char *filepath, ota_progress_cb_t cb)`:
  - Validates the local binary header.
  - Determines next update partition via `esp_ota_get_next_update_partition(NULL)`.
  - Uses `esp_ota_begin()`, streams in 16KB/32KB chunks via `esp_ota_write()`, and finalizes with `esp_ota_end()`.
  - Updates boot target via `esp_ota_set_boot_partition()`.
- `ota_list_sd_firmwares(firmware_info_t *out_list, int max_count, int *out_count)`:
  - Reads directory `/sdcard/firmware/`.
  - Parses `esp_app_desc_t` at offset `0x20` from each `.bin` file to extract version, build date, time, and SHA-256.
- `ota_get_current_info(firmware_info_t *out_info)`:
  - Returns current running partition label, version string, and compile date.

---

### 3. Firmware Manager UI ([`main/ui/ui_firmware_mgr.h`](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_firmware_mgr.h) & [`main/ui/ui_firmware_mgr.cpp`](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_firmware_mgr.cpp))
Create a dedicated full-screen management interface accessible from the Settings menu:
- **Header**: Shows current running version (e.g. `SmartAttendance v1.0.0 [ota_0]`).
- **Archive List**: A scrollable card listing all `.bin` files discovered in `/sdcard/firmware/`.
  - Displays version tag, build date, and file size.
  - Displays a green badge `[CURRENT]` next to the active version.
  - Displays a blue button **"Install / Rollback"** next to other versions.
- **Top Actions**:
  - **"Check Cloud Update"**: Checks server endpoint for a newer version and prompts:
    *"New version vX.Y.Z available. Download to SD Card?" [Cancel] [Download]*
  - **"Refresh List"**: Rescans `/sdcard/firmware/`.
- **Progress Modal**:
  - Modal overlay with progress bar during downloading and flashing.

---

### 4. Settings Screen Integration ([`main/ui/ui_settings.cpp`](file:///c:/Users/user/Documents/projects/SmartAttendance/main/ui/ui_settings.cpp))
- Add a **"Firmware & Updates"** button in the Settings menu that opens the Firmware Manager screen.

---

## Verification Plan

### Automated / Tool-Driven Steps
1. **Compilation Check**:
   - Recompile the project with `ninja -C build` to verify clean linking of `esp_ota_ops`, `app_update`, and new UI components.
2. **Flash to COM3**:
   - Flash the build via `idf.py -p COM3 flash`.
3. **Serial Monitor Validation**:
   - Verify bootloader identifies the active partition and confirms `esp_ota_mark_app_valid_cancel_rollback()`.
   - Verify `/sdcard/firmware/` directory creation.
4. **End-to-End Functional Test**:
   - Place a copy of `SmartAttendance.bin` into `/sdcard/firmware/SmartAttendance_v1.0.0.bin`.
   - Open Settings → Firmware & Updates.
   - Verify the screen correctly parses the binary's `esp_app_desc_t` header, lists the file, and displays the current running state.
   - Test flashing a binary from the SD card to confirm the slot flip between `ota_0` and `ota_1`.

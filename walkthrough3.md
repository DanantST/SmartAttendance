# Face Enrollment Freeze & Locking Fix Walkthrough

This document outlines the design modifications and code improvements made to solve the screen freezing and crash issues observed during student face enrollment.

## Changes Made

### 1. Custom Image Descriptors Flagging
To prevent the LVGL v9 binary image decoder from repeatedly re-allocating and re-wrapping our static camera buffers (which caused recursive rendering loops and potential memory corruption):
- Modified `main/ui/ui_enrollment.cpp` and `main/ui/ui_attendance.cpp` to set the `LV_IMAGE_FLAGS_ALLOCATED` flag in `s_camera_img_dsc.header.flags` right after initialization:
  ```cpp
  lv_draw_buf_init(&s_camera_img_dsc, ...);
  s_camera_img_dsc.header.flags |= LV_IMAGE_FLAGS_ALLOCATED;
  ```

### 2. Lock Acquisition Mutex Hardening
To prevent background tasks from invoking UI routines and releasing the display lock after a timeout (which corrupted the FreeRTOS recursive semaphore state and caused undefined cross-thread collisions):
- Checked the return value of `ui_acquire()` across all background tasks and helper functions:
  - **Main Tasks (`main/main.c`):** Updated `detection_recognition_task()`, `process_enrollment_frames_for_user()`, `start_single_capture_task()`, `start_enrollment_task()`, `network_sync_task()`, `system_state_machine()`, and `handle_low_battery()`.
  - **Queue Callback (`main/ble/ble_registration.c`):** Updated the queue push update callback.
  - **UI Helpers (`main/ui/ui_main.cpp`):** Hardened `ui_show_notification()`, `ui_hide_notification()`, and `ui_set_sd_card_status()`.
- If `ui_acquire()` returns `false` (timeout), the calling routines will skip widget manipulation and bypass the corresponding `ui_release()` call.
- Hardened `detection_recognition_task()` so that it still executes offline attendance database logging even if the UI lock is temporarily busy and fails to draw the face bounding box.

### 3. Asynchronous Object Deletion
To prevent Guru Meditation use-after-free panics and screen turn-offs when dismissing panels or popups (caused by deleting widgets via `lv_obj_del` from inside their own child click callbacks):
- Converted all panel deletion calls triggered by button callbacks from synchronous `lv_obj_del` to asynchronous `lv_obj_delete_async`:
  - **Main Navigation & Popups (`main/ui/ui_main.cpp`):** Notification banner, PIN prompt panel, keyboard panel, quick settings shade, and recent apps panel.
  - **System Settings Modals (`main/ui/ui_settings.cpp`):** Wi-Fi networks scan list and NVS known networks prompt.
  - **User Management Cards (`main/ui/ui_user_manager.cpp`):** User list cards deletion callback.
- This schedules the widget trees to be destroyed safely on the next display refresh timer loop after the event bubbling chain terminates.

### 4. Layout and Click Alignment Fixes
- **Retry Button Relocation (`main/ui/ui_enrollment.cpp`):** Moved the "Try Again" button from `s_enroll_screen` to `s_right_panel` and updated the vertical alignment offset to `-40`. This keeps the button safely positioned inside the panel boundaries and above the bottom navigation bar (Y=540 to 600), so the Home button no longer overlaps or blocks it.
- **Notification Close Button Enlargement (`main/ui/ui_main.cpp`):** Enlarged the close button size from `30x30` to `44x44` (the minimum standard touch target) and aligned it using `LV_ALIGN_RIGHT_MID` with a `-10` px offset to prevent it from clipping outside the parent container's padding box, ensuring it registers all touch clicks correctly.
- **Clean Exit to Main Launcher (`main/main.c`):** Updated the termination of `start_enrollment_task` to call `ui_return_to_main()` rather than invoking `ui_close_enrollment_screen()` directly. This switches back to the persistent main desktop screen first before destroying the enrollment view, avoiding blank screens, task lifecycle mismatches, or system reboots.
- **Task Lifecycle Guard (`main/ui/ui_enrollment.cpp`):** Added `g_enrollment_cancel = true` at the entry of `ui_close_enrollment_screen()` to immediately terminate the background capture loop if the screen is dismissed by the Home or Back navigation buttons.

### 5. Duplicate User Enrollment Gating
- **Embedding Recognition Check (`main/main.c`):** Added a check in `process_enrollment_frames_for_user()` after averaging the face embedding features. It runs `recognizer_identify` against the cache database, and if a face matches an existing student with confidence >= `RECOGNITION_THRESHOLD` (0.65f), it halts enrollment with `ESP_ERR_INVALID_STATE`.
- **UI Error Feedback (`main/main.c`):** Hooked the return value of `process_enrollment_frames_for_user()` inside `start_single_capture_task` so that returning `ESP_ERR_INVALID_STATE` displays a clear status message: `"Face already registered!"` instead of a generic capture failure.

---

## Testing & Verification

### Compilation
- Executed `.\build_workaround.bat` to configure and build the firmware.
- Compilation completed successfully with 0 errors and generated `build/SmartAttendance.bin`.

### Firmware Flashing
- Executed `.\flash_workaround.bat` to write the binary to the CrowPanel Advanced ESP32-P4 board on `COM5`.
- Flash complete, and system reset successfully.

### Manual Verification Instructions
1. Restart the device.
2. Connect to the setup portal `Attendance_Setup` (enter the session PIN shown on the screen).
3. Queue a student for face enrollment.
4. Tap the student card on the Remote Enrollment screen.
5. Verify that:
   - The camera preview displays and streams frames without stutter or locks.
   - Quality evaluation and capturing proceed smoothly to 100%.
   - Trying to enroll a face that has already been registered fails and shows "Face already registered!".
   - The "Try Again" button is fully visible and clickable above the navigation bar if capture fails.
   - Clicking the close button on the PIN banner or exiting remote enrollment returns cleanly to the main attendance scan screen without reboots or freezes.

---

## Phase 2 Update: Face Recognition & Quality-Weighted Vector Fusion (2026-06-12)

This update resolves the "Unknown 0.0%" recognition matching bug and implements a Quality-Weighted Mean vector fusion strategy for enrollment template creation.

### 1. Camera Frame Double-Buffering for Detection Task
To eliminate the DMA race condition where `camera_task` was concurrently overwriting the camera frame buffer (`s_scaled_frame_buffer`) while `detection_recognition_task` was doing face detection and alignment:
- Allocated a persistent static buffer `s_detection_fb_buf` in SPIRAM once during system initialization (`app_main`).
- Updated `camera_task` in `main/main.c` to perform single-copy double-buffering. 
- When `uxQueueSpacesAvailable` indicates the detection task is idle, the camera task copies the frame buffer using the exact `fb->len` byte size to `s_detection_fb_buf`, updates `fb_copy.buf` to point to it, and queues the frame.
- When the queue is full (detection task is busy), the copy is skipped, eliminating concurrent write interference. This ensures that the detection task and the subsequent alignment warp operate on a completely stable snapshot of pixels, resolving image tearing and restoring high-confidence similarity scores.

### 2. Quality-Weighted Mean Vector Fusion
To make student master templates more robust against suboptimal enrollment frames (e.g. frames with slight yaw, poor lighting, or blur):
- Replaced the simple arithmetic mean in `process_enrollment_frames_for_user()` in `main/main.c` with a Quality-Weighted Mean fusion.
- For each kept frame, we compute its $L_2$ norm and accumulate the average norm of the kept frames as a target norm.
- We perform a weighted sum of the embedding vectors using their corresponding quality scores (combining sharpness, yaw, and brightness) as weights:
  $$v_{\text{weighted}}[m] = \frac{\sum_i Q_i \cdot \mathbf{v}_i[m]}{\sum_i Q_i}$$
- Scaled the resulting centroid vector to the average target norm to prevent quantization precision loss:
  $$\mathbf{v}_{\text{scaled}} = \mathbf{v}_{\text{weighted}} \cdot \frac{\text{target\_norm}}{\|\mathbf{v}_{\text{weighted}}\|_2}$$
- Implemented strict value clipping to the `int8_t` range `[-128, 127]` and rounded to the nearest integer to avoid overflow/underflow distortion before saving the master embedding to the database.

### Verification & Flashing
- Compiling: Run `.\build_workaround.bat` — completed cleanly.
- Flashing: Run `.\flash_workaround.bat` — successfully flashed to device on `COM5` and hard reset.
- Verification: The face detection bounding box and live similarity scoring are restored and functional.


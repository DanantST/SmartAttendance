/**
 * @file ui_enrollment.h
 * @brief BLE Multi-Student Enrollment Screen
 *
 * Two-panel layout:
 *   Left  (260 px) — scrollable student queue populated live from BLE
 *   Right (764 px) — oval camera preview with animated progress bar and
 *                    face-detection border feedback
 *
 * Admin taps a student card → camera activates for that student.
 * Oval border turns green when a face is detected.
 * Animated "SUCCESS" popup on completion; student clears from the list.
 */

#ifndef UI_ENROLLMENT_H
#define UI_ENROLLMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_main.h"
#include "ble/ble_registration.h"     /* enrollment_data_t, MAX_PENDING_STUDENTS */

/* ─── Screen lifecycle ─────────────────────────────────────────────────── */

/** @brief Show the enrollment screen (call once; BLE advertising must be started separately) */
void ui_show_enrollment_screen(void);

/** @brief Close enrollment screen and return to main launcher */
void ui_close_enrollment_screen(void);

/* ─── Student list (left panel) ────────────────────────────────────────── */

/**
 * @brief Append a student card to the left-panel queue list.
 *        Safe to call from any task via ui_acquire()/ui_release().
 * @param name        student full name
 * @param student_id  student registration number
 */
void ui_enrollment_add_student(const char* name, const char* student_id);

/**
 * @brief Remove a student card from the left-panel list.
 *        Called automatically after a successful face capture.
 * @param idx  0-based index matching the BLE pending queue index
 */
void ui_enrollment_remove_student_by_index(int idx);

/* ─── Camera / capture feedback (right panel) ──────────────────────────── */

/**
 * @brief Set oval border and glow colour based on face detection result.
 *        Green = face detected; blue = no face.
 * @param detected  true if a face is currently visible in frame
 */
void ui_enrollment_set_face_detected(bool detected);

/**
 * @brief Update the animated capture progress bar above the oval.
 * @param current  frames captured so far
 * @param total    total frames required (≥ 15 per admin spec)
 */
void ui_enrollment_set_capture_progress(int current, int total);

/**
 * @brief Show guidance text below the oval (e.g. "Look straight at camera").
 * @param message  text to display (may be NULL to clear)
 */
void ui_show_pose_guidance(const char* message);

/**
 * @brief Show the animated "✓ SUCCESS" popup above the progress bar,
 *        then clear the student from the list after a short delay.
 * @param student_name  name to include in the success message
 * @param student_idx   BLE queue index to remove once animation completes
 */
void ui_enrollment_show_success(const char* student_name, int student_idx);

/**
 * @brief Show inline failure text with a retry hint.
 * @param success true on success, false on failure
 * @param message  error description
 */
void ui_enrollment_set_status(bool success, const char* message);

/**
 * @brief Show/hide the camera preview container.
 * @param active true to show, false to hide and clear buffer
 */
void ui_enrollment_set_camera_active(bool active);

/* ─── Camera frame delivery ─────────────────────────────────────────────── */

/**
 * @brief Push a decoded RGB565 camera frame into the oval preview.
 *        Should be called from the camera task; takes/releases the LVGL mutex internally.
 * @param img     pointer to RGB565 pixel data
 * @param width   source frame width in pixels
 * @param height  source frame height in pixels
 */
void ui_update_enrollment_camera_frame(uint8_t* img, int width, int height);

/* ─── Legacy aliases kept for compatibility ─────────────────────────────── */

/** @brief Alias: update progress bar (same as ui_enrollment_set_capture_progress) */
void ui_update_enrollment_progress(int current, int total);

/** @brief Alias: flash per-frame quality feedback icon */
void ui_update_enrollment_frame_captured(float quality);

#ifdef __cplusplus
}
#endif

#endif /* UI_ENROLLMENT_H */
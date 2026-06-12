/**
 * @file ble_registration.h
 * @brief In-memory enrollment queue for Web AP captive portal registration.
 *
 * Students and admins register via the Wi-Fi Soft-AP captive portal. This
 * module provides the thread-safe pending-student queue that bridges the
 * HTTP portal handler (which fills the queue) and the enrollment task
 * (which drains it when the admin taps a name on screen).
 *
 * PIN generation for portal identity verification is also provided here.
 */

#ifndef BLE_REGISTRATION_H
#define BLE_REGISTRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** Maximum number of students queued simultaneously */
#define MAX_PENDING_STUDENTS 20

/**
 * @brief Enrollment data submitted via the Web AP captive portal.
 *
 * 'department' has been removed — the admin configures the device's
 * department in Settings; students do not enter it during registration.
 */
typedef struct {
    char name[64];
    char student_id[32];
    char phone_number[20];  /* Matched against Telegram contact for linking */
    char role[16];          /* "student", "lecturer", "admin" */
    uint32_t timestamp;
} enrollment_data_t;

/**
 * @brief Internal enrollment status codes (used for UI feedback only).
 */
typedef enum {
    ENROLL_STATUS_IDLE       = 0,
    ENROLL_STATUS_QUEUED     = 1,
    ENROLL_STATUS_CAPTURING  = 2,
    ENROLL_STATUS_PROCESSING = 3,
    ENROLL_STATUS_SUCCESS    = 4,
    ENROLL_STATUS_FAILED     = 5
} enroll_status_t;

/* ─── Queue Lifecycle ───────────────────────────────────────────────────── */

/**
 * @brief Initialise the enrollment queue mutex. Must be called once at boot
 *        before the Wi-Fi AP or enrollment task starts.
 */
esp_err_t ble_registration_init(void);

/* ─── PIN Generation ────────────────────────────────────────────────────── */

/**
 * @brief Generate a fresh random 6-digit session PIN and store it
 *        internally. Call this when the AP enrollment session opens.
 *        The portal HTML displays this PIN so students can confirm
 *        they are connected to the correct device.
 */
void generate_enrollment_pin(void);

/**
 * @brief Return the current session PIN string (null-terminated, 6 digits).
 *        Valid after generate_enrollment_pin() has been called.
 */
const char* ble_registration_get_pin(void);

/* ─── Queue API ─────────────────────────────────────────────────────────── */

/** @brief Get the number of students currently waiting in the queue */
int ble_registration_get_pending_count(void);

/**
 * @brief Peek at a queued entry without removing it.
 * @param idx  0-based index into the pending queue
 * @param out  destination struct; filled on success
 * @return true if a valid entry exists at idx
 */
bool ble_registration_peek_student(int idx, enrollment_data_t* out);

/**
 * @brief Remove a queued entry after the face has been enrolled.
 *        Higher-index entries shift down.
 * @param idx  0-based index to remove
 * @return true on success
 */
bool ble_registration_consume_student(int idx);

/**
 * @brief Add a pending student/admin to the queue.
 *        Called from the Web AP portal HTTP handler and the Admin Setup wizard.
 * @param name         Full name
 * @param student_id   Matric / Staff / Admin ID
 * @param role         "student", "lecturer", or "admin"
 * @param phone_number Phone number for Telegram linking (may be NULL)
 * @return ESP_OK, or ESP_ERR_NO_MEM if queue is full
 */
esp_err_t ble_registration_add_pending_student(const char* name,
                                                const char* student_id,
                                                const char* role,
                                                const char* phone_number);

/**
 * @brief Log the final enrollment result (used for device-side status only).
 * @param success  true if face enrollment succeeded
 * @param user_id  DB row ID of the enrolled user (0 on failure)
 */
void ble_registration_set_result(bool success, uint32_t user_id);

#ifdef __cplusplus
}
#endif

#endif /* BLE_REGISTRATION_H */
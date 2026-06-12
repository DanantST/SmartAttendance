/**
 * @file ble_registration.c
 * @brief Thread-safe enrollment queue for Web AP captive portal registration.
 *
 * BLE/NimBLE removed 2026-06-12. This file now provides only:
 *   1. A mutex-protected pending-student queue (filled by the HTTP portal,
 *      drained by the enrollment UI task when admin taps a name).
 *   2. A 6-digit session PIN generator used by the portal so students
 *      can verify they are on the correct device hotspot.
 *
 * No Bluetooth stack is initialised. The `ble_registration_init()` name is
 * preserved for call-site compatibility.
 */

#include "ble_registration.h"
#include "esp_log.h"
#include "esp_random.h"
#include "config.h"
#include "ui/ui_main.h"
#include "ui/ui_enrollment.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "ENROLL_QUEUE";

/* ─── Session PIN ────────────────────────────────────────────────────────── */
static char s_enrollment_pin[7] = "000000";

/* ─── Pending student queue ─────────────────────────────────────────────── */
static enrollment_data_t s_pending_students[MAX_PENDING_STUDENTS];
static int               s_pending_count = 0;
static SemaphoreHandle_t s_queue_mutex   = NULL;

/* ─── Lifecycle ─────────────────────────────────────────────────────────── */

esp_err_t ble_registration_init(void) {
    if (s_queue_mutex) return ESP_OK; /* Already initialised */

    s_queue_mutex = xSemaphoreCreateMutex();
    if (!s_queue_mutex) {
        ESP_LOGE(TAG, "Failed to create queue mutex");
        return ESP_ERR_NO_MEM;
    }
    s_pending_count = 0;
    memset(s_pending_students, 0, sizeof(s_pending_students));
    ESP_LOGI(TAG, "Enrollment queue ready (max %d slots)", MAX_PENDING_STUDENTS);
    return ESP_OK;
}

/* ─── PIN ────────────────────────────────────────────────────────────────── */

void generate_enrollment_pin(void) {
    uint32_t pin_val = esp_random() % 1000000;
    snprintf(s_enrollment_pin, sizeof(s_enrollment_pin), "%06d", (int)pin_val);
    ESP_LOGW(TAG, "============================================");
    ESP_LOGW(TAG, " ENROLLMENT SESSION PIN: %s", s_enrollment_pin);
    ESP_LOGW(TAG, "============================================");
}

const char* ble_registration_get_pin(void) {
    return s_enrollment_pin;
}

/* ─── Queue API ─────────────────────────────────────────────────────────── */

int ble_registration_get_pending_count(void) {
    return s_pending_count;
}

bool ble_registration_peek_student(int idx, enrollment_data_t* out) {
    if (!out || idx < 0) return false;
    bool ok = false;
    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (idx < s_pending_count) {
            *out = s_pending_students[idx];
            ok = true;
        }
        xSemaphoreGive(s_queue_mutex);
    }
    return ok;
}

bool ble_registration_consume_student(int idx) {
    if (idx < 0) return false;
    bool ok = false;
    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (idx < s_pending_count) {
            for (int i = idx; i < s_pending_count - 1; i++) {
                s_pending_students[i] = s_pending_students[i + 1];
            }
            s_pending_count--;
            memset(&s_pending_students[s_pending_count], 0, sizeof(enrollment_data_t));
            ok = true;
            ESP_LOGI(TAG, "Consumed queue[%d]; %d remaining", idx, s_pending_count);
        }
        xSemaphoreGive(s_queue_mutex);
    }
    return ok;
}

esp_err_t ble_registration_add_pending_student(const char* name,
                                                const char* student_id,
                                                const char* role,
                                                const char* phone_number) {
    if (!name || !student_id) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = ESP_FAIL;

    if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (s_pending_count < MAX_PENDING_STUDENTS) {
            enrollment_data_t entry = {0};
            strncpy(entry.name,       name,       sizeof(entry.name)       - 1);
            strncpy(entry.student_id, student_id, sizeof(entry.student_id) - 1);
            if (phone_number) {
                strncpy(entry.phone_number, phone_number, sizeof(entry.phone_number) - 1);
            }
            strncpy(entry.role, role ? role : "student", sizeof(entry.role) - 1);
            entry.timestamp = (uint32_t)time(NULL);

            s_pending_students[s_pending_count] = entry;
            int new_idx = s_pending_count++;
            ESP_LOGI(TAG, "Queued %s [%d] — %s", entry.role, new_idx, entry.name);

            /* Push name to enrollment UI list if that screen is already open */
            if (get_system_state() == SYSTEM_STATE_ENROLLMENT) {
                if (ui_acquire()) {
                    ui_enrollment_add_student(entry.name, entry.student_id);
                    ui_release();
                }
            }
            ret = ESP_OK;
        } else {
            ESP_LOGW(TAG, "Queue full — dropping %s", name);
            ret = ESP_ERR_NO_MEM;
        }
        xSemaphoreGive(s_queue_mutex);
    }
    return ret;
}

void ble_registration_set_result(bool success, uint32_t user_id) {
    if (success) {
        ESP_LOGI(TAG, "Enrollment OK (DB id=%lu)", (unsigned long)user_id);
    } else {
        ESP_LOGW(TAG, "Enrollment FAILED");
    }
}

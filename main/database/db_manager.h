/**
 * @file db_manager.h
 * @brief SQLite database manager interface
 */

#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include "recognition/feature_extractor.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t id;
    char uuid[37];
    char name[64];
    char student_id[32];
    char phone_number[20]; // NEW: For Telegram linking
    char telegram_id[24];  // NEW: Telegram user_id after /start
    char role[16];
    face_embedding_t embedding;
    uint32_t created_at;
    uint32_t updated_at;
} user_t;

typedef struct {
    uint32_t id;
    char uuid[37];
    uint32_t user_id;
    uint32_t schedule_id;
    uint32_t timestamp;
    char status[8];
    uint8_t synced;
} attendance_log_t;

/* Database request types */
typedef enum {
    DB_REQUEST_INSERT_LOG,
    DB_REQUEST_GET_USERS,
    DB_REQUEST_UPDATE_SYNC,
    DB_REQUEST_INSERT_USER,
    DB_REQUEST_GET_USER_BY_ID,
} db_request_type_t;

typedef struct {
    db_request_type_t type;
    void *data;
    size_t data_len;
    bool free_data;
} db_request_t;

esp_err_t db_manager_init(void);
esp_err_t db_insert_user(user_t *user);
esp_err_t db_insert_attendance_log(attendance_log_t *log);
esp_err_t db_mark_logs_synced(uint32_t *ids, size_t count);
esp_err_t db_get_user_by_id(uint32_t id, user_t *user);
esp_err_t db_get_all_users(user_t **users, int *count);
uint32_t db_get_current_schedule_id(void);
void db_manager_flush(void);

/* Lecturer & Course extensions */
esp_err_t db_insert_lecturer(user_t *lecturer);
esp_err_t db_link_lecturer_course(int lecturer_id, int course_id);
esp_err_t db_insert_or_get_course(const char* code, const char* name, int* out_id);
esp_err_t db_get_lecturer_by_phone(const char* phone, user_t* out);
esp_err_t db_insert_schedule_from_bot(int course_id, int64_t start_ts, int64_t end_ts);

/**
 * @brief Export unsynced attendance logs to a CSV file
 * @param path File path to save CSV (e.g. "/sdcard/export.csv")
 * @return ESP_OK on success
 */
esp_err_t db_export_attendance_csv(const char* path);

/**
 * @brief Import schedule from a CSV string
 * @param csv_data CSV data string
 * @return ESP_OK on success
 */
esp_err_t db_import_schedule_csv(const char* csv_data);

/**
 * @brief Get count of unsynced logs
 */
int db_get_unsynced_log_count(void);

/**
 * @brief Delete a user and their data from the database
 * @param user_id User ID to delete
 * @return ESP_OK on success
 */
esp_err_t db_delete_user(uint32_t user_id);

/**
 * @brief Delete a user by their UUID
 */
esp_err_t db_delete_user_by_uuid(const char* uuid);

/**
 * @brief Delete a user by their student ID
 */
esp_err_t db_delete_user_by_student_id(const char* student_id);

/**
 * @brief Check if a student ID is already registered in the database
 */
bool db_student_id_exists(const char* student_id);


/**
 * @brief Wipe all tables in the database (Factory Reset)
 * @return ESP_OK on success
 */
esp_err_t db_factory_reset(void);

/**
 * @brief Mark all currently-unsynced attendance logs as synced.
 *        Called after a successful cloud upload to prevent re-uploading. [T5-1]
 * @return ESP_OK on success
 */
esp_err_t db_mark_all_logs_synced(void);

/**
 * @brief Get attendance report as a CSV string
 * @param report_str Pointer to char pointer that will hold the dynamically allocated CSV string. Caller must free().
 * @param course_id The ID of the course to filter by (or 0 for all).
 * @param date_timestamp The midnight timestamp of the selected date to filter by (or 0 for all).
 * @return ESP_OK on success
 */
esp_err_t db_get_attendance_report(char **report_str, int course_id, int date_timestamp);

/**
 * @brief Add a course to the database
 */
esp_err_t db_insert_course(const char* name, const char* code, const char* uuid);

/**
 * @brief Enroll a user in a course by UUID and course code.
 *        Called by cloud_sync when the Telegram bot pushes enrollment data.
 */
esp_err_t db_link_user_course(const char* user_uuid, const char* course_code);

/**
 * @brief Get all course codes a user is enrolled in.
 *        Caller must free() the returned array and each element.
 * @param user_id    Local DB user ID
 * @param codes_out  Receives a heap-allocated array of course code strings
 * @param count_out  Number of courses returned
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no enrollments
 */
esp_err_t db_get_user_courses(uint32_t user_id, char*** codes_out, int* count_out);

/**
 * @brief Get all course names from the database
 */
esp_err_t db_get_all_courses(char*** names, int* count);


#ifdef __cplusplus
}
#endif

#endif /* DB_MANAGER_H */
/**
 * @file db_manager.c
 * @brief SQLite database manager implementation
 */

#include "db_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sqlite3.h"
#include "recognition/feature_extractor.h"
#include <time.h>
#include <ctype.h>
#include <string.h>
#include "storage/sdcard_mount.h"
#include <inttypes.h>
#include <sys/stat.h>

static const char *TAG = "DB";
static sqlite3 *s_db = NULL;
static bool s_initialized = false;
static SemaphoreHandle_t s_db_mutex = NULL;

#define DB_LOCK() do { if (s_db_mutex) xSemaphoreTake(s_db_mutex, portMAX_DELAY); } while(0)
#define DB_UNLOCK() do { if (s_db_mutex) xSemaphoreGive(s_db_mutex); } while(0)

static esp_err_t db_deduplicate_schedules(void);

/* SQL schema */
static const char *CREATE_TABLES_SQL =
    "CREATE TABLE IF NOT EXISTS users ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    uuid TEXT UNIQUE NOT NULL,"
    "    name TEXT NOT NULL,"
    "    student_id TEXT UNIQUE,"
    "    phone_number TEXT,"
    "    telegram_id TEXT,"
    "    role TEXT NOT NULL CHECK(role IN ('student','lecturer','admin')),"
    "    face_embedding BLOB,"
    "    created_at INTEGER NOT NULL,"
    "    updated_at INTEGER,"
    "    enroll_quality INTEGER DEFAULT 0,"
    "    enroll_accepted INTEGER DEFAULT 0,"
    "    enroll_rejected INTEGER DEFAULT 0,"
    "    model_version INTEGER DEFAULT 1,"
    "    embedding_dim INTEGER DEFAULT 128"
    ");"
    "CREATE TABLE IF NOT EXISTS courses ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    uuid TEXT UNIQUE NOT NULL,"
    "    name TEXT NOT NULL,"
    "    code TEXT UNIQUE NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS lecturer_courses ("
    "    lecturer_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "    course_id   INTEGER NOT NULL REFERENCES courses(id) ON DELETE CASCADE,"
    "    PRIMARY KEY (lecturer_id, course_id)"
    ");"
    /* Students enroll in courses via the Telegram bot. */
    "CREATE TABLE IF NOT EXISTS user_courses ("
    "    user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "    course_id   INTEGER NOT NULL REFERENCES courses(id) ON DELETE CASCADE,"
    "    enrolled_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),"
    "    enrolled_by TEXT DEFAULT 'telegram',"
    "    PRIMARY KEY (user_id, course_id)"
    ");"
    "CREATE TABLE IF NOT EXISTS schedule ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    uuid TEXT UNIQUE NOT NULL,"
    "    course_id INTEGER NOT NULL REFERENCES courses(id) ON DELETE CASCADE,"
    "    start_time INTEGER NOT NULL,"
    "    end_time INTEGER NOT NULL,"
    "    location TEXT,"
    "    recurrence_rule TEXT,"
    "    created_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS attendance ("
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "    uuid TEXT UNIQUE NOT NULL,"
    "    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,"
    "    schedule_id INTEGER NOT NULL REFERENCES schedule(id) ON DELETE CASCADE,"
    "    timestamp INTEGER NOT NULL,"
    "    status TEXT NOT NULL DEFAULT 'present' CHECK(status IN ('present','late','absent')),"
    "    sync_status INTEGER DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS sync_state ("
    "    id INTEGER PRIMARY KEY CHECK (id = 1),"
    "    last_sync_timestamp INTEGER,"
    "    last_event_id INTEGER,"
    "    server_url TEXT"
    ");"
    "PRAGMA journal_mode=TRUNCATE;"
    "PRAGMA synchronous=NORMAL;"
    ;

esp_err_t db_manager_init(void) {
    if (s_initialized) return ESP_OK;

    /* Mount SD card first */
    esp_err_t ret = sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card");
        return ret;
    }

    /* Initialize SQLite library */
    sqlite3_initialize();

    /* Open SQLite Database on SD Card */
    int rc = sqlite3_open(DB_PATH, &s_db);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Failed to open database (rc=%d): %s. Attempting to recreate...", rc, sqlite3_errmsg(s_db));
        if (s_db) {
            sqlite3_close(s_db);
            s_db = NULL;
        }
        
        /* Clean up any corrupted or incompatible WAL database files */
        remove(DB_PATH);
        remove(DB_PATH "-wal");
        remove(DB_PATH "-shm");
        
        rc = sqlite3_open(DB_PATH, &s_db);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "Failed to open database on second attempt: %s", sqlite3_errmsg(s_db));
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Database recreated and opened successfully");
    }

    /* Enable foreign key constraint enforcement */
    sqlite3_exec(s_db, "PRAGMA foreign_keys = ON;", NULL, NULL, NULL);

    /* Execute schema */
    char *errmsg = NULL;
    rc = sqlite3_exec(s_db, CREATE_TABLES_SQL, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "SQL error: %s", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(s_db);
        return ESP_FAIL;
    }

    /* Run schema migrations for existing databases (ignore errors if columns already exist) */
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN phone_number TEXT;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN telegram_id TEXT;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN enroll_quality INTEGER DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN enroll_accepted INTEGER DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN enroll_rejected INTEGER DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN model_version INTEGER DEFAULT 1;", NULL, NULL, NULL);
    sqlite3_exec(s_db, "ALTER TABLE users ADD COLUMN embedding_dim INTEGER DEFAULT 128;", NULL, NULL, NULL);

    s_db_mutex = xSemaphoreCreateMutex();
    s_initialized = true;
    
    /* Create directories if they do not exist */
    mkdir("/sdcard/audio", 0777);
    mkdir("/sdcard/models", 0777);
    mkdir("/sdcard/models/p4", 0777);
    mkdir("/sdcard/users", 0777);
    ESP_LOGI(TAG, "Standard SD card directories verified/created");

    /* Seed default courses if the table is empty */
    sqlite3_stmt *seed_stmt;
    if (sqlite3_prepare_v2(s_db, "SELECT count(*) FROM courses;", -1, &seed_stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(seed_stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(seed_stmt, 0);
            if (count == 0) {
                sqlite3_exec(s_db, "INSERT INTO courses (uuid, name, code) VALUES ('c1', 'Linear Algebra', 'MATH201');", NULL, NULL, NULL);
                sqlite3_exec(s_db, "INSERT INTO courses (uuid, name, code) VALUES ('c2', 'Computer Vision', 'CS480');", NULL, NULL, NULL);
                sqlite3_exec(s_db, "INSERT INTO courses (uuid, name, code) VALUES ('c3', 'Embedded Systems', 'ECE320');", NULL, NULL, NULL);
                ESP_LOGI(TAG, "Database seeded with default courses");
            }
        }
        sqlite3_finalize(seed_stmt);
    }

    /* Seed default student course enrollments if they do not exist */
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (14, 1, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (14, 2, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (14, 3, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (14, 4, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (14, 5, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (8, 1, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (8, 2, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (8, 3, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (8, 4, 'telegram');", NULL, NULL, NULL);
    sqlite3_exec(s_db, "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (8, 5, 'telegram');", NULL, NULL, NULL);

    /* Clean up any duplicate schedules that may have been created by previous sync cycles */
    db_deduplicate_schedules();
    db_dump_schedules();

    /* DI-2: One-time migration — normalise all existing course codes to UPPERCASE + no leading/trailing
     * whitespace. Merges duplicates created by variants like "MTE 534" vs "MTE534":
     *   1. Compute the normalised code for every row.
     *   2. For rows whose code already equals the normalised form, do nothing.
     *   3. If a canonical row already exists, remap schedule and user_courses FK references
     *      from the variant row to the canonical row, then delete the variant row.
     *   4. Otherwise, update the row in place with the normalised code. */
    {
        sqlite3_stmt *mig_stmt;
        const char *mig_sql = "SELECT id, code FROM courses";
        if (sqlite3_prepare_v2(s_db, mig_sql, -1, &mig_stmt, NULL) == SQLITE_OK) {
            /* Collect all (id, code) pairs into a temp list */
            typedef struct { int id; char code[32]; } course_row_t;
            course_row_t rows[64];
            int row_count = 0;
            while (sqlite3_step(mig_stmt) == SQLITE_ROW && row_count < 64) {
                rows[row_count].id = sqlite3_column_int(mig_stmt, 0);
                const char *raw = (const char*)sqlite3_column_text(mig_stmt, 1);
                if (!raw) { row_count++; continue; }
                /* Compute normalised code */
                const char *p = raw;
                while (*p == ' ' || *p == '\t') p++;
                const char *e = raw + strlen(raw) - 1;
                while (e > p && (*e == ' ' || *e == '\t')) e--;
                int len = (int)(e - p + 1);
                if (len >= 32) len = 31;
                for (int k = 0; k < len; k++)
                    rows[row_count].code[k] = (char)toupper((unsigned char)p[k]);
                rows[row_count].code[len] = '\0';
                row_count++;
            }
            sqlite3_finalize(mig_stmt);

            for (int i = 0; i < row_count; i++) {
                /* Find canonical row (lowest id with the same normalised code) */
                int canonical_id = rows[i].id;
                for (int j = 0; j < row_count; j++) {
                    if (j != i && strcmp(rows[j].code, rows[i].code) == 0 && rows[j].id < canonical_id)
                        canonical_id = rows[j].id;
                }
                if (canonical_id != rows[i].id) {
                    /* This row is a duplicate — remap FKs to canonical, then delete */
                    char remap_sql[256];
                    snprintf(remap_sql, sizeof(remap_sql),
                             "UPDATE schedule SET course_id=%d WHERE course_id=%d", canonical_id, rows[i].id);
                    sqlite3_exec(s_db, remap_sql, NULL, NULL, NULL);
                    snprintf(remap_sql, sizeof(remap_sql),
                             "UPDATE user_courses SET course_id=%d WHERE course_id=%d", canonical_id, rows[i].id);
                    sqlite3_exec(s_db, remap_sql, NULL, NULL, NULL);
                    snprintf(remap_sql, sizeof(remap_sql),
                             "DELETE FROM courses WHERE id=%d", rows[i].id);
                    sqlite3_exec(s_db, remap_sql, NULL, NULL, NULL);
                    ESP_LOGI(TAG, "DI-2: Merged duplicate course id=%d ('%s') -> canonical id=%d",
                             rows[i].id, rows[i].code, canonical_id);
                } else {
                    /* No duplicate — normalise code in place */
                    char upd_sql[128];
                    snprintf(upd_sql, sizeof(upd_sql),
                             "UPDATE courses SET code='%s' WHERE id=%d", rows[i].code, rows[i].id);
                    sqlite3_exec(s_db, upd_sql, NULL, NULL, NULL);
                }
            }
        }
        ESP_LOGI(TAG, "DI-2: Course code normalisation migration complete");
    }

    /* Dump debug information about users, courses, enrollments, and attendance logs */
    extern esp_err_t db_debug_dump_tables(void);
    db_debug_dump_tables();

    /* NOTE: The /sdcard/attendance_report.csv on-disk file is only written on user demand via the
     * Reports screen "Export CSV" button (after SNTP has synced), so we no longer read it at boot
     * to avoid printing a stale "1 January 1970" header to the monitor. [DI-1] */

    ESP_LOGI(TAG, "Database initialized");
    return ESP_OK;
}

esp_err_t db_insert_user(user_t *user) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!user) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at, "
                      "enroll_quality, enroll_accepted, enroll_rejected, model_version, embedding_dim) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    /* Bind parameters */
    sqlite3_bind_text(stmt, 1, user->uuid, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user->student_id[0] ? user->student_id : NULL, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user->phone_number[0] ? user->phone_number : NULL, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, user->telegram_id[0] ? user->telegram_id : NULL, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, user->role, -1, SQLITE_STATIC);

    /* Bind embedding if present (e.g. not all zeros) */
    bool has_embedding = false;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        if (user->embedding.values[i] != 0) {
            has_embedding = true;
            break;
        }
    }
    if (has_embedding) {
        sqlite3_bind_blob(stmt, 7, user->embedding.values, sizeof(user->embedding.values), SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    sqlite3_bind_int(stmt, 8, user->created_at);
    sqlite3_bind_int(stmt, 9, user->updated_at);
    sqlite3_bind_int(stmt, 10, user->enroll_quality);
    sqlite3_bind_int(stmt, 11, user->enroll_accepted);
    sqlite3_bind_int(stmt, 12, user->enroll_rejected);
    sqlite3_bind_int(stmt, 13, user->model_version > 0 ? user->model_version : 1);
    sqlite3_bind_int(stmt, 14, user->embedding_dim > 0 ? user->embedding_dim : 128);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Insert failed: %s", sqlite3_errmsg(s_db));
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_FAIL;
    }

    user->id = sqlite3_last_insert_rowid(s_db);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_insert_attendance_log(attendance_log_t *log) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!log) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    
    /* Issue 1.5: Duplicate Guard - check if user already logged for this schedule today */
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT id FROM attendance WHERE user_id = ? AND schedule_id = ? "
                            "AND date(timestamp, 'unixepoch') = date(?, 'unixepoch') LIMIT 1";
    
    if (sqlite3_prepare_v2(s_db, check_sql, -1, &check_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(check_stmt, 1, log->user_id);
        sqlite3_bind_int(check_stmt, 2, log->schedule_id);
        sqlite3_bind_int(check_stmt, 3, log->timestamp);
        
        if (sqlite3_step(check_stmt) == SQLITE_ROW) {
            ESP_LOGI(TAG, "Duplicate attendance suppressed for user %d", (int)log->user_id);
            sqlite3_finalize(check_stmt);
            DB_UNLOCK();
            return ESP_OK; /* Already logged, return success without inserting */
        }
        sqlite3_finalize(check_stmt);
    }

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO attendance (uuid, user_id, schedule_id, timestamp, status, sync_status) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return ESP_FAIL;

    sqlite3_bind_text(stmt, 1, log->uuid, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, log->user_id);
    sqlite3_bind_int(stmt, 3, log->schedule_id);
    sqlite3_bind_int(stmt, 4, log->timestamp);
    sqlite3_bind_text(stmt, 5, log->status, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, log->synced);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Insert log failed: %s", sqlite3_errmsg(s_db));
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_FAIL;
    }
    log->id = sqlite3_last_insert_rowid(s_db);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_mark_logs_synced(uint32_t *ids, size_t count) {
    if (!s_initialized || !ids || count == 0) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE attendance SET sync_status = 1 WHERE id = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        DB_UNLOCK();
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; i++) {
        sqlite3_bind_int(stmt, 1, ids[i]);
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            ESP_LOGW(TAG, "Failed to mark log %" PRIu32 " as synced", ids[i]);
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_get_user_by_id(uint32_t id, user_t *user) {
    if (!s_initialized || !user) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at FROM users WHERE id = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { DB_UNLOCK(); return ESP_FAIL; }

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        memset(user, 0, sizeof(*user));  /* Issue 3.5: zero-init for safe strncpy */
        user->id = sqlite3_column_int(stmt, 0);
        const char *uuid = (const char*)sqlite3_column_text(stmt, 1);
        if (uuid) strncpy(user->uuid, uuid, sizeof(user->uuid)-1);
        const char *name = (const char*)sqlite3_column_text(stmt, 2);
        if (name) strncpy(user->name, name, sizeof(user->name)-1);
        const char *student_id = (const char*)sqlite3_column_text(stmt, 3);
        if (student_id) strncpy(user->student_id, student_id, sizeof(user->student_id)-1);
        const char *phone = (const char*)sqlite3_column_text(stmt, 4);
        if (phone) strncpy(user->phone_number, phone, sizeof(user->phone_number)-1);
        const char *telegram = (const char*)sqlite3_column_text(stmt, 5);
        if (telegram) strncpy(user->telegram_id, telegram, sizeof(user->telegram_id)-1);
        const char *role = (const char*)sqlite3_column_text(stmt, 6);
        if (role) strncpy(user->role, role, sizeof(user->role)-1);
        /* Issue 3.4: guard against NULL blob */
        const void *blob = sqlite3_column_blob(stmt, 7);
        if (blob) memcpy(user->embedding.values, blob, sizeof(user->embedding.values));
        user->created_at = sqlite3_column_int(stmt, 8);
        user->updated_at = sqlite3_column_int(stmt, 9);
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_OK;
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_get_all_users(user_t **users, int *count) {
    if (!s_initialized || !users || !count) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at, "
                      "enroll_quality, enroll_accepted, enroll_rejected, model_version, embedding_dim FROM users";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        DB_UNLOCK();
        return ESP_FAIL;
    }

    /* Count rows first */
    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) (*count)++;
    sqlite3_reset(stmt);

    if (*count == 0) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_OK;
    }

    *users = heap_caps_malloc((*count) * sizeof(user_t), MALLOC_CAP_SPIRAM);
    if (!*users) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        user_t *u = &(*users)[idx];
        memset(u, 0, sizeof(*u));  /* Issue 3.5: zero-init for safe strncpy */
        u->id = sqlite3_column_int(stmt, 0);
        const char *uuid = (const char*)sqlite3_column_text(stmt, 1);
        if (uuid) strncpy(u->uuid, uuid, sizeof(u->uuid)-1);
        const char *name = (const char*)sqlite3_column_text(stmt, 2);
        if (name) strncpy(u->name, name, sizeof(u->name)-1);
        const char *student_id = (const char*)sqlite3_column_text(stmt, 3);
        if (student_id) strncpy(u->student_id, student_id, sizeof(u->student_id)-1);
        const char *phone = (const char*)sqlite3_column_text(stmt, 4);
        if (phone) strncpy(u->phone_number, phone, sizeof(u->phone_number)-1);
        const char *telegram = (const char*)sqlite3_column_text(stmt, 5);
        if (telegram) strncpy(u->telegram_id, telegram, sizeof(u->telegram_id)-1);
        const char *role = (const char*)sqlite3_column_text(stmt, 6);
        if (role) strncpy(u->role, role, sizeof(u->role)-1);
        /* Issue 3.4: guard against NULL blob */
        const void *blob = sqlite3_column_blob(stmt, 7);
        if (blob) memcpy(u->embedding.values, blob, sizeof(u->embedding.values));
        u->created_at = sqlite3_column_int(stmt, 8);
        u->updated_at = sqlite3_column_int(stmt, 9);
        u->enroll_quality  = (uint8_t)sqlite3_column_int(stmt, 10);
        u->enroll_accepted = (uint8_t)sqlite3_column_int(stmt, 11);
        u->enroll_rejected = (uint8_t)sqlite3_column_int(stmt, 12);
        u->model_version   = (uint8_t)sqlite3_column_int(stmt, 13);
        u->embedding_dim   = (uint16_t)sqlite3_column_int(stmt, 14);
        idx++;
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

uint32_t db_get_current_schedule_id(void) {
    if (!s_initialized) return 0;

    DB_LOCK();
    time_t now = time(NULL);
    sqlite3_stmt *stmt;
    /* Exact active time window match only — no fallback.
     * Returning any random schedule when outside a window caused attendance
     * logs to be written with a mismatched schedule_id, breaking CSV reports.
     * Outside a scheduled window the scanner shows "Identified as [Name]"
     * without logging, which is the correct behaviour. */
    const char *sql = "SELECT id FROM schedule WHERE start_time <= ? AND end_time >= ? LIMIT 1";
    uint32_t schedule_id = 0;
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, (int)now);
        sqlite3_bind_int(stmt, 2, (int)now);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            schedule_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    DB_UNLOCK();
    return schedule_id;
}

void db_manager_flush(void) {
    /* Issue 4.7: Force WAL checkpoint to persist data before shutdown */
    DB_LOCK();
    if (s_db) {
        char *errmsg = NULL;
        int rc = sqlite3_exec(s_db, "PRAGMA wal_checkpoint(FULL);", NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "WAL checkpoint failed: %s", errmsg ? errmsg : "unknown");
            if (errmsg) sqlite3_free(errmsg);
        } else {
            ESP_LOGI(TAG, "WAL checkpoint completed");
        }
    }
    DB_UNLOCK();
}

int db_get_unsynced_log_count(void) {
    if (!s_initialized) return 0;
    
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM attendance WHERE sync_status = 0";
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        DB_UNLOCK();
        return 0;
    }
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return count;
}

esp_err_t db_export_attendance_csv(const char* path) {
    if (!s_initialized || !path) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open export file: %s", path);
        return ESP_FAIL;
    }
    
    /* Write CSV header */
    fprintf(f, "LogID,Timestamp,UserID,Name,StudentID,Status\n");
    
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = 
        "SELECT a.id, a.timestamp, u.id, u.name, u.student_id, a.status "
        "FROM attendance a JOIN users u ON a.user_id = u.id "
        "WHERE a.sync_status = 0";
    
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        DB_UNLOCK();
        fclose(f);
        return ESP_FAIL;
    }
    
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fprintf(f, "%d,%d,%d,\"%s\",\"%s\",\"%s\"\n",
                sqlite3_column_int(stmt, 0),
                sqlite3_column_int(stmt, 1),
                sqlite3_column_int(stmt, 2),
                sqlite3_column_text(stmt, 3),
                sqlite3_column_text(stmt, 4),
                sqlite3_column_text(stmt, 5));
        rows++;
    }
    
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    fclose(f);
    ESP_LOGI(TAG, "Exported %d logs to %s", rows, path);
    return ESP_OK;
}

esp_err_t db_import_schedule_csv(const char* csv_data) {
    if (!s_initialized || !csv_data) return ESP_ERR_INVALID_ARG;
    
    ESP_LOGI(TAG, "Importing schedule from CSV");
    
    char *data = strdup(csv_data);
    char *line = strtok(data, "\n");
    
    /* Skip header if present */
    if (line && strcasestr(line, "CourseName")) {
        line = strtok(NULL, "\n");
    }
    
    DB_LOCK();
    while (line) {
        char *name = NULL, *code = NULL, *start_s = NULL, *end_s = NULL, *loc = NULL;
        
        name = line;
        char *p = strchr(line, ',');
        if (p) { *p = '\0'; code = p + 1; }
        if (code) { p = strchr(code, ','); if (p) { *p = '\0'; start_s = p + 1; } }
        if (start_s) { p = strchr(start_s, ','); if (p) { *p = '\0'; end_s = p + 1; } }
        if (end_s) { p = strchr(end_s, ','); if (p) { *p = '\0'; loc = p + 1; } }
        
        if (name && code && start_s && end_s) {
            uint32_t start_t = strtoul(start_s, NULL, 10);
            uint32_t end_t = strtoul(end_s, NULL, 10);
            
            sqlite3_stmt* stmt;
            /* Insert or Update Course */
            const char* sql_c =
                "INSERT OR IGNORE INTO courses "
                "(uuid, name, code) "
                "VALUES (lower(hex(randomblob(16))), ?, ?)";
            if (sqlite3_prepare_v2(s_db, sql_c, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, code, -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            
            /* Insert schedule entry */
            const char* sql_s =
                "INSERT OR REPLACE INTO schedule "
                "(uuid, course_id, start_time, end_time, location, created_at) "
                "VALUES (lower(hex(randomblob(16))), "
                " (SELECT id FROM courses WHERE code=?), "
                " ?, ?, ?, strftime('%s','now'))";
            if (sqlite3_prepare_v2(s_db, sql_s, -1, &stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
                sqlite3_bind_int(stmt, 2, (int)start_t);
                sqlite3_bind_int(stmt, 3, (int)end_t);
                sqlite3_bind_text(stmt, 4, loc ? loc : "", -1, SQLITE_STATIC);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
            
            ESP_LOGI(TAG, "Imported schedule entry: %s (%s)", name, code);
        }
        
        line = strtok(NULL, "\n");
    }
    DB_UNLOCK();
    
    free(data);
    return ESP_OK;
}

/**
 * @brief Mark all unsynced attendance logs as synced. [T5-1]
 *        Called after a successful cloud upload.
 */
esp_err_t db_mark_all_logs_synced(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    DB_LOCK();
    char *errmsg = NULL;
    int rc = sqlite3_exec(s_db,
                          "UPDATE attendance SET sync_status = 1 WHERE sync_status = 0",
                          NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "db_mark_all_logs_synced failed: %s", errmsg ? errmsg : "unknown");
        if (errmsg) sqlite3_free(errmsg);
        DB_UNLOCK();
        return ESP_FAIL;
    }
    DB_UNLOCK();
    ESP_LOGI(TAG, "All unsynced attendance logs marked as synced");
    return ESP_OK;
}

esp_err_t db_delete_user(uint32_t user_id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    /* [Fix C2] Use a prepared statement — never interpolate user-controlled data into SQL strings. */
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM users WHERE id = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare delete failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, (int)user_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Failed to delete user %d: %s", (int)user_id, sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t db_delete_user_by_uuid(const char* uuid) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!uuid) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM users WHERE uuid = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare delete by UUID failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Failed to delete user by UUID %s: %s", uuid, sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t db_delete_course_by_code(const char* code) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!code) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM courses WHERE code = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare delete course by code failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Failed to delete course by code %s: %s", code, sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Deleted course %s and cascaded dependent tables", code);
    return ESP_OK;
}

esp_err_t db_delete_schedule_by_details(const char* course_code, int64_t start_time, int64_t end_time) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!course_code) return ESP_ERR_INVALID_ARG;

    /* Strip spaces from incoming code in C (avoids SQLite REPLACE() parser stack overflow) */
    char clean_code[64] = {0};
    int j = 0;
    for (int i = 0; course_code[i] && j < (int)sizeof(clean_code) - 1; i++) {
        if (course_code[i] != ' ') clean_code[j++] = course_code[i];
    }

    DB_LOCK();

    /* Step 1: resolve course_id by iterating courses and comparing in C */
    int course_id = 0;
    sqlite3_stmt *find_stmt;
    if (sqlite3_prepare_v2(s_db, "SELECT id, code FROM courses", -1, &find_stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(find_stmt) == SQLITE_ROW) {
            int cid = sqlite3_column_int(find_stmt, 0);
            const char *stored = (const char*)sqlite3_column_text(find_stmt, 1);
            if (!stored) continue;
            /* Strip spaces from stored code */
            char stored_clean[64] = {0};
            int k = 0;
            for (int m = 0; stored[m] && k < (int)sizeof(stored_clean) - 1; m++) {
                if (stored[m] != ' ') stored_clean[k++] = stored[m];
            }
            if (strcmp(stored, course_code) == 0 || strcmp(stored_clean, clean_code) == 0) {
                course_id = cid;
                break;
            }
        }
        sqlite3_finalize(find_stmt);
    }

    if (course_id == 0) {
        ESP_LOGW(TAG, "Delete schedule: course '%s' not found in DB", course_code);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Step 2: simple DELETE by course_id — no SQLite functions needed */
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM schedule WHERE course_id = ? AND start_time = ? AND end_time = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare delete schedule failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, course_id);
    sqlite3_bind_int64(stmt, 2, start_time);
    sqlite3_bind_int64(stmt, 3, end_time);
    rc = sqlite3_step(stmt);
    int rows_deleted = sqlite3_changes(s_db);
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Failed to delete schedule for %s (%lld - %lld): %s",
                 course_code, (long long)start_time, (long long)end_time, sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    if (rows_deleted == 0) {
        ESP_LOGW(TAG, "Delete schedule for %s (%lld - %lld): no matching row found",
                 course_code, (long long)start_time, (long long)end_time);
    } else {
        ESP_LOGI(TAG, "Deleted %d schedule row(s) for %s (%lld - %lld)",
                 rows_deleted, course_code, (long long)start_time, (long long)end_time);
    }
    return ESP_OK;
}


esp_err_t db_update_user_telegram_id(const char* uuid, const char* telegram_id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!uuid || !telegram_id) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE users SET telegram_id = ?, updated_at = strftime('%s','now') WHERE uuid = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare update telegram_id failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, telegram_id, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, uuid, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Update telegram_id failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Updated telegram_id=%s for user UUID %s", telegram_id, uuid);
    return ESP_OK;
}

esp_err_t db_delete_user_by_student_id(const char* student_id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!student_id) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "DELETE FROM users WHERE student_id = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare delete by Student ID failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, student_id, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    int changes = sqlite3_changes(s_db);
    DB_UNLOCK();
    
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Failed to delete user by Student ID %s: %s", student_id, sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    if (changes == 0) {
        ESP_LOGW(TAG, "No user found with Student ID %s to delete", student_id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Deleted user with Student ID %s successfully", student_id);
    return ESP_OK;
}

esp_err_t db_factory_reset(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    ESP_LOGW(TAG, "Wiping database for factory reset...");

    /* [Fix C4] Only wipe tables that actually exist in the schema.
     * The "config" table was never created and caused a silent SQLite error. */
    const char* tables[] = {"attendance", "schedule", "user_courses", "lecturer_courses", "courses", "users"};
    DB_LOCK();
    for (int i = 0; i < 6; i++) {
        char sql[64];
        snprintf(sql, sizeof(sql), "DELETE FROM %s", tables[i]);
        char *errmsg = NULL;
        int rc = sqlite3_exec(s_db, sql, NULL, NULL, &errmsg);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "Factory reset failed on table %s: %s", tables[i], errmsg ? errmsg : "");
            if (errmsg) sqlite3_free(errmsg);
        }
    }
    DB_UNLOCK();

    /* Force WAL checkpoint to flush deletions to disk before restart */
    db_manager_flush();
    return ESP_OK;
}

static void format_custom_timestamp(time_t ts, char* buf, size_t max_len) {
    struct tm t;
    if (!localtime_r(&ts, &t)) return;
    const char* months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    int hour = t.tm_hour;
    const char* meridiem = "a.m.";
    if (hour >= 12) {
        meridiem = "p.m.";
        if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;
    
    snprintf(buf, max_len, "%d %s, %d. %d:%02d %s",
             t.tm_mday, months[t.tm_mon], t.tm_year + 1900,
             hour, t.tm_min, meridiem);
}

static void format_schedule_range(time_t start, time_t end, char* buf, size_t max_len) {
    struct tm t_start, t_end;
    if (!localtime_r(&start, &t_start) || !localtime_r(&end, &t_end)) return;
    const char* months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    
    int start_hour = t_start.tm_hour;
    const char* start_meridiem = "a.m.";
    if (start_hour >= 12) {
        start_meridiem = "p.m.";
        if (start_hour > 12) start_hour -= 12;
    }
    if (start_hour == 0) start_hour = 12;

    int end_hour = t_end.tm_hour;
    const char* end_meridiem = "a.m.";
    if (end_hour >= 12) {
        end_meridiem = "p.m.";
        if (end_hour > 12) end_hour -= 12;
    }
    if (end_hour == 0) end_hour = 12;

    snprintf(buf, max_len, "%d %s, %d. %d:%02d %s To %d:%02d %s",
             t_start.tm_mday, months[t_start.tm_mon], t_start.tm_year + 1900,
             start_hour, t_start.tm_min, start_meridiem,
             end_hour, t_end.tm_min, end_meridiem);
}

static void format_cell_time(time_t ts, char* buf, size_t max_len) {
    struct tm t;
    if (!localtime_r(&ts, &t)) return;
    int hour = t.tm_hour;
    const char* meridiem = "a.m.";
    if (hour >= 12) {
        meridiem = "p.m.";
        if (hour > 12) hour -= 12;
    }
    if (hour == 0) hour = 12;
    snprintf(buf, max_len, "%d:%02d %s", hour, t.tm_min, meridiem);
}

typedef struct {
    uint32_t id;
    char name[64];
    char student_id[32];
} student_row_t;

static void add_student_if_unique(student_row_t *students, int *student_count, uint32_t id, const char *name, const char *sid) {
    for (int i = 0; i < *student_count; i++) {
        if (students[i].id == id) {
            return;
        }
    }
    if (*student_count >= 500) return;
    students[*student_count].id = id;
    if (name) strncpy(students[*student_count].name, name, sizeof(students[*student_count].name) - 1);
    else students[*student_count].name[0] = '\0';
    if (sid) strncpy(students[*student_count].student_id, sid, sizeof(students[*student_count].student_id) - 1);
    else strcpy(students[*student_count].student_id, "N/A");
    (*student_count)++;
}

esp_err_t db_get_attendance_report(char **report_str, int course_id, int date_timestamp) {
    if (!s_initialized || !report_str) return ESP_ERR_INVALID_STATE;
    DB_LOCK();

    int target_course_id = course_id;
    if (target_course_id == 0) {
        sqlite3_stmt *stmt_c;
        if (sqlite3_prepare_v2(s_db, "SELECT id FROM courses LIMIT 1", -1, &stmt_c, NULL) == SQLITE_OK) {
            if (sqlite3_step(stmt_c) == SQLITE_ROW) {
                target_course_id = sqlite3_column_int(stmt_c, 0);
            }
            sqlite3_finalize(stmt_c);
        }
    }

    size_t buf_size = 128 * 1024;
    *report_str = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!*report_str) {
        *report_str = malloc(buf_size);
    }
    if (!*report_str) {
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }
    memset(*report_str, 0, buf_size);
    size_t offset = 0;

    #define APPEND_REPORT(...) do { \
        int n = snprintf((*report_str) + offset, buf_size - offset, __VA_ARGS__); \
        if (n > 0 && offset + n < buf_size) { \
            offset += n; \
        } \
    } while(0)

    if (target_course_id == 0) {
        APPEND_REPORT("S/N,Student name,Matric number\nNo records found,\n");
        DB_UNLOCK();
        return ESP_OK;
    }

    char course_code[64] = "N/A";
    char course_name[128] = "N/A";
    sqlite3_stmt *stmt_c;
    const char *sql_c = "SELECT code, name FROM courses WHERE id = ?";
    if (sqlite3_prepare_v2(s_db, sql_c, -1, &stmt_c, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_c, 1, target_course_id);
        if (sqlite3_step(stmt_c) == SQLITE_ROW) {
            const char *c = (const char*)sqlite3_column_text(stmt_c, 0);
            const char *n = (const char*)sqlite3_column_text(stmt_c, 1);
            if (c) strncpy(course_code, c, sizeof(course_code) - 1);
            if (n) strncpy(course_name, n, sizeof(course_name) - 1);
        }
        sqlite3_finalize(stmt_c);
    }

    /* Query all scheduled events for this course */
    typedef struct {
        uint32_t id;
        char title[128];
        time_t start_time;
        time_t end_time;
        bool is_test_or_exam;
    } sched_event_t;

    sched_event_t *events = calloc(100, sizeof(sched_event_t));
    if (!events) {
        free(*report_str);
        *report_str = NULL;
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }
    int event_count = 0;

    sqlite3_stmt *stmt_s;
    const char *sql_s = "SELECT id, COALESCE(location, 'Class'), start_time, end_time FROM schedule WHERE course_id = ? ORDER BY start_time ASC;";
    if (sqlite3_prepare_v2(s_db, sql_s, -1, &stmt_s, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt_s, 1, target_course_id);
        while (sqlite3_step(stmt_s) == SQLITE_ROW && event_count < 100) {
            events[event_count].id = sqlite3_column_int(stmt_s, 0);
            const char *loc = (const char*)sqlite3_column_text(stmt_s, 1);
            if (loc) {
                strncpy(events[event_count].title, loc, sizeof(events[event_count].title) - 1);
            } else {
                strcpy(events[event_count].title, "Class");
            }
            events[event_count].start_time = sqlite3_column_int(stmt_s, 2);
            events[event_count].end_time = sqlite3_column_int(stmt_s, 3);
            
            if (strcasestr(events[event_count].title, "Test") || 
                strcasestr(events[event_count].title, "Exam")) {
                events[event_count].is_test_or_exam = true;
            } else {
                events[event_count].is_test_or_exam = false;
            }
            event_count++;
        }
        sqlite3_finalize(stmt_s);
    }

    student_row_t *students = calloc(500, sizeof(student_row_t));
    if (!students) {
        free(events);
        free(*report_str);
        *report_str = NULL;
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }
    int student_count = 0;

    /* Check if any students are enrolled in user_courses for this course.
     * If there are zero enrollments (e.g. offline testing or local registration),
     * we fall back to showing all users with role='student' so that they still
     * appear in the report and are marked Absent/Present correctly. */
    int enrollment_count = 0;
    sqlite3_stmt *count_stmt;
    /* Count only student-role enrollments — lecturer entries from cloud sync
     * must not suppress the fallback that shows all registered students. */
    if (sqlite3_prepare_v2(s_db,
            "SELECT count(*) FROM user_courses uc "
            "JOIN users u ON uc.user_id = u.id "
            "WHERE uc.course_id = ? AND u.role = 'student'",
            -1, &count_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(count_stmt, 1, target_course_id);
        if (sqlite3_step(count_stmt) == SQLITE_ROW) {
            enrollment_count = sqlite3_column_int(count_stmt, 0);
        }
        sqlite3_finalize(count_stmt);
    }
    ESP_LOGI(TAG, "Attendance report: course_id=%d student_enrollment_count=%d",
             target_course_id, enrollment_count);

    if (enrollment_count > 0) {
        // Query 1: explicitly enrolled students (role=student)
        sqlite3_stmt *stmt1;
        const char *sql1 =
            "SELECT u.id, u.name, COALESCE(u.student_id, 'N/A') "
            "FROM users u "
            "JOIN user_courses uc ON u.id = uc.user_id "
            "WHERE uc.course_id = ? AND u.role = 'student';";
        int rc1 = sqlite3_prepare_v2(s_db, sql1, -1, &stmt1, NULL);
        if (rc1 == SQLITE_OK) {
            sqlite3_bind_int(stmt1, 1, target_course_id);
            while (sqlite3_step(stmt1) == SQLITE_ROW) {
                uint32_t id = sqlite3_column_int(stmt1, 0);
                const char *name = (const char*)sqlite3_column_text(stmt1, 1);
                const char *sid = (const char*)sqlite3_column_text(stmt1, 2);
                add_student_if_unique(students, &student_count, id, name, sid);
            }
            sqlite3_finalize(stmt1);
        } else {
            ESP_LOGE(TAG, "Report: enrolled student query prepare FAILED (rc=%d): %s", rc1, sqlite3_errmsg(s_db));
        }

        // Query 2: anyone who scanned/logged attendance for this course
        sqlite3_stmt *stmt2;
        const char *sql2 =
            "SELECT DISTINCT u.id, u.name, COALESCE(u.student_id, 'N/A') "
            "FROM users u "
            "JOIN attendance a ON u.id = a.user_id "
            "JOIN schedule s ON a.schedule_id = s.id "
            "WHERE s.course_id = ? AND u.role = 'student';";
        int rc2 = sqlite3_prepare_v2(s_db, sql2, -1, &stmt2, NULL);
        if (rc2 == SQLITE_OK) {
            sqlite3_bind_int(stmt2, 1, target_course_id);
            while (sqlite3_step(stmt2) == SQLITE_ROW) {
                uint32_t id = sqlite3_column_int(stmt2, 0);
                const char *name = (const char*)sqlite3_column_text(stmt2, 1);
                const char *sid = (const char*)sqlite3_column_text(stmt2, 2);
                add_student_if_unique(students, &student_count, id, name, sid);
            }
            sqlite3_finalize(stmt2);
        } else {
            ESP_LOGE(TAG, "Report: attended student query prepare FAILED (rc=%d): %s", rc2, sqlite3_errmsg(s_db));
        }
    } else {
        // No enrolled students in database: show all students registered on the device
        sqlite3_stmt *stmt3;
        const char *sql3 =
            "SELECT u.id, u.name, COALESCE(u.student_id, 'N/A') "
            "FROM users u "
            "WHERE u.role = 'student';";
        int rc3 = sqlite3_prepare_v2(s_db, sql3, -1, &stmt3, NULL);
        if (rc3 == SQLITE_OK) {
            while (sqlite3_step(stmt3) == SQLITE_ROW) {
                uint32_t id = sqlite3_column_int(stmt3, 0);
                const char *name = (const char*)sqlite3_column_text(stmt3, 1);
                const char *sid = (const char*)sqlite3_column_text(stmt3, 2);
                add_student_if_unique(students, &student_count, id, name, sid);
            }
            sqlite3_finalize(stmt3);
        } else {
            ESP_LOGE(TAG, "Report: all registered student query prepare FAILED (rc=%d): %s", rc3, sqlite3_errmsg(s_db));
        }
    }

    // Sort students by name using simple bubble sort
    for (int i = 0; i < student_count - 1; i++) {
        for (int j = i + 1; j < student_count; j++) {
            if (strcmp(students[i].name, students[j].name) > 0) {
                student_row_t temp = students[i];
                students[i] = students[j];
                students[j] = temp;
            }
        }
    }

    for (int i = 0; i < student_count; i++) {
        ESP_LOGI(TAG, "Report student[%d]: id=%lu name='%s' sid='%s'",
                 i, (unsigned long)students[i].id, students[i].name, students[i].student_id);
    }

    ESP_LOGI(TAG, "Report: course_id=%d events=%d students=%d (enrollment_count=%d)",
             target_course_id, event_count, student_count, enrollment_count);

    /* 1. Print Title Line */
    char gen_time_str[64] = "";
    format_custom_timestamp(time(NULL), gen_time_str, sizeof(gen_time_str));
    APPEND_REPORT("%s %s attendance report as at [%s]\n",
                  course_code, course_name, gen_time_str);

    /* 2. Print Table Header */
    APPEND_REPORT("S/N,Student name,Matric number");
    for (int i = 0; i < event_count; i++) {
        char range_str[128] = "";
        format_schedule_range(events[i].start_time, events[i].end_time, range_str, sizeof(range_str));
        APPEND_REPORT(",\"%s [%s]\"", events[i].title, range_str);
    }
    APPEND_REPORT("\n");

    /* 3. Print Student Rows */
    for (int s_idx = 0; s_idx < student_count; s_idx++) {
        APPEND_REPORT("%d,\"%s\",\"%s\"", s_idx + 1, students[s_idx].name, students[s_idx].student_id);
        
        for (int e_idx = 0; e_idx < event_count; e_idx++) {
            sqlite3_stmt *stmt_a;
            const char *sql_a = "SELECT timestamp FROM attendance WHERE user_id = ? AND schedule_id = ? ORDER BY timestamp ASC;";
            time_t logs[10];
            int log_count = 0;
            if (sqlite3_prepare_v2(s_db, sql_a, -1, &stmt_a, NULL) == SQLITE_OK) {
                sqlite3_bind_int(stmt_a, 1, students[s_idx].id);
                sqlite3_bind_int(stmt_a, 2, events[e_idx].id);
                while (sqlite3_step(stmt_a) == SQLITE_ROW && log_count < 10) {
                    logs[log_count++] = sqlite3_column_int(stmt_a, 0);
                }
                sqlite3_finalize(stmt_a);
            }
            
            char cell_val[128] = "";
            if (log_count == 0) {
                strcpy(cell_val, "Absent");
            } else {
                char check_in[32] = "";
                format_cell_time(logs[0], check_in, sizeof(check_in));
                
                if (events[e_idx].is_test_or_exam) {
                    if (log_count > 1) {
                        char check_out[32] = "";
                        format_cell_time(logs[log_count - 1], check_out, sizeof(check_out));
                        snprintf(cell_val, sizeof(cell_val), "Present\n%s To %s", check_in, check_out);
                    } else {
                        snprintf(cell_val, sizeof(cell_val), "Present\n%s", check_in);
                    }
                } else {
                    snprintf(cell_val, sizeof(cell_val), "Present\n%s", check_in);
                }
            }
            APPEND_REPORT(",\"%s\"", cell_val);
        }
        APPEND_REPORT("\n");
    }

    free(events);
    free(students);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_insert_course(const char* name, const char* code, const char* uuid) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!name || !code || !uuid) return ESP_ERR_INVALID_ARG;
    DB_LOCK();
    
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO courses (uuid, name, code) VALUES (?, ?, ?);";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare insert course failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    
    sqlite3_bind_text(stmt, 1, uuid, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, code, -1, SQLITE_STATIC);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Step insert course failed (rc=%d)", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t db_get_all_courses(char*** names, int* count) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!names || !count) return ESP_ERR_INVALID_ARG;
    DB_LOCK();
    
    sqlite3_stmt *stmt;
    const char *sql = "SELECT name FROM courses;";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare select courses failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    
    int max_courses = 50;
    char** list = (char**)malloc(sizeof(char*) * max_courses);
    int c = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW && c < max_courses) {
        const unsigned char* name = sqlite3_column_text(stmt, 0);
        list[c] = strdup((const char*)name);
        c++;
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    
    *names = list;
    *count = c;
    return ESP_OK;
}

esp_err_t db_get_all_courses_with_ids(int** ids, char*** names, int* count) {
    if (!s_initialized || !ids || !names || !count) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name FROM courses ORDER BY name ASC;";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare select courses with ids failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    int max_courses = 100;
    int *id_list = (int*)malloc(sizeof(int) * max_courses);
    char **name_list = (char**)malloc(sizeof(char*) * max_courses);
    if (!id_list || !name_list) {
        if (id_list) free(id_list);
        if (name_list) free(name_list);
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    int c = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && c < max_courses) {
        id_list[c] = sqlite3_column_int(stmt, 0);
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        name_list[c] = strdup(name ? (const char*)name : "Unknown");
        c++;
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();

    *ids = id_list;
    *names = name_list;
    *count = c;
    return ESP_OK;
}

esp_err_t db_get_all_courses_full(db_course_t **courses, int *count) {
    if (!s_initialized || !courses || !count) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    sqlite3_stmt *stmt;
    const char *sql = "SELECT code, name FROM courses";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare select all courses full failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    /* Count rows first */
    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) (*count)++;
    sqlite3_reset(stmt);

    if (*count == 0) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        *courses = NULL;
        return ESP_OK;
    }

    *courses = (db_course_t *)malloc((*count) * sizeof(db_course_t));
    if (!*courses) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < *count) {
        db_course_t *c = &(*courses)[idx];
        memset(c, 0, sizeof(*c));
        const char *code = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (code) strncpy(c->code, code, sizeof(c->code) - 1);
        if (name) strncpy(c->name, name, sizeof(c->name) - 1);
        idx++;
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_get_all_lecturer_assignments(db_lecturer_assignment_t **assignments, int *count) {
    if (!s_initialized || !assignments || !count) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    sqlite3_stmt *stmt;
    /* Join lecturer_courses → users (uuid) → courses (code) */
    const char *sql =
        "SELECT u.uuid, c.code "
        "FROM lecturer_courses lc "
        "JOIN users u ON lc.lecturer_id = u.id "
        "JOIN courses c ON lc.course_id = c.id";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare select lecturer assignments failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    /* Count rows first */
    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) (*count)++;
    sqlite3_reset(stmt);

    if (*count == 0) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        *assignments = NULL;
        return ESP_OK;
    }

    *assignments = (db_lecturer_assignment_t *)malloc((*count) * sizeof(db_lecturer_assignment_t));
    if (!*assignments) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < *count) {
        db_lecturer_assignment_t *a = &(*assignments)[idx];
        memset(a, 0, sizeof(*a));
        const char *uuid = (const char *)sqlite3_column_text(stmt, 0);
        const char *code = (const char *)sqlite3_column_text(stmt, 1);
        if (uuid) strncpy(a->lecturer_uuid, uuid, sizeof(a->lecturer_uuid) - 1);
        if (code) strncpy(a->course_code,   code, sizeof(a->course_code)   - 1);
        idx++;
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

/* --- Lecturer & Course Extensions implementation --- */

esp_err_t db_insert_lecturer(user_t *lecturer) {
    if (!lecturer) return ESP_ERR_INVALID_ARG;
    strcpy(lecturer->role, "lecturer");
    memset(&lecturer->embedding, 0, sizeof(lecturer->embedding));
    return db_insert_user(lecturer);
}

esp_err_t db_link_lecturer_course(int lecturer_id, int course_id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO lecturer_courses (lecturer_id, course_id) VALUES (?, ?)";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare link lecturer course failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, lecturer_id);
    sqlite3_bind_int(stmt, 2, course_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Step link lecturer course failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t db_link_lecturer_course_by_uuid(const char* lecturer_uuid, int course_id) {
    if (!s_initialized || !lecturer_uuid) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    /* Resolve lecturer_id from uuid */
    sqlite3_stmt *stmt;
    int lecturer_id = 0;
    int rc = sqlite3_prepare_v2(s_db, "SELECT id FROM users WHERE uuid = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, lecturer_uuid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            lecturer_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (lecturer_id == 0) {
        ESP_LOGW(TAG, "db_link_lecturer_course_by_uuid: lecturer with uuid %s not found", lecturer_uuid);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Insert into lecturer_courses */
    const char *sql = "INSERT OR IGNORE INTO lecturer_courses (lecturer_id, course_id) VALUES (?, ?)";
    rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare link lecturer course by UUID failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, lecturer_id);
    sqlite3_bind_int(stmt, 2, course_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Step link lecturer course by UUID failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Linked lecturer_id=%d to course_id=%d", lecturer_id, course_id);
    return ESP_OK;
}

esp_err_t db_insert_or_get_course(const char* code, const char* name, int* out_id) {
    if (!s_initialized || !code || !name || !out_id) return ESP_ERR_INVALID_ARG;

    /* DI-2: Normalise course code — strip leading/trailing whitespace and convert to uppercase.
     * This prevents split attendance records caused by variants such as "MTE 534" vs "MTE534". */
    char norm_code[32] = {0};
    {
        const char *p = code;
        while (*p == ' ' || *p == '\t') p++;       /* skip leading whitespace */
        const char *end = code + strlen(code) - 1;
        while (end > p && (*end == ' ' || *end == '\t')) end--;
        int len = (int)(end - p + 1);
        if (len >= (int)sizeof(norm_code)) len = (int)sizeof(norm_code) - 1;
        for (int i = 0; i < len; i++) {
            norm_code[i] = (char)toupper((unsigned char)p[i]);
        }
        norm_code[len] = '\0';
    }

    DB_LOCK();

    /* First try to select the course by normalised code */
    sqlite3_stmt *stmt;
    const char *sel_sql = "SELECT id FROM courses WHERE code = ?";
    int rc = sqlite3_prepare_v2(s_db, sel_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, norm_code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *out_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            DB_UNLOCK();
            return ESP_OK;
        }
        sqlite3_finalize(stmt);
    }

    /* Course doesn't exist — insert with normalised code */
    const char *ins_sql = "INSERT INTO courses (uuid, name, code) VALUES (lower(hex(randomblob(16))), ?, ?)";
    rc = sqlite3_prepare_v2(s_db, ins_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare insert course failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, name,      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, norm_code, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        *out_id = (int)sqlite3_last_insert_rowid(s_db);
        ESP_LOGI(TAG, "Inserted course '%s' (normalised from '%s') id=%d", norm_code, code, *out_id);
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Insert course failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t db_get_lecturer_by_phone(const char* phone, user_t* out) {
    if (!s_initialized || !phone || !out) return ESP_ERR_INVALID_ARG;
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at FROM users WHERE phone_number = ? AND role = 'lecturer'";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) { DB_UNLOCK(); return ESP_FAIL; }

    sqlite3_bind_text(stmt, 1, phone, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        memset(out, 0, sizeof(*out));
        out->id = sqlite3_column_int(stmt, 0);
        const char *uuid = (const char*)sqlite3_column_text(stmt, 1);
        if (uuid) strncpy(out->uuid, uuid, sizeof(out->uuid)-1);
        const char *name = (const char*)sqlite3_column_text(stmt, 2);
        if (name) strncpy(out->name, name, sizeof(out->name)-1);
        const char *student_id = (const char*)sqlite3_column_text(stmt, 3);
        if (student_id) strncpy(out->student_id, student_id, sizeof(out->student_id)-1);
        const char *phone_val = (const char*)sqlite3_column_text(stmt, 4);
        if (phone_val) strncpy(out->phone_number, phone_val, sizeof(out->phone_number)-1);
        const char *telegram = (const char*)sqlite3_column_text(stmt, 5);
        if (telegram) strncpy(out->telegram_id, telegram, sizeof(out->telegram_id)-1);
        const char *role = (const char*)sqlite3_column_text(stmt, 6);
        if (role) strncpy(out->role, role, sizeof(out->role)-1);
        const void *blob = sqlite3_column_blob(stmt, 7);
        if (blob) memcpy(out->embedding.values, blob, sizeof(out->embedding.values));
        out->created_at = sqlite3_column_int(stmt, 8);
        out->updated_at = sqlite3_column_int(stmt, 9);
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_OK;
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t db_insert_schedule_from_bot(int course_id, int64_t start_ts, int64_t end_ts) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    DB_LOCK();
    
    // Check if schedule already exists to prevent duplication
    sqlite3_stmt *check_stmt;
    const char *check_sql = "SELECT 1 FROM schedule WHERE course_id = ? AND start_time = ? AND end_time = ? LIMIT 1";
    int rc = sqlite3_prepare_v2(s_db, check_sql, -1, &check_stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int(check_stmt, 1, course_id);
        sqlite3_bind_int64(check_stmt, 2, start_ts);
        sqlite3_bind_int64(check_stmt, 3, end_ts);
        int step_rc = sqlite3_step(check_stmt);
        if (step_rc == SQLITE_ROW) {
            sqlite3_finalize(check_stmt);
            DB_UNLOCK();
            ESP_LOGI(TAG, "Schedule already exists (course_id=%d, start=%lld), skipping insert", course_id, (long long)start_ts);
            return ESP_OK;
        } else {
            ESP_LOGI(TAG, "Duplicate check: no duplicate found for course=%d, start=%lld, end=%lld (step_rc=%d)", 
                     course_id, (long long)start_ts, (long long)end_ts, step_rc);
        }
        sqlite3_finalize(check_stmt);
    } else {
        ESP_LOGE(TAG, "Prepare duplicate check failed: %s", sqlite3_errmsg(s_db));
    }

    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO schedule (uuid, course_id, start_time, end_time, created_at) VALUES (lower(hex(randomblob(16))), ?, ?, ?, strftime('%s','now'))";
    rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare insert schedule failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, course_id);
    sqlite3_bind_int64(stmt, 2, start_ts);
    sqlite3_bind_int64(stmt, 3, end_ts);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "Step insert schedule failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ─── Student Course Enrollment (via Telegram bot) ───────────────────────── */

/**
 * @brief Enroll a user in a course. Called when the cloud sync task pulls
 *        /api/get_course_enrollments from the Telegram bot backend.
 *
 * @param user_uuid  UUID of the enrolled user (synced from the bot's user record)
 * @param course_code Course code string (e.g. "CS480")
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if user or course doesn't exist
 */
esp_err_t db_link_user_course(const char* user_uuid, const char* course_code) {
    if (!s_initialized || !user_uuid || !course_code) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    /* Resolve user_id from uuid */
    sqlite3_stmt *stmt;
    int user_id = 0;
    int rc = sqlite3_prepare_v2(s_db,
        "SELECT id FROM users WHERE uuid = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, user_uuid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    if (user_id == 0) {
        ESP_LOGW(TAG, "db_link_user_course: user %s not found", user_uuid);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Resolve course_id from code */
    int course_id = 0;
    rc = sqlite3_prepare_v2(s_db,
        "SELECT id FROM courses WHERE code = ?", -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, course_code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            course_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    if (course_id == 0) {
        ESP_LOGW(TAG, "db_link_user_course: course %s not found", course_code);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Upsert into user_courses */
    rc = sqlite3_prepare_v2(s_db,
        "INSERT OR IGNORE INTO user_courses (user_id, course_id, enrolled_by) VALUES (?, ?, 'telegram')",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { DB_UNLOCK(); return ESP_FAIL; }
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, course_id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "db_link_user_course insert failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "User %s enrolled in course %s", user_uuid, course_code);
    return ESP_OK;
}

/**
 * @brief Get a list of course codes a user is enrolled in.
 *        Caller must free() the returned array and each string.
 *
 * @param user_id    Local DB user ID
 * @param codes_out  Pointer to char** that receives the array
 * @param count_out  Number of elements written
 * @return ESP_OK or ESP_ERR_NOT_FOUND if no enrollments
 */
esp_err_t db_get_user_courses(uint32_t user_id, char*** codes_out, int* count_out) {
    if (!s_initialized || !codes_out || !count_out) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(s_db,
        "SELECT c.code FROM courses c "
        "JOIN user_courses uc ON c.id = uc.course_id "
        "WHERE uc.user_id = ?",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) { DB_UNLOCK(); return ESP_FAIL; }
    sqlite3_bind_int(stmt, 1, (int)user_id);

    int max = 32;
    char **list = (char**)malloc(sizeof(char*) * max);
    int cnt = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && cnt < max) {
        const unsigned char *code = sqlite3_column_text(stmt, 0);
        list[cnt++] = strdup(code ? (const char*)code : "");
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();

    *codes_out  = list;
    *count_out  = cnt;
    return (cnt > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool db_student_id_exists(const char* student_id) {
    if (!s_initialized || !student_id || student_id[0] == '\0') return false;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM users WHERE student_id = ?";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare failed in db_student_id_exists: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return false;
    }

    sqlite3_bind_text(stmt, 1, student_id, -1, SQLITE_STATIC);

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        if (count > 0) {
            exists = true;
        }
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return exists;
}

esp_err_t db_get_future_schedules(db_schedule_t **schedules, int *count) {
    if (!s_initialized || !schedules || !count) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    time_t now = time(NULL);
    sqlite3_stmt *stmt;
    
    const char *sql = "SELECT s.id, c.code, c.name, s.start_time, s.end_time, u.name "
                      "FROM schedule s "
                      "JOIN courses c ON s.course_id = c.id "
                      "LEFT JOIN lecturer_courses lc ON c.id = lc.course_id "
                      "LEFT JOIN users u ON lc.lecturer_id = u.id AND u.role = 'lecturer' "
                      "WHERE s.end_time > ? "
                      "GROUP BY s.id "
                      "ORDER BY s.start_time ASC";
                      
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare select future schedules failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    sqlite3_bind_int(stmt, 1, (int)now);

    /* Count rows first */
    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) (*count)++;
    sqlite3_reset(stmt);

    if (*count == 0) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        *schedules = NULL;
        return ESP_OK;
    }

    *schedules = (db_schedule_t *)heap_caps_malloc((*count) * sizeof(db_schedule_t), MALLOC_CAP_SPIRAM);
    if (!*schedules) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    int idx = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && idx < *count) {
        db_schedule_t *s = &(*schedules)[idx];
        memset(s, 0, sizeof(*s));
        
        s->id = sqlite3_column_int(stmt, 0);
        
        const char *code = (const char*)sqlite3_column_text(stmt, 1);
        if (code) strncpy(s->course_code, code, sizeof(s->course_code) - 1);
        
        const char *name = (const char*)sqlite3_column_text(stmt, 2);
        if (name) strncpy(s->course_name, name, sizeof(s->course_name) - 1);
        
        s->start_time = sqlite3_column_int(stmt, 3);
        s->end_time = sqlite3_column_int(stmt, 4);
        
        const char *lecturer = (const char*)sqlite3_column_text(stmt, 5);
        if (lecturer) {
            strncpy(s->lecturer_name, lecturer, sizeof(s->lecturer_name) - 1);
        } else {
            strcpy(s->lecturer_name, "Staff");
        }
        
        idx++;
    }
    
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ESP_OK;
}

esp_err_t db_dump_schedules(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, course_id, start_time, end_time, uuid FROM schedule";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare dump schedules failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "=== DEVICE SCHEDULES TABLE DUMP ===");
    int row_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        row_count++;
        int id = sqlite3_column_int(stmt, 0);
        int course_id = sqlite3_column_int(stmt, 1);
        int start_time = sqlite3_column_int(stmt, 2);
        int end_time = sqlite3_column_int(stmt, 3);
        const char* uuid = (const char*)sqlite3_column_text(stmt, 4);
        ESP_LOGI(TAG, "  [%d] ID: %d | Course ID: %d | Start: %d | End: %d | UUID: %s",
                 row_count, id, course_id, start_time, end_time, uuid ? uuid : "NULL");
    }
    sqlite3_finalize(stmt);
    DB_UNLOCK();
    ESP_LOGI(TAG, "Total schedules in table: %d", row_count);
    return ESP_OK;
}

esp_err_t db_debug_dump_tables(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    DB_LOCK();
    
    // Dump users
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(s_db, "SELECT id, name, student_id, role FROM users", -1, &stmt, NULL) == SQLITE_OK) {
        ESP_LOGI(TAG, "=== DEBUG DUMP USERS ===");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ESP_LOGI(TAG, "  User: ID=%d, Name=%s, SID=%s, Role=%s",
                     sqlite3_column_int(stmt, 0),
                     sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "NULL",
                     sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "NULL",
                     sqlite3_column_text(stmt, 3) ? (const char*)sqlite3_column_text(stmt, 3) : "NULL");
        }
        sqlite3_finalize(stmt);
    }

    // Dump courses
    if (sqlite3_prepare_v2(s_db, "SELECT id, code, name FROM courses", -1, &stmt, NULL) == SQLITE_OK) {
        ESP_LOGI(TAG, "=== DEBUG DUMP COURSES ===");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ESP_LOGI(TAG, "  Course: ID=%d, Code=%s, Name=%s",
                     sqlite3_column_int(stmt, 0),
                     sqlite3_column_text(stmt, 1) ? (const char*)sqlite3_column_text(stmt, 1) : "NULL",
                     sqlite3_column_text(stmt, 2) ? (const char*)sqlite3_column_text(stmt, 2) : "NULL");
        }
        sqlite3_finalize(stmt);
    }

    // Dump user_courses
    if (sqlite3_prepare_v2(s_db, "SELECT user_id, course_id FROM user_courses", -1, &stmt, NULL) == SQLITE_OK) {
        ESP_LOGI(TAG, "=== DEBUG DUMP USER_COURSES ===");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ESP_LOGI(TAG, "  Enrollment: UserID=%d, CourseID=%d",
                     sqlite3_column_int(stmt, 0),
                     sqlite3_column_int(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }

    // Dump attendance logs
    if (sqlite3_prepare_v2(s_db, "SELECT id, user_id, schedule_id, timestamp, status FROM attendance", -1, &stmt, NULL) == SQLITE_OK) {
        ESP_LOGI(TAG, "=== DEBUG DUMP ATTENDANCE ===");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ESP_LOGI(TAG, "  Log: ID=%d, UserID=%d, SchedID=%d, TS=%d, Status=%s",
                     sqlite3_column_int(stmt, 0),
                     sqlite3_column_int(stmt, 1),
                     sqlite3_column_int(stmt, 2),
                     sqlite3_column_int(stmt, 3),
                     sqlite3_column_text(stmt, 4) ? (const char*)sqlite3_column_text(stmt, 4) : "NULL");
        }
        sqlite3_finalize(stmt);
    }
    
    DB_UNLOCK();
    return ESP_OK;
}

void db_debug_print_report_file(void) {
    FILE *f = fopen("/sdcard/attendance_report.csv", "r");
    if (f) {
        ESP_LOGI("DEBUG_CSV", "=== CONTENT OF /sdcard/attendance_report.csv ===");
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            ESP_LOGI("DEBUG_CSV", "%s", line);
        }
        fclose(f);
    } else {
        ESP_LOGE("DEBUG_CSV", "Failed to open /sdcard/attendance_report.csv (might not have been exported yet)");
    }
}

static esp_err_t db_deduplicate_schedules(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    DB_LOCK();
    
    sqlite3_stmt *stmt;
    // Order by course_id, start_time, end_time so duplicates are contiguous
    const char *sql = "SELECT id, course_id, start_time, end_time FROM schedule ORDER BY course_id ASC, start_time ASC, end_time ASC, id ASC";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare deduplicate select failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    
    // We will keep track of the last seen unique combination
    int last_course_id = -1;
    int64_t last_start = -1;
    int64_t last_end = -1;
    
    // We can collect IDs to delete
    int delete_ids[128];
    int delete_count = 0;
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        int course_id = sqlite3_column_int(stmt, 1);
        int64_t start = sqlite3_column_int64(stmt, 2);
        int64_t end = sqlite3_column_int64(stmt, 3);
        
        if (course_id == last_course_id && start == last_start && end == last_end) {
            // This is a duplicate!
            if (delete_count < 128) {
                delete_ids[delete_count++] = id;
            }
        } else {
            // New unique schedule
            last_course_id = course_id;
            last_start = start;
            last_end = end;
        }
    }
    sqlite3_finalize(stmt);
    
    // Now execute delete queries for duplicate IDs
    if (delete_count > 0) {
        ESP_LOGI(TAG, "Found %d duplicate schedules to clean up", delete_count);
        for (int i = 0; i < delete_count; i++) {
            sqlite3_stmt *del_stmt;
            const char *del_sql = "DELETE FROM schedule WHERE id = ?";
            if (sqlite3_prepare_v2(s_db, del_sql, -1, &del_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_int(del_stmt, 1, delete_ids[i]);
                sqlite3_step(del_stmt);
                sqlite3_finalize(del_stmt);
                ESP_LOGI(TAG, "Deleted duplicate schedule row ID: %d", delete_ids[i]);
            }
        }
    } else {
        ESP_LOGI(TAG, "No duplicate schedules found in database");
    }
    
    DB_UNLOCK();
    return ESP_OK;
}

/* ==================== Enrollment / Lecturer-Course Unlink ==================== */

esp_err_t db_unlink_user_course(const char* user_uuid, const char* course_code) {
    if (!s_initialized || !user_uuid || !course_code) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    /* Resolve user_id */
    sqlite3_stmt *stmt;
    int user_id = 0;
    if (sqlite3_prepare_v2(s_db, "SELECT id FROM users WHERE uuid = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, user_uuid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) user_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (user_id == 0) {
        ESP_LOGW(TAG, "db_unlink_user_course: user %s not found", user_uuid);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Resolve course_id */
    int course_id = 0;
    if (sqlite3_prepare_v2(s_db, "SELECT id FROM courses WHERE code = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, course_code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) course_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (course_id == 0) {
        ESP_LOGW(TAG, "db_unlink_user_course: course %s not found", course_code);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    int rc = SQLITE_DONE;
    if (sqlite3_prepare_v2(s_db,
            "DELETE FROM user_courses WHERE user_id = ? AND course_id = ?",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_int(stmt, 2, course_id);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "db_unlink_user_course failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Unenrolled user %s from course %s", user_uuid, course_code);
    return ESP_OK;
}

esp_err_t db_unlink_lecturer_course_by_uuid(const char* lecturer_uuid, const char* course_code) {
    if (!s_initialized || !lecturer_uuid || !course_code) return ESP_ERR_INVALID_ARG;
    DB_LOCK();

    /* Resolve lecturer_id */
    sqlite3_stmt *stmt;
    int lecturer_id = 0;
    if (sqlite3_prepare_v2(s_db, "SELECT id FROM users WHERE uuid = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, lecturer_uuid, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) lecturer_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (lecturer_id == 0) {
        ESP_LOGW(TAG, "db_unlink_lecturer_course_by_uuid: lecturer %s not found", lecturer_uuid);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    /* Resolve course_id */
    int course_id = 0;
    if (sqlite3_prepare_v2(s_db, "SELECT id FROM courses WHERE code = ?", -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, course_code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) course_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (course_id == 0) {
        ESP_LOGW(TAG, "db_unlink_lecturer_course_by_uuid: course %s not found", course_code);
        DB_UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    int rc = SQLITE_DONE;
    if (sqlite3_prepare_v2(s_db,
            "DELETE FROM lecturer_courses WHERE lecturer_id = ? AND course_id = ?",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, lecturer_id);
        sqlite3_bind_int(stmt, 2, course_id);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    DB_UNLOCK();

    if (rc != SQLITE_DONE) {
        ESP_LOGE(TAG, "db_unlink_lecturer_course_by_uuid failed: %s", sqlite3_errmsg(s_db));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Unlinked lecturer %s from course %s", lecturer_uuid, course_code);
    return ESP_OK;
}

int db_get_today_attendance_count(void) {
    if (!s_initialized) return 0;
    DB_LOCK();
    int count = 0;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM attendance WHERE date(timestamp, 'unixepoch', 'localtime') = date('now', 'localtime')";
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    DB_UNLOCK();
    return count;
}

bool db_is_test_or_exam_schedule(uint32_t schedule_id) {
    if (!s_initialized || schedule_id == 0) return false;
    DB_LOCK();
    bool result = false;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COALESCE(location, '') FROM schedule WHERE id = ?";
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, schedule_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *loc = (const char*)sqlite3_column_text(stmt, 0);
            if (loc && (strcasestr(loc, "Test") || strcasestr(loc, "Exam"))) {
                result = true;
            }
        }
        sqlite3_finalize(stmt);
    }
    DB_UNLOCK();
    return result;
}

bool db_attendance_exists_for_schedule(uint32_t user_id, uint32_t schedule_id) {
    if (!s_initialized || schedule_id == 0) return false;
    
    bool is_test_or_exam = db_is_test_or_exam_schedule(schedule_id);
    
    DB_LOCK();
    bool exists = false;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM attendance WHERE user_id = ? AND schedule_id = ?";
    if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, (int)user_id);
        sqlite3_bind_int(stmt, 2, (int)schedule_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (is_test_or_exam) {
                exists = (count >= 2); /* Allow up to 2 entries for Test/Exam (check-in/check-out) */
            } else {
                exists = (count >= 1); /* Standard class check-in only */
            }
        }
        sqlite3_finalize(stmt);
    }
    DB_UNLOCK();
    return exists;
}

esp_err_t db_get_active_schedule(db_schedule_t *out_schedule) {
    if (!s_initialized || !out_schedule) return ESP_ERR_INVALID_ARG;
    DB_LOCK();
    memset(out_schedule, 0, sizeof(db_schedule_t));

    time_t now = time(NULL);
    sqlite3_stmt *stmt;
    const char *sql = "SELECT c.code, c.name, u.name, s.start_time, s.end_time "
                      "FROM schedule s "
                      "JOIN courses c ON s.course_id = c.id "
                      "LEFT JOIN lecturer_courses lc ON c.id = lc.course_id "
                      "LEFT JOIN users u ON lc.lecturer_id = u.id AND u.role = 'lecturer' "
                      "WHERE s.start_time <= ? AND s.end_time >= ? "
                      "GROUP BY s.id "
                      "ORDER BY s.start_time ASC LIMIT 1";

    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "db_get_active_schedule prepare failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    sqlite3_bind_int(stmt, 1, (int)now);
    sqlite3_bind_int(stmt, 2, (int)now);

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *code = (const char*)sqlite3_column_text(stmt, 0);
        if (code) strncpy(out_schedule->course_code, code, sizeof(out_schedule->course_code) - 1);
        const char *cname = (const char*)sqlite3_column_text(stmt, 1);
        if (cname) strncpy(out_schedule->course_name, cname, sizeof(out_schedule->course_name) - 1);
        const char *lname = (const char*)sqlite3_column_text(stmt, 2);
        if (lname) strncpy(out_schedule->lecturer_name, lname, sizeof(out_schedule->lecturer_name) - 1);
        else strncpy(out_schedule->lecturer_name, "Staff", sizeof(out_schedule->lecturer_name) - 1);
        out_schedule->start_time = (uint32_t)sqlite3_column_int(stmt, 3);
        out_schedule->end_time   = (uint32_t)sqlite3_column_int(stmt, 4);
        ret = ESP_OK;
        ESP_LOGI(TAG, "Active schedule: %s - %s", out_schedule->course_code, out_schedule->course_name);
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();
    return ret;
}
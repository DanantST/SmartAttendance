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
    "    updated_at INTEGER"
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

    ESP_LOGI(TAG, "Database initialized");
    return ESP_OK;
}

esp_err_t db_insert_user(user_t *user) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!user) return ESP_ERR_INVALID_ARG;

    DB_LOCK();
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO users (uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
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
        if (user->embedding.values[i] != 0.0f) {
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
    const char *sql = "SELECT id, uuid, name, student_id, phone_number, telegram_id, role, face_embedding, created_at, updated_at FROM users";
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
    const char *sql = "SELECT id FROM schedule WHERE start_time <= ? AND end_time >= ? LIMIT 1";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        DB_UNLOCK();
        return 0;
    }
    
    sqlite3_bind_int(stmt, 1, (int)now);
    sqlite3_bind_int(stmt, 2, (int)now);
    
    uint32_t schedule_id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        schedule_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
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

esp_err_t db_get_attendance_report(char **report_str, int course_id, int date_timestamp) {
    if (!s_initialized || !report_str) return ESP_ERR_INVALID_STATE;

    /* Issue 6.5: For now, we return the last 100 records globally. 
     * You can add WHERE clauses for course_id and date_timestamp later if needed. */
    const char *sql = "SELECT u.student_id, u.name, a.status, time(a.timestamp, 'unixepoch', 'localtime') "
                      "FROM attendance a "
                      "JOIN users u ON a.user_id = u.id "
                      "ORDER BY a.timestamp DESC LIMIT 100;";

    DB_LOCK();
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Failed to prepare report query: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }

    /* Allocate buffer for the CSV string */
    size_t buf_size = 4096;
    *report_str = malloc(buf_size);
    if (!*report_str) {
        sqlite3_finalize(stmt);
        DB_UNLOCK();
        return ESP_ERR_NO_MEM;
    }

    strcpy(*report_str, "Student ID,Name,Status,Time\n");
    size_t len = strlen(*report_str);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sid = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *status = (const char *)sqlite3_column_text(stmt, 2);
        const char *time_str = (const char *)sqlite3_column_text(stmt, 3);

        char line[128];
        snprintf(line, sizeof(line), "%s,%s,%s,%s\n", 
                 sid ? sid : "N/A", 
                 name ? name : "Unknown", 
                 status ? status : "N/A", 
                 time_str ? time_str : "N/A");

        if (len + strlen(line) < buf_size - 1) {
            strcat(*report_str, line);
            len += strlen(line);
        }
    }

    sqlite3_finalize(stmt);
    DB_UNLOCK();

    if (len == strlen("Student ID,Name,Status,Time\n")) {
        strcat(*report_str, "No records found,\n");
    }

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

esp_err_t db_insert_or_get_course(const char* code, const char* name, int* out_id) {
    if (!s_initialized || !code || !name || !out_id) return ESP_ERR_INVALID_ARG;
    DB_LOCK();
    
    // First try to select the course
    sqlite3_stmt *stmt;
    const char *sel_sql = "SELECT id FROM courses WHERE code = ?";
    int rc = sqlite3_prepare_v2(s_db, sel_sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *out_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            DB_UNLOCK();
            return ESP_OK;
        }
        sqlite3_finalize(stmt);
    }
    
    // Course doesn't exist, insert it
    const char *ins_sql = "INSERT INTO courses (uuid, name, code) VALUES (lower(hex(randomblob(16))), ?, ?)";
    rc = sqlite3_prepare_v2(s_db, ins_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare insert course failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, code, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        *out_id = (int)sqlite3_last_insert_rowid(s_db);
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
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO schedule (uuid, course_id, start_time, end_time, created_at) VALUES (lower(hex(randomblob(16))), ?, ?, ?, strftime('%s','now'))";
    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Prepare insert schedule failed: %s", sqlite3_errmsg(s_db));
        DB_UNLOCK();
        return ESP_FAIL;
    }
    sqlite3_bind_int(stmt, 1, course_id);
    sqlite3_bind_int(stmt, 2, (int)start_ts);
    sqlite3_bind_int(stmt, 3, (int)end_ts);
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

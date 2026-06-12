/**
 * @file cloud_sync.c
 * @brief Cloud sync implementation using esp_http_client
 * 
 * Implements HTTPS client with certificate verification and
 * JSON payload handling for attendance data sync [citation:3][citation:6]
 */

#include "cloud_sync.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "database/db_manager.h"
#include "network/ota_manager.h"
#include "recognition/recognizer.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include <string.h>
#include <inttypes.h>
#include <time.h>

static const char* TAG = "CLOUD_SYNC";

/* Sync state */
static sync_status_t s_sync_status = SYNC_STATUS_IDLE;
static time_t s_last_sync_timestamp = 0;
static SemaphoreHandle_t s_sync_mutex = NULL;
static char s_api_endpoint[256] = {0};
static char s_api_token[128] = {0};
static char s_chat_id[128] = {0};
static TaskHandle_t s_sync_task_handle = NULL;

#define TELEGRAM_API_BASE "https://api.telegram.org/bot"
#define EXPORT_FILE_PATH "/sdcard/attendance_export.csv"

/* Forward declarations */
static void cloud_sync_task(void* param);
static esp_err_t sync_schedule(void);
static esp_err_t sync_attendance_logs(void);
static esp_err_t sync_deletions(void);
static esp_err_t sync_users(void);
static esp_err_t sync_enrollments(void);
static char* build_sync_payload(void);
static int parse_schedule_response(const char* response, size_t len);

/* ==================== Public API ==================== */

esp_err_t cloud_sync_init(void) {
    ESP_LOGI(TAG, "Initializing cloud sync module");
    
    s_sync_mutex = xSemaphoreCreateMutex();
    if (!s_sync_mutex) {
        ESP_LOGE(TAG, "Failed to create sync mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* Load saved sync state from database */
    /* In production, load from sync_state table */
    s_last_sync_timestamp = 0;
    
    nvs_handle_t nvs_tel;
    if (nvs_open("telegram", NVS_READONLY, &nvs_tel) == ESP_OK) {
        size_t len = sizeof(s_api_token);
        nvs_get_str(nvs_tel, "token", s_api_token, &len);
        len = sizeof(s_chat_id);
        nvs_get_str(nvs_tel, "chat_id", s_chat_id, &len);
        len = sizeof(s_api_endpoint);
        nvs_get_str(nvs_tel, "endpoint", s_api_endpoint, &len);
        nvs_close(nvs_tel);
    }
    
    ESP_LOGI(TAG, "Cloud sync initialized");
    return ESP_OK;
}

esp_err_t cloud_sync_start(void) {
    if (s_sync_status == SYNC_STATUS_IN_PROGRESS) {
        ESP_LOGW(TAG, "Sync already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (strlen(s_api_token) == 0 || strlen(s_chat_id) == 0) {
        ESP_LOGE(TAG, "Telegram credentials not configured in NVS");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_sync_task_handle != NULL) {
        ESP_LOGW(TAG, "Sync task already running via handle guard");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting cloud sync");
    
    /* Create sync task */
    xTaskCreate(cloud_sync_task, "cloud_sync", TASK_NETWORK_STACK_SIZE, NULL,
                TASK_NETWORK_PRIORITY, &s_sync_task_handle);
    
    return ESP_OK;
}

sync_status_t cloud_sync_get_status(void) {
    return s_sync_status;
}

void cloud_sync_set_endpoint(const char* url) {
    if (url) {
        strncpy(s_api_endpoint, url, sizeof(s_api_endpoint) - 1);
        ESP_LOGI(TAG, "API endpoint set to: %s", s_api_endpoint);
    }
}

void cloud_sync_set_token(const char* token) {
    if (token) {
        strncpy(s_api_token, token, sizeof(s_api_token) - 1);
        ESP_LOGI(TAG, "API token set");
    }
}

time_t cloud_sync_get_last_timestamp(void) {
    return s_last_sync_timestamp;
}

/* ==================== Sync Task ==================== */

static void cloud_sync_task(void* param) {
    s_sync_status = SYNC_STATUS_IN_PROGRESS;
    esp_err_t overall_result = ESP_OK;
    
    /* Step 1: Download schedule updates */
    esp_err_t ret = sync_schedule();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Schedule sync failed: %d", ret);
        overall_result = ret;
    }
    
    /* Step 1.5: Download user deletions */
    ret = sync_deletions();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Deletion sync failed: %d", ret);
        overall_result = ret;
    }
    
    /* Step 2: Download user updates */
    ret = sync_users();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "User sync failed: %d", ret);
        overall_result = ret;
    }
    
    /* Step 2.5: Download course enrollment updates */
    ret = sync_enrollments();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enrollment sync failed: %d", ret);
        overall_result = ret;
    }
    
    /* Step 3: Upload attendance logs */
    ret = sync_attendance_logs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Attendance upload failed: %d", ret);
        overall_result = ret;
    }
    
    /* Update sync status */
    if (overall_result == ESP_OK) {
        s_sync_status = SYNC_STATUS_SUCCESS;
        s_last_sync_timestamp = time(NULL);
        ESP_LOGI(TAG, "Cloud sync completed successfully");
    } else {
        s_sync_status = SYNC_STATUS_FAILED;
        ESP_LOGE(TAG, "Cloud sync failed");
    }

    /* [Fix M2] Signal network_sync_task that we are done so it can unblock
     * without relying on a hardcoded vTaskDelay. The event group is owned by
     * main.c; we access it through the extern declaration in cloud_sync.h. */
    extern EventGroupHandle_t g_system_event_group;
#define SYSTEM_EVENT_CLOUD_SYNC_DONE (1 << 9)
    if (g_system_event_group) {
        xEventGroupSetBits(g_system_event_group, SYSTEM_EVENT_CLOUD_SYNC_DONE);
    }
    
    s_sync_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ==================== HTTP Client Helper ==================== */

/* Issue 5.2: Event handler to capture response body during perform() */
typedef struct {
    char *buffer;
    size_t len;
    size_t capacity;
} http_response_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_response_ctx_t *ctx = (http_response_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (ctx->buffer == NULL) {
                ctx->capacity = 1024;
                /* [Fix C3] Allocate capacity+1 bytes so the null terminator written at
                 * buffer[len] always fits, even when the response exactly fills capacity. */
                ctx->buffer = (char *)malloc(ctx->capacity + 1);
                ctx->len = 0;
            }
            if (ctx->buffer && (ctx->len + evt->data_len + 1 <= ctx->capacity)) {
                memcpy(ctx->buffer + ctx->len, evt->data, evt->data_len);
                ctx->len += evt->data_len;
            } else if (ctx->buffer) {
                /* Grow buffer — always allocate +1 extra for the null terminator */
                size_t new_cap = ctx->capacity * 2 + evt->data_len;
                char *new_buf = (char *)realloc(ctx->buffer, new_cap + 1);
                if (new_buf) {
                    ctx->buffer = new_buf;
                    ctx->capacity = new_cap;
                    memcpy(ctx->buffer + ctx->len, evt->data, evt->data_len);
                    ctx->len += evt->data_len;
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static esp_err_t http_request(const char* url, const char* method,
                               const char* post_data, char** response,
                               size_t* response_len) {
    http_response_ctx_t resp_ctx = { .buffer = NULL, .len = 0, .capacity = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .method = (strcmp(method, "POST") == 0) ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = CLOUD_API_TIMEOUT_MS,
        .skip_cert_common_name_check = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &resp_ctx,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_ERR_NO_MEM;
    
    /* Set authentication header */
    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", s_api_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    /* Set POST data if provided */
    if (post_data && strlen(post_data) > 0) {
        esp_http_client_set_post_field(client, post_data, strlen(post_data));
    }
    
    /* Perform request — response body captured via event handler */
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200 || status_code == 201) {
            if (resp_ctx.buffer && resp_ctx.len > 0) {
                resp_ctx.buffer[resp_ctx.len] = '\0';
                *response = resp_ctx.buffer;
                *response_len = resp_ctx.len;
                resp_ctx.buffer = NULL;  /* ownership transferred */
            }
        } else {
            ESP_LOGE(TAG, "HTTP error: %d", status_code);
            err = ESP_ERR_HTTP_BASE + status_code;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %d", err);
    }
    
    /* Free response buffer if not transferred */
    if (resp_ctx.buffer) free(resp_ctx.buffer);
    
    esp_http_client_cleanup(client);
    return err;
}

/* ==================== Telegram Sync Operations ==================== */

static esp_err_t sync_schedule(void) {
    if (strlen(s_api_endpoint) == 0) {
        ESP_LOGW(TAG, "Server endpoint URL not configured. Skipping schedule sync.");
        return ESP_OK;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/get_schedules?since=%ld", s_api_endpoint, (long)s_last_sync_timestamp);
    
    ESP_LOGI(TAG, "Fetching schedule updates from %s", url);
    
    char* response = NULL;
    size_t response_len = 0;
    esp_err_t err = http_request(url, "GET", NULL, &response, &response_len);
    
    if (err == ESP_OK && response) {
        cJSON *root = cJSON_Parse(response);
        if (root && cJSON_IsArray(root)) {
            int size = cJSON_GetArraySize(root);
            ESP_LOGI(TAG, "Parsed %d new schedules from cloud", size);
            for (int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(root, i);
                cJSON *code_json = cJSON_GetObjectItem(item, "course_code");
                cJSON *title_json = cJSON_GetObjectItem(item, "course_title");
                cJSON *start_json = cJSON_GetObjectItem(item, "start_time");
                cJSON *end_json = cJSON_GetObjectItem(item, "end_time");
                
                if (code_json && start_json && end_json) {
                    const char *code = code_json->valuestring;
                    const char *title = title_json ? title_json->valuestring : "Scheduled Course";
                    int64_t start_time = start_json->valuedouble;
                    int64_t end_time = end_json->valuedouble;
                    
                    int course_id = 0;
                    if (db_insert_or_get_course(code, title, &course_id) == ESP_OK) {
                        db_insert_schedule_from_bot(course_id, start_time, end_time);
                        ESP_LOGI(TAG, "Inserted schedule: Course ID %d, %lld to %lld", course_id, (long long)start_time, (long long)end_time);
                    }
                }
            }
        }
        if (root) cJSON_Delete(root);
        free(response);
    } else {
        ESP_LOGE(TAG, "Failed to fetch schedules from cloud (err=%d)", err);
    }
    
    return err;
}

static esp_err_t sync_attendance_logs(void) {
    /* 1. Export unsynced logs to CSV */
    if (db_get_unsynced_log_count() == 0) {
        ESP_LOGI(TAG, "No logs to sync");
        return ESP_OK;
    }
    
    if (db_export_attendance_csv(EXPORT_FILE_PATH) != ESP_OK) {
        return ESP_FAIL;
    }
    
    /* 2. Upload to Telegram */
    /* 2. Upload notification to Telegram */
    char url[512];
    snprintf(url, sizeof(url), "%s%s/sendDocument", TELEGRAM_API_BASE, s_api_token);
    
    ESP_LOGI(TAG, "Uploading logs to Telegram Chat %s", s_chat_id);
    
    FILE* f = fopen(EXPORT_FILE_PATH, "r");
    if (!f) return ESP_FAIL;
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* file_data = malloc(file_size + 1);
    if (!file_data) { fclose(f); return ESP_ERR_NO_MEM; }
    fread(file_data, 1, file_size, f);
    file_data[file_size] = '\0';
    fclose(f);

    const char* boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    char* body = malloc(file_size + 1024);
    if (!body) { free(file_data); return ESP_ERR_NO_MEM; }

    int header_len = snprintf(body, file_size + 1024,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"document\"; filename=\"attendance.csv\"\r\n"
        "Content-Type: text/csv\r\n\r\n",
        boundary, s_chat_id, boundary);

    memcpy(body + header_len, file_data, file_size);
    int footer_len = snprintf(body + header_len + file_size, 1024, "\r\n--%s--\r\n", boundary);
    size_t body_len = header_len + file_size + footer_len;

    esp_http_client_config_t config = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);
    
    esp_http_client_set_post_field(client, body, body_len);
    esp_err_t err = esp_http_client_perform(client);

    free(file_data);
    free(body);
    esp_http_client_cleanup(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Telegram sendDocument upload completed");
        /* [T5-1] Mark all uploaded logs as synced to prevent re-uploading next cycle */
        db_mark_all_logs_synced();
    } else {
        ESP_LOGE(TAG, "Telegram upload failed: %d — logs remain unsynced", err);
    }

    return err;
}

/* ==================== Sync Tasks (Placeholder stubs) ==================== */

static esp_err_t sync_users(void) {
    if (strlen(s_api_endpoint) == 0) {
        ESP_LOGW(TAG, "Server endpoint URL not configured. Skipping user sync.");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Syncing users to cloud server...");
    
    user_t *users = NULL;
    int count = 0;
    esp_err_t err = db_get_all_users(&users, &count);
    if (err != ESP_OK || count == 0) {
        ESP_LOGI(TAG, "No users found in database to sync");
        if (users) free(users);
        return ESP_OK;
    }
    
    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "uuid", users[i].uuid);
        cJSON_AddStringToObject(item, "name", users[i].name);
        cJSON_AddStringToObject(item, "student_id", users[i].student_id[0] ? users[i].student_id : "");
        cJSON_AddStringToObject(item, "phone_number", users[i].phone_number[0] ? users[i].phone_number : "");
        cJSON_AddStringToObject(item, "telegram_id", users[i].telegram_id[0] ? users[i].telegram_id : "");
        cJSON_AddStringToObject(item, "role", users[i].role);
        cJSON_AddItemToArray(root, item);
    }
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(users);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/sync_users", s_api_endpoint);
    
    char *response = NULL;
    size_t response_len = 0;
    err = http_request(url, "POST", json_str, &response, &response_len);
    free(json_str);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Users synced successfully to cloud: %s", response ? response : "No response body");
        if (response) free(response);
    } else {
        ESP_LOGE(TAG, "Failed to sync users to cloud (err=%d)", err);
    }
    
    return err;
}

static esp_err_t sync_deletions(void) {
    if (strlen(s_api_endpoint) == 0) {
        ESP_LOGW(TAG, "Server endpoint URL not configured. Skipping deletion sync.");
        return ESP_OK;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/get_deletions?since=%ld", s_api_endpoint, (long)s_last_sync_timestamp);
    
    ESP_LOGI(TAG, "Fetching deleted users from %s", url);
    
    char* response = NULL;
    size_t response_len = 0;
    esp_err_t err = http_request(url, "GET", NULL, &response, &response_len);
    
    if (err == ESP_OK && response) {
        cJSON *root = cJSON_Parse(response);
        if (root && cJSON_IsArray(root)) {
            int size = cJSON_GetArraySize(root);
            if (size > 0) {
                ESP_LOGI(TAG, "Parsed %d deletions from cloud", size);
                bool any_deleted = false;
                for (int i = 0; i < size; i++) {
                    cJSON *item = cJSON_GetArrayItem(root, i);
                    cJSON *uuid_json = cJSON_GetObjectItem(item, "uuid");
                    
                    if (uuid_json) {
                        const char *uuid = uuid_json->valuestring;
                        ESP_LOGI(TAG, "Deleting user with UUID: %s", uuid);
                        if (db_delete_user_by_uuid(uuid) == ESP_OK) {
                            any_deleted = true;
                        }
                    }
                }
                if (any_deleted) {
                    // Refresh face recognizer cache
                    recognizer_load_cache();
                }
            }
        }
        if (root) cJSON_Delete(root);
        free(response);
    } else {
        ESP_LOGE(TAG, "Failed to fetch deletions from cloud (err=%d)", err);
    }
    
    return err;
}

static esp_err_t sync_enrollments(void) {
    if (strlen(s_api_endpoint) == 0) {
        ESP_LOGW(TAG, "Server endpoint URL not configured. Skipping enrollment sync.");
        return ESP_OK;
    }
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/get_course_enrollments?since=%ld", s_api_endpoint, (long)s_last_sync_timestamp);
    
    ESP_LOGI(TAG, "Fetching course enrollment updates from %s", url);
    
    char* response = NULL;
    size_t response_len = 0;
    esp_err_t err = http_request(url, "GET", NULL, &response, &response_len);
    
    if (err == ESP_OK && response) {
        cJSON *root = cJSON_Parse(response);
        if (root && cJSON_IsArray(root)) {
            int size = cJSON_GetArraySize(root);
            ESP_LOGI(TAG, "Parsed %d new course enrollments from cloud", size);
            for (int i = 0; i < size; i++) {
                cJSON *item = cJSON_GetArrayItem(root, i);
                cJSON *uuid_json = cJSON_GetObjectItem(item, "user_uuid");
                cJSON *code_json = cJSON_GetObjectItem(item, "course_code");
                
                if (uuid_json && code_json) {
                    const char *user_uuid = uuid_json->valuestring;
                    const char *course_code = code_json->valuestring;
                    
                    db_link_user_course(user_uuid, course_code);
                }
            }
        }
        if (root) cJSON_Delete(root);
        free(response);
    } else {
        ESP_LOGE(TAG, "Failed to fetch course enrollments from cloud (err=%d)", err);
    }
    
    return err;
}

static __attribute__((unused)) char* build_sync_payload(void) {
    /* To be implemented: build JSON payload from SQLite logs */
    return NULL;
}

static __attribute__((unused)) int parse_schedule_response(const char* response, size_t len) {
    /* To be implemented: parse JSON response from server */
    return 0;
}

void cloud_sync_set_interval(uint32_t interval_ms) {
    ESP_LOGI(TAG, "Sync interval updated to %" PRIu32 " ms", interval_ms);
    /* Note: The network_sync_task in main.c reads this value from NVS. 
     * This function acts as a notification endpoint. */
}
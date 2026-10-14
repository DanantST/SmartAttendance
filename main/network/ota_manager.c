/**
 * @file ota_manager.c
 * @brief Over-The-Air (OTA) and SD-Card Firmware Update & Rollback Manager Implementation
 */

#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "esp_heap_caps.h"

static const char *TAG = "OTA_MGR";

static SemaphoreHandle_t s_ota_mutex = NULL;
static bool s_ota_busy = false;
static int s_ota_progress = -1;

typedef struct {
    char url[256];
    char version[32];
    ota_progress_cb_t progress_cb;
    ota_done_cb_t done_cb;
} download_task_param_t;

typedef struct {
    char filepath[256];
    ota_progress_cb_t progress_cb;
    ota_done_cb_t done_cb;
} flash_task_param_t;

/* ---- Internal Helper: Parse esp_app_desc_t from local .bin file ---- */
static bool parse_binary_header(const char *filepath, firmware_info_t *out_info) {
    if (!filepath || !out_info) return false;
    memset(out_info, 0, sizeof(firmware_info_t));

    FILE *f = fopen(filepath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 256) {
        fclose(f);
        return false;
    }

    /* ESP-IDF binaries have esp_image_header_t (24B) + segment_header (8B) = 32B before esp_app_desc_t */
    if (fseek(f, 32, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    esp_app_desc_t desc;
    if (fread(&desc, 1, sizeof(desc), f) != sizeof(desc)) {
        fclose(f);
        return false;
    }
    fclose(f);

    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return false;
    }

    strncpy(out_info->filepath, filepath, sizeof(out_info->filepath) - 1);
    const char *slash = strrchr(filepath, '/');
    strncpy(out_info->filename, slash ? slash + 1 : filepath, sizeof(out_info->filename) - 1);

    strncpy(out_info->version, desc.version, sizeof(out_info->version) - 1);
    strncpy(out_info->project_name, desc.project_name, sizeof(out_info->project_name) - 1);
    strncpy(out_info->compile_date, desc.date, sizeof(out_info->compile_date) - 1);
    strncpy(out_info->compile_time, desc.time, sizeof(out_info->compile_time) - 1);
    strcpy(out_info->slot_name, "SD Card");
    out_info->size_bytes = (size_t)sz;
    out_info->is_valid = true;

    /* Compare against currently running firmware SHA-256 */
    const esp_app_desc_t *running_desc = esp_app_get_description();
    if (running_desc && memcmp(running_desc->app_elf_sha256, desc.app_elf_sha256, sizeof(desc.app_elf_sha256)) == 0) {
        out_info->is_current = true;
    } else {
        out_info->is_current = false;
    }

    return true;
}

/* ==================== Public API Implementation ==================== */

esp_err_t ota_manager_init(void) {
    if (!s_ota_mutex) {
        s_ota_mutex = xSemaphoreCreateMutex();
    }

    /* Ensure firmware directory exists on SD card */
    struct stat st;
    if (stat(OTA_FIRMWARE_DIR, &st) != 0) {
        mkdir(OTA_FIRMWARE_DIR, 0775);
        ESP_LOGI(TAG, "Created firmware directory: %s", OTA_FIRMWARE_DIR);
    }
    return ESP_OK;
}

esp_err_t ota_manager_get_running_info(firmware_info_t *out_info) {
    if (!out_info) return ESP_ERR_INVALID_ARG;
    memset(out_info, 0, sizeof(firmware_info_t));

    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (desc) {
        strncpy(out_info->version, desc->version, sizeof(out_info->version) - 1);
        strncpy(out_info->project_name, desc->project_name, sizeof(out_info->project_name) - 1);
        strncpy(out_info->compile_date, desc->date, sizeof(out_info->compile_date) - 1);
        strncpy(out_info->compile_time, desc->time, sizeof(out_info->compile_time) - 1);
    }

    if (running) {
        strncpy(out_info->slot_name, running->label, sizeof(out_info->slot_name) - 1);
        out_info->size_bytes = running->size;
    } else {
        strcpy(out_info->slot_name, "Unknown");
    }

    snprintf(out_info->filename, sizeof(out_info->filename), "%s_%s.bin",
             out_info->project_name, out_info->version);
    snprintf(out_info->filepath, sizeof(out_info->filepath), "%s/%s",
             OTA_FIRMWARE_DIR, out_info->filename);

    out_info->is_current = true;
    out_info->is_valid = true;
    return ESP_OK;
}

int ota_manager_list_sd_firmwares(firmware_info_t *out_list, int max_entries) {
    if (!out_list || max_entries <= 0) return 0;

    ota_manager_init();
    DIR *dir = opendir(OTA_FIRMWARE_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open firmware directory %s", OTA_FIRMWARE_DIR);
        return 0;
    }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max_entries) {
        if (ent->d_name[0] == '.') continue;
        size_t name_len = strlen(ent->d_name);
        /* Only consider files ending with .bin (exclude .tmp files) */
        if (name_len > 4 && strcmp(ent->d_name + name_len - 4, ".bin") == 0) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", OTA_FIRMWARE_DIR, ent->d_name);
            firmware_info_t info;
            if (parse_binary_header(full_path, &info)) {
                out_list[count++] = info;
            }
        }
    }
    closedir(dir);
    ESP_LOGI(TAG, "Discovered %d valid firmware binaries on SD card", count);
    return count;
}

/* ---- Download Worker Task ---- */
static void download_task(void *pvParam) {
    download_task_param_t *params = (download_task_param_t *)pvParam;
    char target_path[256];
    char temp_path[260];

    snprintf(target_path, sizeof(target_path), "%s/SmartAttendance_%s.bin",
             OTA_FIRMWARE_DIR, params->version);
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", target_path);

    ESP_LOGI(TAG, "Starting resilient firmware download: %s -> %s", params->url, temp_path);
    if (params->progress_cb) params->progress_cb(0, "Connecting to update server...");

    const int MAX_DOWNLOAD_RETRIES = 5;
    const int MAX_CONNECT_RETRIES  = 3;
    bool download_finished = false;
    char *buffer = (char *)malloc(8192);
    char *stream_buf = (char *)heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);

    if (!buffer) {
        ESP_LOGE(TAG, "Out of memory for download buffer");
        if (params->done_cb) params->done_cb(ESP_ERR_NO_MEM, "Out of memory");
        if (stream_buf) free(stream_buf);
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    int last_reported_pct = -1;
    int total_expected = -1;

    for (int retry = 0; retry < MAX_DOWNLOAD_RETRIES && !download_finished; retry++) {
        /* Check existing downloaded size for resume */
        struct stat st;
        int existing_bytes = 0;
        if (stat(temp_path, &st) == 0 && st.st_size > 0) {
            existing_bytes = (int)st.st_size;
            ESP_LOGI(TAG, "Resuming download from byte offset %d (attempt %d/%d)",
                     existing_bytes, retry + 1, MAX_DOWNLOAD_RETRIES);
        }

        esp_http_client_config_t config = {
            .url = params->url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 30000, /* 30-second timeout for large transfers over SDIO */
            .keep_alive_enable = true,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            ESP_LOGE(TAG, "HTTP client initialization failed");
            vTaskDelay(pdMS_TO_TICKS(1500));
            continue;
        }

        /* If resuming, send Range request header */
        if (existing_bytes > 0) {
            char range_hdr[64];
            snprintf(range_hdr, sizeof(range_hdr), "bytes=%d-", existing_bytes);
            esp_http_client_set_header(client, "Range", range_hdr);
        }

        /* Retry opening connection up to MAX_CONNECT_RETRIES times with backoff */
        esp_err_t err = ESP_FAIL;
        for (int conn_try = 1; conn_try <= MAX_CONNECT_RETRIES; conn_try++) {
            err = esp_http_client_open(client, 0);
            if (err == ESP_OK) break;

            ESP_LOGW(TAG, "Connection attempt %d/%d failed (%s), retrying in %d s...",
                     conn_try, MAX_CONNECT_RETRIES, esp_err_to_name(err), conn_try * 2);
            if (params->progress_cb) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Connecting to server... (try %d/%d)", conn_try, MAX_CONNECT_RETRIES);
                int current_pct = (existing_bytes > 0 && total_expected > 0) ? ((existing_bytes * 100) / total_expected) : 0;
                params->progress_cb(current_pct, msg);
            }
            vTaskDelay(pdMS_TO_TICKS(conn_try * 2000));
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to server after %d attempts: %s", MAX_CONNECT_RETRIES, esp_err_to_name(err));
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int chunk_content_len = esp_http_client_fetch_headers(client);
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Server HTTP status: %d, chunk content length: %d bytes", status_code, chunk_content_len);

        FILE *f = NULL;
        int current_file_bytes = 0;

        if (status_code == 206) {
            /* 206 Partial Content: Server accepted Range request, append to temp file */
            f = fopen(temp_path, "ab");
            current_file_bytes = existing_bytes;
            if (chunk_content_len > 0) {
                total_expected = existing_bytes + chunk_content_len;
            }
            ESP_LOGI(TAG, "HTTP 206 Partial Content: Appending from byte %d (expected total: %d bytes)",
                     current_file_bytes, total_expected);
        } else if (status_code == 200) {
            /* 200 OK: Full content sent, restart from beginning */
            f = fopen(temp_path, "wb");
            current_file_bytes = 0;
            total_expected = chunk_content_len;
            ESP_LOGI(TAG, "HTTP 200 OK: Downloading from byte 0 (total: %d bytes)", total_expected);
        } else if (status_code == 416) {
            /* Range not satisfiable (perhaps already fully downloaded or invalid) */
            ESP_LOGW(TAG, "HTTP 416 Range Not Satisfiable: Restarting download from byte 0");
            unlink(temp_path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            continue;
        } else {
            ESP_LOGE(TAG, "Unexpected HTTP status: %d", status_code);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        if (!f) {
            ESP_LOGE(TAG, "Failed to open temporary file %s", temp_path);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            break;
        }

        /* Use large 32KB stream buffer in PSRAM to eliminate SDMMC bus contention with SDIO Wi-Fi */
        if (stream_buf) {
            setvbuf(f, stream_buf, _IOFBF, 32768);
        }

        bool stream_success = true;
        while (1) {
            int read_bytes = esp_http_client_read(client, buffer, 8192);
            if (read_bytes < 0) {
                ESP_LOGW(TAG, "HTTP read error (%d) at %d bytes", read_bytes, current_file_bytes);
                stream_success = false;
                break;
            } else if (read_bytes == 0) {
                /* Stream finished */
                break;
            }

            size_t written = fwrite(buffer, 1, read_bytes, f);
            if (written != (size_t)read_bytes) {
                ESP_LOGE(TAG, "SD card write error at byte %d", current_file_bytes);
                stream_success = false;
                break;
            }

            current_file_bytes += read_bytes;

            /* Yield 10ms to allow ESP-Hosted SDIO Wi-Fi driver time on SDMMC bus */
            vTaskDelay(pdMS_TO_TICKS(10));

            if (total_expected > 0) {
                int pct = (current_file_bytes * 100) / total_expected;
                if (pct != last_reported_pct) {
                    last_reported_pct = pct;
                    s_ota_progress = pct;
                    if (params->progress_cb) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Downloading: %d%% (%d / %d KB)",
                                 pct, current_file_bytes / 1024, total_expected / 1024);
                        params->progress_cb(pct, msg);
                    }
                }
            }
        }

        fflush(f);
        fclose(f);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        if (stream_success) {
            if (total_expected > 0 && current_file_bytes >= total_expected) {
                download_finished = true;
                break;
            } else if (total_expected <= 0 && current_file_bytes > 1024*1024) {
                download_finished = true;
                break;
            }
        }

        ESP_LOGW(TAG, "Download interrupted at %d bytes. Preparing to resume...", current_file_bytes);
        if (params->progress_cb) {
            char resume_msg[64];
            snprintf(resume_msg, sizeof(resume_msg), "Resuming from %d KB...", current_file_bytes / 1024);
            params->progress_cb(last_reported_pct > 0 ? last_reported_pct : 0, resume_msg);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    free(buffer);
    if (stream_buf) free(stream_buf);

    if (!download_finished) {
        ESP_LOGE(TAG, "Download failed after all retry attempts");
        if (params->done_cb) params->done_cb(ESP_FAIL, "Download interrupted. Please tap Download again to resume.");
    } else {
        /* Verify header of newly downloaded file */
        firmware_info_t test_info;
        if (!parse_binary_header(temp_path, &test_info)) {
            unlink(temp_path);
            ESP_LOGE(TAG, "Downloaded file has invalid ESP32 firmware header");
            if (params->done_cb) params->done_cb(ESP_FAIL, "Invalid firmware binary header");
        } else {
            char final_path[256];
            char clean_ver[32];
            strncpy(clean_ver, test_info.version, sizeof(clean_ver) - 1);
            clean_ver[sizeof(clean_ver) - 1] = '\0';
            for (int i = 0; clean_ver[i] != '\0'; i++) {
                if (clean_ver[i] == '/' || clean_ver[i] == ' ' || clean_ver[i] == '\\') {
                    clean_ver[i] = '_';
                }
            }

            if (strlen(clean_ver) > 0 && strcmp(params->version, "latest") == 0) {
                snprintf(final_path, sizeof(final_path), "%s/SmartAttendance_%s.bin",
                         OTA_FIRMWARE_DIR, clean_ver);
            } else {
                strncpy(final_path, target_path, sizeof(final_path) - 1);
                final_path[sizeof(final_path) - 1] = '\0';
            }

            unlink(final_path);
            rename(temp_path, final_path);
            ESP_LOGI(TAG, "Firmware successfully saved to: %s", final_path);
            if (params->done_cb) params->done_cb(ESP_OK, "Firmware downloaded successfully to SD card!");
        }
    }

    s_ota_busy = false;
    s_ota_progress = -1;
    free(params);
    vTaskDelete(NULL);
}

esp_err_t ota_manager_download_to_sd_async(const char *url, const char *version,
                                           ota_progress_cb_t progress_cb, ota_done_cb_t done_cb) {
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;
    if (s_ota_busy) return ESP_ERR_INVALID_STATE;

    download_task_param_t *params = (download_task_param_t *)malloc(sizeof(download_task_param_t));
    if (!params) return ESP_ERR_NO_MEM;

    strncpy(params->url, url, sizeof(params->url) - 1);
    strncpy(params->version, version ? version : "update", sizeof(params->version) - 1);
    params->progress_cb = progress_cb;
    params->done_cb = done_cb;

    s_ota_busy = true;
    s_ota_progress = 0;

    BaseType_t ret = xTaskCreate(download_task, "ota_down", 10240, params, 5, NULL);
    if (ret != pdPASS) {
        free(params);
        s_ota_busy = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---- Flash from SD Worker Task ---- */
static void flash_task(void *pvParam) {
    flash_task_param_t *params = (flash_task_param_t *)pvParam;
    ESP_LOGI(TAG, "Initiating firmware install from: %s", params->filepath);

    if (params->progress_cb) params->progress_cb(0, "Validating firmware binary...");

    FILE *f = fopen(params->filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open firmware file %s", params->filepath);
        if (params->done_cb) params->done_cb(ESP_FAIL, "Cannot open firmware file on SD card");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Max OTA app partition is 6MB (0x600000) */
    if (file_size > 0x600000 || file_size < 1024) {
        fclose(f);
        ESP_LOGE(TAG, "Invalid binary size: %ld bytes (max 6MB)", file_size);
        if (params->done_cb) params->done_cb(ESP_ERR_INVALID_SIZE, "Firmware size exceeds partition capacity (6MB)");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    /* Verify header */
    firmware_info_t info;
    if (!parse_binary_header(params->filepath, &info)) {
        fclose(f);
        ESP_LOGE(TAG, "Header check failed for %s", params->filepath);
        if (params->done_cb) params->done_cb(ESP_FAIL, "Corrupt or invalid firmware binary header");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        fclose(f);
        ESP_LOGE(TAG, "No standby OTA partition available");
        if (params->done_cb) params->done_cb(ESP_FAIL, "No standby OTA slot found");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Writing to standby partition: %s (offset 0x%08" PRIx32 ", size 0x%08" PRIx32 ")",
             update_partition->label, update_partition->address, update_partition->size);

    if (params->progress_cb) params->progress_cb(5, "Erasing target flash slot...");

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, file_size, &ota_handle);
    if (err != ESP_OK) {
        fclose(f);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        if (params->done_cb) params->done_cb(err, "Flash preparation failed");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    /* Allocate 32 KB chunk buffer */
    const size_t CHUNK_SIZE = 32768;
    uint8_t *chunk_buf = (uint8_t *)malloc(CHUNK_SIZE);
    if (!chunk_buf) {
        esp_ota_abort(ota_handle);
        fclose(f);
        if (params->done_cb) params->done_cb(ESP_ERR_NO_MEM, "Out of memory for flash buffer");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    size_t total_written = 0;
    bool write_ok = true;
    int last_reported_pct = -1;

    while (total_written < (size_t)file_size) {
        size_t bytes_to_read = (file_size - total_written > CHUNK_SIZE) ? CHUNK_SIZE : (file_size - total_written);
        size_t read_bytes = fread(chunk_buf, 1, bytes_to_read, f);
        if (read_bytes != bytes_to_read) {
            ESP_LOGE(TAG, "SD card read error at byte %zu", total_written);
            write_ok = false;
            break;
        }

        err = esp_ota_write(ota_handle, chunk_buf, read_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write error: %s", esp_err_to_name(err));
            write_ok = false;
            break;
        }

        total_written += read_bytes;
        int pct = 5 + (int)((total_written * 90) / file_size);
        if (pct != last_reported_pct) {
            last_reported_pct = pct;
            s_ota_progress = pct;
            if (params->progress_cb) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Flashing: %d%% (%zu / %ld KB)",
                         pct, total_written / 1024, file_size / 1024);
                params->progress_cb(pct, msg);
            }
        }
    }

    free(chunk_buf);
    fclose(f);

    if (!write_ok) {
        esp_ota_abort(ota_handle);
        if (params->done_cb) params->done_cb(ESP_FAIL, "Flash write failed");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    if (params->progress_cb) params->progress_cb(96, "Verifying checksum...");

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        if (params->done_cb) params->done_cb(err, "Firmware verification failed");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        if (params->done_cb) params->done_cb(err, "Failed to switch active boot slot");
        free(params);
        s_ota_busy = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Firmware flashed successfully to slot %s! Restarting...", update_partition->label);
    if (params->progress_cb) params->progress_cb(100, "Update complete! Rebooting device...");
    if (params->done_cb) params->done_cb(ESP_OK, "Firmware updated successfully! Rebooting...");

    free(params);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

esp_err_t ota_manager_flash_from_sd_async(const char *filepath,
                                          ota_progress_cb_t progress_cb, ota_done_cb_t done_cb) {
    if (!filepath || strlen(filepath) == 0) return ESP_ERR_INVALID_ARG;
    if (s_ota_busy) return ESP_ERR_INVALID_STATE;

    flash_task_param_t *params = (flash_task_param_t *)malloc(sizeof(flash_task_param_t));
    if (!params) return ESP_ERR_NO_MEM;

    strncpy(params->filepath, filepath, sizeof(params->filepath) - 1);
    params->progress_cb = progress_cb;
    params->done_cb = done_cb;

    s_ota_busy = true;
    s_ota_progress = 0;

    BaseType_t ret = xTaskCreate(flash_task, "ota_flash", 8192, params, 5, NULL);
    if (ret != pdPASS) {
        free(params);
        s_ota_busy = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Legacy wrapper */
esp_err_t ota_manager_start(const char *url) {
    return ota_manager_download_to_sd_async(url, "latest", NULL, NULL);
}

int ota_manager_get_progress(void) {
    return s_ota_progress;
}

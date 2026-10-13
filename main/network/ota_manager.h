/**
 * @file ota_manager.h
 * @brief Over-The-Air (OTA) and SD-Card Firmware Update & Rollback Manager
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define OTA_FIRMWARE_DIR        "/sdcard/firmware"
#define MAX_FIRMWARE_ENTRIES    20

typedef struct {
    char filename[96];          /* e.g. "SmartAttendance_v1.0.1.bin" */
    char filepath[256];         /* e.g. "/sdcard/firmware/SmartAttendance_v1.0.1.bin" */
    char version[32];           /* e.g. "v1.0.1" extracted from esp_app_desc_t */
    char project_name[32];      /* e.g. "SmartAttendance" */
    char compile_date[16];      /* e.g. "Oct 13 2026" */
    char compile_time[16];      /* e.g. "20:40:43" */
    char slot_name[16];         /* e.g. "ota_0", "ota_1", or "SD Card" */
    size_t size_bytes;          /* file/image size in bytes */
    bool is_current;            /* true if matches currently running partition */
    bool is_valid;              /* true if valid ESP-IDF app binary header */
} firmware_info_t;

typedef void (*ota_progress_cb_t)(int percent, const char *status_msg);
typedef void (*ota_done_cb_t)(esp_err_t err, const char *message);

/**
 * @brief Initialize the OTA subsystem (ensures /sdcard/firmware directory exists)
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Get metadata about the currently running firmware partition
 */
esp_err_t ota_manager_get_running_info(firmware_info_t *out_info);

/**
 * @brief Scan /sdcard/firmware/ for all available .bin firmware files
 * @param out_list Array of firmware_info_t to populate
 * @param max_entries Maximum entries the array can hold
 * @return Number of valid firmware files discovered
 */
int ota_manager_list_sd_firmwares(firmware_info_t *out_list, int max_entries);

/**
 * @brief Download remote firmware to SD card asynchronously
 * @param url Remote HTTPS URL
 * @param version Version identifier (used to name file, e.g. "v1.0.1")
 * @param progress_cb Optional progress callback (0-100%)
 * @param done_cb Completion callback
 */
esp_err_t ota_manager_download_to_sd_async(const char *url, const char *version,
                                           ota_progress_cb_t progress_cb, ota_done_cb_t done_cb);

/**
 * @brief Flash a local firmware binary from SD card into the inactive OTA slot
 * @param filepath Full path to .bin file (e.g. "/sdcard/firmware/SmartAttendance_v1.0.0.bin")
 * @param progress_cb Optional progress callback (0-100%)
 * @param done_cb Completion callback (if ESP_OK, system reboots automatically)
 */
esp_err_t ota_manager_flash_from_sd_async(const char *filepath,
                                          ota_progress_cb_t progress_cb, ota_done_cb_t done_cb);

/**
 * @brief Legacy streaming direct OTA update (wrapper)
 */
esp_err_t ota_manager_start(const char *url);
int ota_manager_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */

/**
 * @file cloud_sync.h
 * @brief Cloud synchronization client for attendance data
 * 
 * Implements HTTPS client for:
 * - Pulling schedule updates from cloud API
 * - Pushing attendance logs to cloud
 * - Downloading user database updates [citation:3][citation:6]
 */

#ifndef CLOUD_SYNC_H
#define CLOUD_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @brief Cloud sync status
 */
typedef enum {
    SYNC_STATUS_IDLE,
    SYNC_STATUS_IN_PROGRESS,
    SYNC_STATUS_SUCCESS,
    SYNC_STATUS_FAILED
} sync_status_t;

/**
 * @brief Initialize cloud sync module
 * @return ESP_OK on success
 */
esp_err_t cloud_sync_init(void);

/**
 * @brief Start cloud synchronization (non-blocking)
 * @return ESP_OK if started successfully
 */
esp_err_t cloud_sync_start(void);

/**
 * @brief Get current sync status
 * @return sync_status_t current status
 */
sync_status_t cloud_sync_get_status(void);

/**
 * @brief Set cloud API endpoint URL
 * @param url base URL (e.g., "https://api.example.com")
 */
void cloud_sync_set_endpoint(const char* url);

/**
 * @brief Set API authentication token
 * @param token Bearer token for API authentication
 */
void cloud_sync_set_token(const char* token);

/**
 * @brief Get last sync timestamp
 * @return Unix timestamp of last successful sync
 */
time_t cloud_sync_get_last_timestamp(void);


#ifdef __cplusplus
}
#endif

#endif /* CLOUD_SYNC_H */
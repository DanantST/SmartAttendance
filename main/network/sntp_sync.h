/**
 * @file sntp_sync.h
 * @brief SNTP time synchronization interface
 */

#ifndef SNTP_SYNC_H
#define SNTP_SYNC_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SNTP and start synchronization
 * @return ESP_OK on success
 */
esp_err_t sntp_sync_init(void);

/**
 * @brief Check if time is synchronized
 * @return true if synchronized
 */
bool sntp_sync_is_synchronized(void);

/**
 * @brief Wait for time synchronization (blocking)
 * @param timeout_ms timeout in milliseconds
 * @return ESP_OK if synchronized within timeout
 */
esp_err_t sntp_sync_wait_for_sync(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* SNTP_SYNC_H */

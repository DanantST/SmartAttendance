/**
 * @file ota_manager.h
 * @brief Over-The-Air (OTA) update manager
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Start OTA update from the given URL
 * @param url HTTPS URL of the firmware binary
 * @return ESP_OK if update started, ESP_FAIL otherwise
 */
esp_err_t ota_manager_start(const char* url);

/**
 * @brief Get current OTA update progress
 * @return progress percentage (0-100) or -1 if no update in progress
 */
int ota_manager_get_progress(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */

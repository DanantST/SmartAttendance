#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Start OTA update from given URL
 * 
 * @param url HTTPS URL to firmware binary
 * @return esp_err_t ESP_OK on success (will reboot), error otherwise
 */
esp_err_t ota_manager_update(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */

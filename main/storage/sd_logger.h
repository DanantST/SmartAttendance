#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD card logging system.
 * Hooks esp_log_set_vprintf() to intercept system logs (ESP_LOGI, ESP_LOGW, ESP_LOGE)
 * and push them asynchronously to /sdcard/logs/system.log.
 * Automatically rotates system.log -> system.log.old when size exceeds 500 KB.
 */
esp_err_t sd_logger_init(void);

/**
 * @brief Dump stored SD card system logs directly to standard output / serial monitor.
 * @return Number of lines printed to console, or -1 on error.
 */
int sd_logger_dump(void);

/**
 * @brief Non-blocking check for serial monitor input ('log_dump' or 'DUMP_LOGS')
 * to allow triggering SD log dumps from the serial monitor at any time.
 */
void sd_logger_check_serial_trigger(void);

#ifdef __cplusplus
}
#endif

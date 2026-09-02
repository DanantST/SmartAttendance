#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize SD card logging system.
 * Starts background SD file writer task for usage logs at /sdcard/logs/system.log.
 * Automatically rotates system.log -> system.log.old when size exceeds 500 KB.
 */
esp_err_t sd_logger_init(void);

/**
 * @brief Record a usage log entry to the SD card.
 * Thread-safe and non-blocking (queued to background writer).
 *
 * @param tag Component or category tag (e.g. "ATTENDANCE", "ENROLL", "SYSTEM")
 * @param fmt Printf-style format string followed by arguments
 */
void sd_logger_write(const char *tag, const char *fmt, ...);

/**
 * @brief Dump stored SD card system logs directly to standard output / serial monitor.
 * @return Number of lines printed to console, or -1 on error.
 */
int sd_logger_dump(void);

/**
 * @brief Non-blocking check for serial monitor input ('d' or 'log_dump')
 * to allow triggering SD log dumps from the serial monitor at any time.
 */
void sd_logger_check_serial_trigger(void);

#ifdef __cplusplus
}
#endif

/**
 * @file ds1302.h
 * @brief Driver for DS1302 Real-Time Clock module
 *
 * Implements a bit-banged 3-wire synchronous serial protocol (CE, CLK, DAT)
 * for ESP32-P4 on expansion header J7.
 */

#ifndef DS1302_H
#define DS1302_H

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize DS1302 GPIOs and start the oscillator if halted.
 * @return ESP_OK on success
 */
esp_err_t ds1302_init(void);

/**
 * @brief Read date and time from DS1302 into struct tm.
 * @param[out] timeinfo Pointer to struct tm to populate
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if communication failed
 */
esp_err_t ds1302_get_time(struct tm *timeinfo);

/**
 * @brief Write date and time from struct tm into DS1302.
 * @param[in] timeinfo Pointer to struct tm containing valid date/time
 * @return ESP_OK on success
 */
esp_err_t ds1302_set_time(const struct tm *timeinfo);

/**
 * @brief Synchronize ESP-IDF POSIX system clock from DS1302.
 * Validates RTC timestamp (must be >= 2024). If valid, calls settimeofday().
 * @return ESP_OK if system clock was set, ESP_ERR_INVALID_RESPONSE if RTC invalid
 */
esp_err_t ds1302_sync_system_time(void);

/**
 * @brief Save current system clock into DS1302 (called on WiFi/SNTP sync).
 * @return ESP_OK on success
 */
esp_err_t ds1302_save_system_time(void);

#ifdef __cplusplus
}
#endif

#endif /* DS1302_H */

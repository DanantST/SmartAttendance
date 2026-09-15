#pragma once

#include "esp_err.h"
#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

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

/* ===========================================================================
 * Battery Calibration CSV Logger
 * Available only when BATTERY_CALIB_LOGGING == 1 in config.h.
 * =========================================================================*/

#if BATTERY_CALIB_LOGGING

/**
 * @brief Initialize battery calibration CSV logger.
 * Creates a separate queue and FreeRTOS writer task for /sdcard/logs/batt_calib.csv.
 * Must be called after sd_logger_init() and after SD card is mounted.
 * @return ESP_OK on success
 */
esp_err_t sd_logger_calib_init(void);

/**
 * @brief Write a single raw STC8 telemetry row to batt_calib.csv.
 * Thread-safe and non-blocking. Drops row silently if queue is full.
 *
 * @param unix_ts    Unix epoch timestamp (0 if SNTP not yet synced)
 * @param boot_ms    Milliseconds since device boot (esp_timer_get_time / 1000)
 * @param bat_mv     Battery terminal voltage in mV
 * @param adc_mv     Raw STC8 ADC voltage in mV (may equal bat_mv; logged for analysis)
 * @param stc8_pct   STC8-reported battery percentage 0-100
 * @param stc8_state Raw STC8 state byte (0=idle,1=charging,2=full,3=no-charge,4=error)
 * @param charging   true if plugged in and charging
 * @param event      Row label: "NORMAL","PLUG_IN","PLUG_OUT","SESSION_START","SESSION_END"
 */
void sd_logger_write_batt_calib(time_t unix_ts, uint32_t boot_ms,
                                 uint16_t bat_mv, uint16_t adc_mv,
                                 uint8_t stc8_pct, uint8_t stc8_state,
                                 bool charging, const char *event);

#else /* BATTERY_CALIB_LOGGING == 0 — compile to no-ops */

static inline esp_err_t sd_logger_calib_init(void) { return ESP_OK; }
static inline void sd_logger_write_batt_calib(time_t u, uint32_t b,
                                               uint16_t bv, uint16_t av,
                                               uint8_t p, uint8_t s,
                                               bool c, const char *e)
    { (void)u;(void)b;(void)bv;(void)av;(void)p;(void)s;(void)c;(void)e; }

#endif /* BATTERY_CALIB_LOGGING */

#ifdef __cplusplus
}
#endif


/**
 * @file battery_monitor.h
 * @brief Battery voltage monitoring and percentage calculation
 * 
 * Reads ADC voltage from battery divider circuit and calculates
 * remaining percentage using calibrated lookup table [citation:4]
 */

#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Battery status
 */
typedef enum {
    BATTERY_STATUS_OK,
    BATTERY_STATUS_LOW,
    BATTERY_STATUS_CRITICAL,
    BATTERY_STATUS_CHARGING
} battery_status_t;

/**
 * @brief Initialize battery monitor
 * @return ESP_OK on success
 */
esp_err_t battery_monitor_init(void);

/**
 * @brief Get current battery voltage (mV)
 * @return voltage in millivolts
 */
uint16_t battery_monitor_get_voltage_mv(void);

/**
 * @brief Get battery percentage (0-100)
 * @return estimated remaining capacity percentage
 */
int battery_monitor_get_percent(void);

/**
 * @brief Get battery status
 * @return battery_status_t current status
 */
battery_status_t battery_monitor_get_status(void);

/**
 * @brief Check if battery is charging (USB power present)
 * @return true if charging
 */
bool battery_monitor_is_charging(void);

/**
 * @brief Perform graceful shutdown (save data, power off)
 */
void battery_monitor_shutdown(void);

/**
 * @brief Update last activity time for idle tracking
 */
void battery_monitor_update_activity(void);

/**
 * @brief Check for idle timeout and enter deep sleep
 */
void battery_monitor_check_idle_sleep(void);


#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MONITOR_H */
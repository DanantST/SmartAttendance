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
#include <time.h>

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
 * @brief Raw telemetry struct returned by battery_monitor_read_raw().
 * All fields populated in a single I²C read sequence from the STC8 co-processor.
 */
typedef struct {
    uint16_t bat_mv;     /**< Battery terminal voltage in mV (post-divider correction) */
    uint16_t adc_mv;     /**< Raw ADC reading in mV (pre-divider, may equal bat_mv) */
    uint8_t  stc8_pct;  /**< STC8-reported battery percentage 0–100 */
    uint8_t  stc8_state;/**< STC8 state: 0=idle,1=charging,2=full+charging,3=no-charge,4=error */
    bool     charging;   /**< true if stc8_state==1 or stc8_state==2 */
} battery_raw_t;

/**
 * @brief Read all raw STC8 telemetry in one I²C transaction sequence.
 *        Updates the internal voltage/percent/charging cache as a side effect.
 * @param[out] out  Pointer to battery_raw_t to populate
 * @return ESP_OK on success, error code on I²C failure
 */
esp_err_t battery_monitor_read_raw(battery_raw_t *out);

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
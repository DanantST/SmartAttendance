/**
 * @file battery_monitor.c
 * @brief Battery monitor implementation using STC8H1Kxx coprocessor over shared I2C bus
 */

#include "battery_monitor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "boards/elecrow_p4_board.h"
#include "display/touch_driver.h"
#include "network/cloud_sync.h"
#include "utils/queue_manager.h"
#include "ui/ui_main.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

#define IDLE_TIMEOUT_US (5 * 60 * 1000 * 1000LL) // 5 minutes
static int64_t s_last_activity_time = 0;
static bool s_screen_dimmed = false;

void battery_monitor_update_activity(void) {
    s_last_activity_time = esp_timer_get_time();
    if (s_screen_dimmed) {
        s_screen_dimmed = false;
        board_backlight_set(ui_get_brightness());
        ESP_LOGI("BATTERY", "User activity detected: restoring backlight");
    }
}

static const char* TAG = "BATTERY";

/* STC8 Coprocessor Address and Register mapping */
#define BSP_STC8H1KXX_I2C_ADDRESS       0x2F
#define STC8_REG_ADDR_BATTERY           0x00

typedef struct {
    uint32_t adc_voltage;    // Collected voltage (mV)
    uint32_t bat_voltage;    // Battery voltage after converting the voltage divider (mV)
    uint8_t bat_level;       // Battery percentage charge (%)
    uint8_t bat_state;       // Battery status (0=idle, 1=charging, 2=fully charged, 3=no charge, 4=error)
    uint8_t led_state;       // LED indicator light status
} __attribute__((packed)) STC8_Battery_info_t;

static i2c_master_dev_handle_t s_stc8_handle = NULL;
static uint16_t s_last_voltage_mv = 3700;
static int s_last_percent = 100;
static bool s_charging = false;

/* ==================== Calibrated Battery Lookup Table ==================== */
/* Discharge LUT (unplugged resting voltage).
 * Derived from batt_calib.csv empirical data with linear interpolation.
 * Strictly monotonic ascending for binary search.
 */
static const struct { uint16_t mv; uint8_t pct; } discharge_lut[] = {
    { 3300,  0 },   /* Empty cutoff threshold */
    { 3537,  5 },
    { 3549,  7 },
    { 3561,  9 },
    { 3573, 10 },
    { 3583, 11 },
    { 3591, 13 },
    { 3601, 14 },
    { 3609, 15 },
    { 3618, 16 },
    { 3623, 17 },
    { 3632, 18 },
    { 3641, 20 },
    { 3654, 22 },
    { 3668, 24 },
    { 3686, 26 },
    { 3694, 27 },
    { 3698, 28 },
    { 3704, 29 },
    { 3714, 30 },
    { 3720, 31 },
    { 3726, 32 },
    { 3734, 33 },
    { 3739, 34 },
    { 3748, 35 },
    { 3757, 36 },
    { 3766, 38 },
    /* Gap 39–49%: interpolated linearly */
    { 3810, 43 },
    { 3855, 50 },
    { 3860, 51 },
    { 3869, 52 },
    { 3873, 53 },
    { 3882, 54 },
    { 3887, 55 },
    { 3892, 56 },
    { 3900, 57 },
    { 3910, 58 },
    { 3918, 59 },
    { 3928, 61 },
    { 3936, 62 },
    /* High SoC curve to resting 100% (4.06V resting full measured by user multimeter) */
    { 3970, 72 },
    { 4000, 82 },
    { 4030, 91 },
    { 4060, 100 },
};
#define DISCHARGE_LUT_SIZE  (sizeof(discharge_lut)/sizeof(discharge_lut[0]))

/**
 * @brief Charging surface-charge voltage correction.
 * Compensates for the ~220-255mV voltage inflation present when charging.
 */
static uint16_t charging_voltage_correction(uint16_t bat_mv) {
    if (bat_mv >= 4250) return 4060;
    if (bat_mv >= 4100) {
        /* User measured 4280mV charging -> 4060mV resting (offset ~220mV) */
        return (bat_mv > 220) ? (bat_mv - 220) : 0;
    }
    /* Empirical offset from batt_calib.csv across 15%–75% is ~255mV */
    return (bat_mv > 255) ? (bat_mv - 255) : 0;
}

/**
 * @brief Convert raw bat_mv to calibrated battery percentage.
 * Replaces the STC8 coprocessor's inaccurate internal percentage.
 */
static uint8_t battery_calibrated_percent(uint16_t bat_mv, bool charging, uint8_t bat_state) {
    /* State 2: STC8 indicates fully charged */
    if (bat_state == 2) return 100;

    uint16_t corrected_mv = charging ? charging_voltage_correction(bat_mv) : bat_mv;

    if (corrected_mv <= discharge_lut[0].mv) return 0;
    if (corrected_mv >= discharge_lut[DISCHARGE_LUT_SIZE - 1].mv) return 100;

    int lo = 0, hi = DISCHARGE_LUT_SIZE - 1;
    while (lo < hi - 1) {
        int mid = (lo + hi) / 2;
        if (corrected_mv >= discharge_lut[mid].mv) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    uint16_t mv0 = discharge_lut[lo].mv,  mv1 = discharge_lut[hi].mv;
    uint8_t  p0  = discharge_lut[lo].pct, p1  = discharge_lut[hi].pct;
    uint8_t pct = p0 + (uint8_t)(((uint32_t)(corrected_mv - mv0) * (p1 - p0)) / (mv1 - mv0));
    return pct > 100 ? 100 : pct;
}

/* Helper to read telemetry from STC8 coprocessor */
static esp_err_t stc8_read_battery(STC8_Battery_info_t *info) {
    if (!s_stc8_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t *ptr = (uint8_t *)info;
    for (int i = 0; i < sizeof(STC8_Battery_info_t); i++) {
        uint8_t reg_addr = STC8_REG_ADDR_BATTERY + i;
        esp_err_t ret = i2c_master_transmit_receive(s_stc8_handle, &reg_addr, 1, ptr + i, 1, 100);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t battery_monitor_init(void) {
    ESP_LOGI(TAG, "Initializing STC8 battery monitor via shared I2C bus...");
    battery_monitor_update_activity();
    
    i2c_master_bus_handle_t i2c_bus = touch_get_i2c_bus();
    if (!i2c_bus) {
        ESP_LOGE(TAG, "Shared I2C bus handle is NULL! Ensure touch_init() runs first");
        return ESP_FAIL;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_STC8H1KXX_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_stc8_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register STC8 I2C device on shared bus: %d", ret);
        return ret;
    }
    
    /* Pre-populate telemetry cache */
    battery_monitor_get_percent();
    
    ESP_LOGI(TAG, "STC8 battery monitor initialized successfully");
    return ESP_OK;
}

uint16_t battery_monitor_get_voltage_mv(void) {
    STC8_Battery_info_t info;
    if (stc8_read_battery(&info) == ESP_OK) {
        s_last_voltage_mv = info.bat_voltage;
    }
    return s_last_voltage_mv;
}

int battery_monitor_get_percent(void) {
    STC8_Battery_info_t info;
    if (stc8_read_battery(&info) == ESP_OK) {
        s_last_voltage_mv = info.bat_voltage;
        
        /* State 1: Charging, State 2: Fully Charged */
        if (info.bat_state == 1 || info.bat_state == 2) {
            s_charging = true;
            battery_monitor_update_activity();
        } else {
            s_charging = false;
        }

        s_last_percent = battery_calibrated_percent(info.bat_voltage, s_charging, info.bat_state);
        
        ESP_LOGI(TAG, "Power Check (STC8): %dmV, Calibrated: %d%% (STC8 raw: %d%%), State: %d, Charging: %d",
                 (int)info.bat_voltage, (int)s_last_percent, (int)info.bat_level, (int)info.bat_state, (int)s_charging);
    } else {
        ESP_LOGE(TAG, "Failed to read battery info from STC8 over I2C");
    }
    return s_last_percent;
}

esp_err_t battery_monitor_read_raw(battery_raw_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;

    STC8_Battery_info_t info;
    esp_err_t ret = stc8_read_battery(&info);
    if (ret != ESP_OK) return ret;

    /* Update shared cache */
    s_last_voltage_mv = info.bat_voltage;
    s_charging        = (info.bat_state == 1 || info.bat_state == 2);
    if (s_charging) battery_monitor_update_activity();

    uint8_t calib_pct = battery_calibrated_percent(info.bat_voltage, s_charging, info.bat_state);
    s_last_percent    = calib_pct;

    out->bat_mv     = info.bat_voltage;
    out->adc_mv     = info.adc_voltage;
    out->stc8_pct   = calib_pct;
    out->stc8_state = info.bat_state;
    out->charging   = s_charging;

    return ESP_OK;
}


battery_status_t battery_monitor_get_status(void) {
    int percent = battery_monitor_get_percent();
    
    if (s_charging) {
        return BATTERY_STATUS_CHARGING;
    }
    
    if (percent <= BATTERY_SHUTDOWN_THRESHOLD) {
        return BATTERY_STATUS_CRITICAL;
    }
    
    if (percent <= BATTERY_WARNING_THRESHOLD) {
        return BATTERY_STATUS_LOW;
    }
    
    return BATTERY_STATUS_OK;
}

void battery_monitor_check_idle_sleep(void) {
    if (s_charging) return;

    if (get_system_state() != SYSTEM_STATE_NORMAL) {
        battery_monitor_update_activity();
        return;
    }

    if (cloud_sync_get_status() == SYNC_STATUS_IN_PROGRESS) {
        battery_monitor_update_activity();
        return;
    }

    if (g_db_request_queue != NULL && uxQueueMessagesWaiting(g_db_request_queue) > 0) {
        battery_monitor_update_activity();
        return;
    }
    
    if (!s_screen_dimmed && (esp_timer_get_time() - s_last_activity_time > IDLE_TIMEOUT_US)) {
        ESP_LOGI(TAG, "System idle for 5 minutes: blanking backlight to save battery (tap screen to wake)");
        board_backlight_set(0);
        s_screen_dimmed = true;
    }
}

bool battery_monitor_is_charging(void) {
    return s_charging;
}

void battery_monitor_shutdown(void) {
    ESP_LOGW(TAG, "Performing emergency shutdown");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    if (POWER_ENABLE_PIN != GPIO_NC) {
        gpio_set_level(POWER_ENABLE_PIN, 0);
    }
    
    ESP_LOGI(TAG, "System halted due to critical battery");
    while (1) {
        vTaskSuspend(NULL);
    }
}
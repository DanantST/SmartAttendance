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

void battery_monitor_update_activity(void) {
    s_last_activity_time = esp_timer_get_time();
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
        s_last_percent = info.bat_level;
        
        /* State 1: Charging, State 2: Fully Charged */
        if (info.bat_state == 1 || info.bat_state == 2) {
            s_charging = true;
            battery_monitor_update_activity();
        } else {
            s_charging = false;
        }
        
        ESP_LOGI(TAG, "Power Check (STC8): %dmV, Level: %d%%, State: %d, Charging: %d",
                 (int)info.bat_voltage, (int)info.bat_level, (int)info.bat_state, (int)s_charging);
    } else {
        ESP_LOGE(TAG, "Failed to read battery info from STC8 over I2C");
    }
    return s_last_percent;
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
    
    if (esp_timer_get_time() - s_last_activity_time > IDLE_TIMEOUT_US) {
        ESP_LOGI(TAG, "System idle for 5 minutes. Entering light sleep...");
        
        board_backlight_set(0);
        
        esp_sleep_enable_ext1_wakeup(1ULL << TOUCH_INT_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
        
        esp_light_sleep_start();
        
        board_backlight_set(ui_get_brightness());
        
        battery_monitor_update_activity();
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
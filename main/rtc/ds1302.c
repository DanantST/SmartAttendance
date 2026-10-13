/**
 * @file ds1302.c
 * @brief Driver for DS1302 Real-Time Clock module
 */

#include "rtc/ds1302.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <sys/time.h>
#include <string.h>

static const char *TAG = "DS1302";

/* Register commands (Write / Read) */
#define DS1302_CMD_SECONDS_W    0x80
#define DS1302_CMD_SECONDS_R    0x81
#define DS1302_CMD_MINUTES_W    0x82
#define DS1302_CMD_MINUTES_R    0x83
#define DS1302_CMD_HOURS_W      0x84
#define DS1302_CMD_HOURS_R      0x85
#define DS1302_CMD_DATE_W       0x86
#define DS1302_CMD_DATE_R       0x87
#define DS1302_CMD_MONTH_W      0x88
#define DS1302_CMD_MONTH_R      0x89
#define DS1302_CMD_DAY_W        0x8A
#define DS1302_CMD_DAY_R        0x8B
#define DS1302_CMD_YEAR_W       0x8C
#define DS1302_CMD_YEAR_R       0x8D
#define DS1302_CMD_WP_W         0x8E
#define DS1302_CMD_WP_R         0x8F
#define DS1302_CMD_BURST_R      0xBF

#define DELAY_US()              esp_rom_delay_us(2)

static SemaphoreHandle_t s_ds1302_mutex = NULL;

static inline uint8_t bcd2dec(uint8_t val) {
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static inline uint8_t dec2bcd(uint8_t val) {
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static void ds1302_dat_mode(gpio_mode_t mode) {
    gpio_set_direction((gpio_num_t)DS1302_PIN_DAT, mode);
}

/* Transmit 1 byte to DS1302 (LSB first) */
static void ds1302_write_raw_byte(uint8_t val) {
    ds1302_dat_mode(GPIO_MODE_OUTPUT);
    for (int i = 0; i < 8; i++) {
        gpio_set_level((gpio_num_t)DS1302_PIN_DAT, (val >> i) & 0x01);
        DELAY_US();
        gpio_set_level((gpio_num_t)DS1302_PIN_CLK, 1);
        DELAY_US();
        gpio_set_level((gpio_num_t)DS1302_PIN_CLK, 0);
        DELAY_US();
    }
}

/* Receive 1 byte from DS1302 (LSB first) */
static uint8_t ds1302_read_raw_byte(void) {
    ds1302_dat_mode(GPIO_MODE_INPUT);
    uint8_t val = 0;
    for (int i = 0; i < 8; i++) {
        if (gpio_get_level((gpio_num_t)DS1302_PIN_DAT)) {
            val |= (1 << i);
        }
        gpio_set_level((gpio_num_t)DS1302_PIN_CLK, 1);
        DELAY_US();
        gpio_set_level((gpio_num_t)DS1302_PIN_CLK, 0);
        DELAY_US();
    }
    return val;
}

static uint8_t ds1302_read_reg(uint8_t cmd) {
    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 1);
    DELAY_US();
    ds1302_write_raw_byte(cmd);
    uint8_t data = ds1302_read_raw_byte();
    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 0);
    DELAY_US();
    return data;
}

static void ds1302_write_reg(uint8_t cmd, uint8_t data) {
    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 1);
    DELAY_US();
    ds1302_write_raw_byte(cmd);
    ds1302_write_raw_byte(data);
    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 0);
    DELAY_US();
}

esp_err_t ds1302_init(void) {
    if (!s_ds1302_mutex) {
        s_ds1302_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Initializing DS1302 (CLK: GPIO%d, DAT: GPIO%d, CE: GPIO%d)",
             DS1302_PIN_CLK, DS1302_PIN_DAT, DS1302_PIN_CE);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DS1302_PIN_CLK) | (1ULL << DS1302_PIN_CE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)DS1302_PIN_CLK, 0);
    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 0);

    /* DAT line config (open-drain capable or bidirectional) */
    gpio_config_t dat_conf = {
        .pin_bit_mask = (1ULL << DS1302_PIN_DAT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dat_conf);

    if (xSemaphoreTake(s_ds1302_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Disable write protection */
    ds1302_write_reg(DS1302_CMD_WP_W, 0x00);

    /* Check Clock Halt (CH) bit in Seconds register */
    uint8_t sec_raw = ds1302_read_reg(DS1302_CMD_SECONDS_R);
    if (sec_raw & 0x80) {
        ESP_LOGW(TAG, "Oscillator was halted (CH=1). Clearing CH to start clock...");
        ds1302_write_reg(DS1302_CMD_SECONDS_W, sec_raw & 0x7F);
    } else {
        ESP_LOGI(TAG, "Oscillator is running (CH=0)");
    }

    xSemaphoreGive(s_ds1302_mutex);
    return ESP_OK;
}

esp_err_t ds1302_get_time(struct tm *timeinfo) {
    if (!timeinfo) return ESP_ERR_INVALID_ARG;
    if (!s_ds1302_mutex) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_ds1302_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 1);
    DELAY_US();
    ds1302_write_raw_byte(DS1302_CMD_BURST_R);

    uint8_t sec_raw  = ds1302_read_raw_byte();
    uint8_t min_raw  = ds1302_read_raw_byte();
    uint8_t hour_raw = ds1302_read_raw_byte();
    uint8_t date_raw = ds1302_read_raw_byte();
    uint8_t mon_raw  = ds1302_read_raw_byte();
    uint8_t day_raw  = ds1302_read_raw_byte();
    uint8_t year_raw = ds1302_read_raw_byte();

    gpio_set_level((gpio_num_t)DS1302_PIN_CE, 0);
    DELAY_US();

    xSemaphoreGive(s_ds1302_mutex);

    /* If all 0xFF or 0x00, communication failed */
    if ((sec_raw == 0xFF && min_raw == 0xFF && hour_raw == 0xFF) ||
        (sec_raw == 0x00 && min_raw == 0x00 && date_raw == 0x00 && mon_raw == 0x00 && year_raw == 0x00)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(timeinfo, 0, sizeof(struct tm));
    timeinfo->tm_sec  = bcd2dec(sec_raw & 0x7F);
    timeinfo->tm_min  = bcd2dec(min_raw & 0x7F);
    
    /* 24-hour mode support */
    if (hour_raw & 0x80) {
        /* 12-hour mode: bit 5 indicates PM */
        uint8_t hr = bcd2dec(hour_raw & 0x1F);
        if (hour_raw & 0x20) { /* PM */
            if (hr < 12) hr += 12;
        } else {
            if (hr == 12) hr = 0;
        }
        timeinfo->tm_hour = hr;
    } else {
        timeinfo->tm_hour = bcd2dec(hour_raw & 0x3F);
    }

    timeinfo->tm_mday = bcd2dec(date_raw & 0x3F);
    timeinfo->tm_mon  = bcd2dec(mon_raw & 0x1F) - 1; /* tm_mon is 0-11 */
    timeinfo->tm_year = bcd2dec(year_raw) + 100;      /* 2000-2099 -> 100-199 */
    timeinfo->tm_wday = bcd2dec(day_raw & 0x07) - 1;

    /* Basic sanity check */
    if (timeinfo->tm_mday < 1 || timeinfo->tm_mday > 31 ||
        timeinfo->tm_mon < 0  || timeinfo->tm_mon > 11 ||
        timeinfo->tm_hour > 23 || timeinfo->tm_min > 59 || timeinfo->tm_sec > 59) {
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ds1302_set_time(const struct tm *timeinfo) {
    if (!timeinfo) return ESP_ERR_INVALID_ARG;
    if (!s_ds1302_mutex) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_ds1302_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Disable write protection */
    ds1302_write_reg(DS1302_CMD_WP_W, 0x00);

    /* Write registers: ensure 24h mode and CH=0 */
    uint8_t sec  = dec2bcd((uint8_t)timeinfo->tm_sec) & 0x7F;  /* CH=0 */
    uint8_t min  = dec2bcd((uint8_t)timeinfo->tm_min) & 0x7F;
    uint8_t hour = dec2bcd((uint8_t)timeinfo->tm_hour) & 0x3F; /* 24h format */
    uint8_t date = dec2bcd((uint8_t)timeinfo->tm_mday) & 0x3F;
    uint8_t mon  = dec2bcd((uint8_t)(timeinfo->tm_mon + 1)) & 0x1F;
    uint8_t day  = dec2bcd((uint8_t)(timeinfo->tm_wday + 1)) & 0x07;
    uint8_t year = dec2bcd((uint8_t)(timeinfo->tm_year >= 100 ? (timeinfo->tm_year - 100) : timeinfo->tm_year));

    ds1302_write_reg(DS1302_CMD_SECONDS_W, sec);
    ds1302_write_reg(DS1302_CMD_MINUTES_W, min);
    ds1302_write_reg(DS1302_CMD_HOURS_W,   hour);
    ds1302_write_reg(DS1302_CMD_DATE_W,    date);
    ds1302_write_reg(DS1302_CMD_MONTH_W,   mon);
    ds1302_write_reg(DS1302_CMD_DAY_W,     day);
    ds1302_write_reg(DS1302_CMD_YEAR_W,    year);

    /* Re-enable write protect */
    ds1302_write_reg(DS1302_CMD_WP_W, 0x80);

    xSemaphoreGive(s_ds1302_mutex);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", timeinfo);
    ESP_LOGI(TAG, "DS1302 updated with time: %s", buf);
    return ESP_OK;
}

esp_err_t ds1302_sync_system_time(void) {
    struct tm tm;
    esp_err_t ret = ds1302_get_time(&tm);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not read valid time from DS1302: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Verify year is sensible (>= 2024, tm_year >= 124) */
    if (tm.tm_year < 124) {
        ESP_LOGW(TAG, "DS1302 time is prior to 2024 (%d), skipping system sync", tm.tm_year + 1900);
        return ESP_ERR_INVALID_RESPONSE;
    }

    time_t t = mktime(&tm);
    if (t < 1704067200) { /* 2024-01-01 */
        return ESP_ERR_INVALID_RESPONSE;
    }

    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "System time successfully synchronized from DS1302: %s (epoch: %lld)",
             buf, (long long)t);
    return ESP_OK;
}

esp_err_t ds1302_save_system_time(void) {
    time_t now = time(NULL);
    if (now < 1704067200) { /* If system time is invalid (< 2024), don't overwrite RTC */
        ESP_LOGD(TAG, "System time not yet valid, not saving to DS1302");
        return ESP_ERR_INVALID_STATE;
    }

    struct tm tm;
    localtime_r(&now, &tm);
    return ds1302_set_time(&tm);
}

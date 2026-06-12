/**
 * @file elecrow_p4_board.c
 * @brief Board initialization for CrowPanel Advanced 7" ESP32-P4
 *
 * Hardware-validated facts (CrowPanel v1.0):
 *  - Backlight: GPIO31 direct PWM (no PCA9557 on this revision)
 *  - Touch (GT911): owned by touch_driver.c via legacy I2C on I2C_NUM_1
 *  - SD card: CLK=36, CMD=35, D0=37
 */

#include "elecrow_p4_board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "BOARD";

esp_err_t board_init(void) {
    ESP_LOGI(TAG, "Initializing CrowPanel Advanced 7\" ESP32-P4");

    /* 1. Power init */
    esp_err_t ret = board_power_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Power init failed");
        return ret;
    }

    /* 2. Backlight via direct GPIO31 PWM.
     * PCA9557 I/O expander is NOT present on CrowPanel v1.0.
     * Touch controller (GT911) owns I2C_NUM_1 via legacy driver —
     * initialized separately in touch_driver.c touch_init(). */
    ret = board_backlight_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Backlight PWM init failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Board initialization complete");
    return ESP_OK;
}

esp_err_t board_power_init(void) {
    /* 1. Mute and configure Power Amplifier (PA) enable pin early to prevent boot crackle */
    if (AUDIO_PA_ENABLE != GPIO_NC) {
        gpio_config_t pa_conf = {
            .pin_bit_mask = (1ULL << AUDIO_PA_ENABLE),
            .mode = GPIO_MODE_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Active pull-down to keep amplifier shutdown
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        esp_err_t ret = gpio_config(&pa_conf);
        if (ret == ESP_OK) {
            gpio_set_level(AUDIO_PA_ENABLE, 0);   // Explicitly drive LOW to mute
        }
    }

    if (POWER_ENABLE_PIN != GPIO_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << POWER_ENABLE_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .intr_type = GPIO_INTR_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        esp_err_t ret = gpio_config(&io_conf);
        if (ret != ESP_OK) return ret;
        gpio_set_level(POWER_ENABLE_PIN, 1);
    }
    return ESP_OK;
}

esp_err_t board_backlight_init(void) {
    /* Validated: CrowPanel v1.0 uses GPIO31 PWM — 30 kHz, 11-bit */
    ESP_LOGI(TAG, "Initializing Backlight PWM GPIO%d 30kHz 11-bit",
             LCD_BACKLIGHT_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LCD_BACKLIGHT_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;

    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LCD_BACKLIGHT_PWM_TIMER,
        .duty_resolution = LCD_BACKLIGHT_PWM_RES,     /* 11-bit */
        .freq_hz         = LCD_BACKLIGHT_PWM_FREQ_HZ, /* 30 kHz */
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LCD_BACKLIGHT_PWM_CHANNEL,
        .timer_sel  = LCD_BACKLIGHT_PWM_TIMER,
        .gpio_num   = LCD_BACKLIGHT_PIN,
        .duty       = LCD_BACKLIGHT_DUTY(80), /* start at 80% */
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) return ret;

    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL);
    return ESP_OK;
}

void board_backlight_set(uint8_t percent) {
    if (percent > 100) percent = 100;
    /* Validated: duty = (percent != 0) ? (percent*18 + 200) : 0 */
    uint32_t duty = LCD_BACKLIGHT_DUTY(percent);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL);
}
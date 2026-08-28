#include "display/backlight.h"
#include "driver/ledc.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"

static const char *TAG = "BACKLIGHT";

/* AR-3: Guard against double LEDC init.
 * board_backlight_init() (called from board_init() early in app_main) already
 * configures the same LEDC timer and channel on GPIO31. If ui_main calls
 * backlight_init() again the ESP-IDF LEDC driver logs a warning about
 * reconfiguring an active channel. The guard makes the second call a no-op. */
static bool s_backlight_initialized = false;

esp_err_t backlight_init(void) {
    if (s_backlight_initialized) {
        ESP_LOGD(TAG, "Backlight already initialised by board_init() — skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing backlight via LEDC timer and channel");

    ledc_timer_config_t ledc_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LCD_BACKLIGHT_PWM_TIMER,
        .duty_resolution = LCD_BACKLIGHT_PWM_RES,
        .freq_hz         = LCD_BACKLIGHT_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %d", ret);
        return ret;
    }

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LCD_BACKLIGHT_PWM_CHANNEL,
        .timer_sel  = LCD_BACKLIGHT_PWM_TIMER,
        .gpio_num   = LCD_BACKLIGHT_PIN,
        .duty       = LCD_BACKLIGHT_DUTY(80),
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %d", ret);
        return ret;
    }

    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL);
    s_backlight_initialized = true;
    return ESP_OK;
}

void backlight_mark_initialized(void) {
    s_backlight_initialized = true;
}

void backlight_set(uint8_t percent) {
    if (percent > 100) percent = 100;
    /* Validated duty formula for CrowPanel v1.0 backlight circuit:
     * duty = (percent != 0) ? (percent * 18 + 200) : 0 */
    uint32_t duty = (percent != 0) ? ((uint32_t)(percent * 18) + 200U) : 0U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_PWM_CHANNEL);
}


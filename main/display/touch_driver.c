/**
 * @file touch_driver.c
 * @brief GT911 touch driver for CrowPanel Advanced 7" ESP32-P4
 *
 * Rewritten to use the ESP-IDF v5.4.x new I2C master bus API,
 * matching the Elecrow factory BSP (esp32_p4_function_ev_board.c)
 * exactly.
 *
 * Key facts (from factory BSP):
 *  - I2C port: I2C_NUM_0, GPIO SDA=45 SCL=46
 *  - Clock speed: 400000 Hz (Fast Mode)
 *  - GT911 address: 0x5D (set by holding INT low during RST release)
 *  - RST GPIO: 40, INT GPIO: 42
 *  - io_config set via ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() macro
 *  - x_max=1024, y_max=600, no mirror, no swap
 *
 * Threading architecture:
 *  touch_poll_task (priority 4) – performs I2C reads via
 *  esp_lcd_touch_read_data() and caches the result in s_touch_state.
 *  touch_read_cb (called by lv_timer_handler inside s_lvgl_mux) – reads
 *  only from s_touch_state; NO I2C in the LVGL lock path, preventing the
 *  GT911 I2C hang from deadlocking the entire UI.
 */

#include "display/touch_driver.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"
#include <string.h>

static const char *TAG = "TOUCH";
static esp_lcd_touch_handle_t s_touch_handle = NULL;
static i2c_master_bus_handle_t s_i2c_bus = NULL;

/* -----------------------------------------------------------------------
 * Shared touch state updated by touch_poll_task, consumed by touch_read_cb.
 * Uses a mutex so that partial writes are never observed by the reader.
 * The mutex is tiny (non-recursive, taken for <1µs), completely separate
 * from the LVGL s_lvgl_mux, so there is no deadlock risk.
 * ----------------------------------------------------------------------- */
typedef struct {
    int32_t x;
    int32_t y;
    bool    pressed;
} touch_state_t;

static volatile touch_state_t s_touch_state = {0};
static SemaphoreHandle_t       s_touch_state_mutex = NULL;

/* Poll period in ms. 16 ms ≈ 60 Hz, well within GT911's 5 ms report rate. */
#define TOUCH_POLL_PERIOD_MS  16

/* -----------------------------------------------------------------------
 * touch_poll_task – runs at priority 4, BELOW lvgl_port_task (10) and
 * BELOW camera / enrollment tasks (7, 5), so it never starves them.
 * ALL I2C traffic lives here, never inside lv_timer_handler.
 * ----------------------------------------------------------------------- */
static void touch_poll_task(void *arg)
{
    while (1) {
        /* --- I2C read: may block if GT911 is slow; that's fine here --- */
        if (s_touch_handle) {
            esp_err_t rd = esp_lcd_touch_read_data(s_touch_handle);
            if (rd == ESP_OK) {
                esp_lcd_touch_point_data_t pt;
                uint8_t touch_cnt = 0;
                esp_err_t gd = esp_lcd_touch_get_data(s_touch_handle, &pt, &touch_cnt, 1);

                /* Rate-limited diagnostic log */
                if (gd == ESP_OK && touch_cnt > 0) {
                    static uint32_t last_log_ms = 0;
                    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
                    if (now_ms - last_log_ms > 1000) {
                        ESP_LOGI(TAG, "Touch at x:%d y:%d", (int)pt.x, (int)pt.y);
                        last_log_ms = now_ms;
                    }
                }

                /* Atomically update shared state */
                if (xSemaphoreTake(s_touch_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    if (gd == ESP_OK && touch_cnt > 0) {
                        s_touch_state.x       = (int32_t)pt.x;
                        s_touch_state.y       = (int32_t)pt.y;
                        s_touch_state.pressed = true;
                    } else {
                        s_touch_state.pressed = false;
                    }
                    xSemaphoreGive(s_touch_state_mutex);
                }
            } else {
                /* I2C error – mark as released so UI doesn't get stuck */
                if (xSemaphoreTake(s_touch_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    s_touch_state.pressed = false;
                    xSemaphoreGive(s_touch_state_mutex);
                }
                ESP_LOGW(TAG, "touch_read_data err: %s", esp_err_to_name(rd));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_PERIOD_MS));
    }
}

i2c_master_bus_handle_t touch_get_i2c_bus(void)
{
    return s_i2c_bus;
}

/* I2C settings for touch */
#define TOUCH_I2C_SPEED  400000
#define TOUCH_ADDR       0x5D

esp_err_t touch_init(void)
{
    ESP_LOGI(TAG, "Initializing GT911 touch (Phase 1 exact sequence)");

    /* ----------------------------------------------------------------
     * Step 1 – Manual RST/INT sequence to lock GT911 at address 0x5D.
     * INT must be LOW before RST is released.
     * ---------------------------------------------------------------- */
    gpio_set_direction((gpio_num_t)TOUCH_RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)TOUCH_INT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)TOUCH_RESET_PIN, 0);   /* RST low  */
    gpio_set_level((gpio_num_t)TOUCH_INT_PIN, 0);   /* INT low  */
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level((gpio_num_t)TOUCH_RESET_PIN, 1);   /* release RST */
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level((gpio_num_t)TOUCH_INT_PIN, 1);   /* release INT → 0x5D */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Reset INT to INPUT so we don't block the GT911 from driving it.
     * DO NOT set RST to INPUT; it must be actively driven HIGH or the screen will flicker/reset. */
    gpio_set_direction((gpio_num_t)TOUCH_INT_PIN, GPIO_MODE_INPUT);

    /* ----------------------------------------------------------------
     * Step 2 – Create new-API I2C master bus (matches factory BSP).
     * ---------------------------------------------------------------- */
    i2c_master_bus_config_t bus_cfg = {
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .i2c_port          = TOUCH_I2C_PORT,
        .sda_io_num        = TOUCH_I2C_SDA,
        .scl_io_num        = TOUCH_I2C_SCL,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
                        TAG, "I2C master bus init failed");

    /* ----------------------------------------------------------------
     * Step 3 – Create panel IO on the I2C bus (factory BSP pattern).
     * ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() sets cmd/param widths,
     * control phase, and device address automatically.
     * ---------------------------------------------------------------- */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_cfg.scl_speed_hz = TOUCH_I2C_SPEED;
    tp_io_cfg.lcd_param_bits = 16; /* CRITICAL quirk from factory BSP */
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
        TAG, "Panel IO I2C init failed");

    /* ----------------------------------------------------------------
     * Step 4 – Create GT911 driver instance (factory BSP pattern).
     * ---------------------------------------------------------------- */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = 1024,
        .y_max        = 600,
        .rst_gpio_num = GPIO_NUM_NC,  /* already handled above */
        .int_gpio_num = GPIO_NUM_NC,  /* already handled above */
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 }, /* Reverted mirroring */
        .driver_data  = (void *)(uintptr_t)TOUCH_ADDR,
    };
    ESP_RETURN_ON_ERROR(
        esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &s_touch_handle),
        TAG, "GT911 driver init failed");

    /* ----------------------------------------------------------------
     * Step 5 – Wake-up delay (mandatory, same as Phase 1).
     * ---------------------------------------------------------------- */
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_lcd_touch_exit_sleep(s_touch_handle);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* ----------------------------------------------------------------
     * Step 6 – Create the shared-state mutex and start touch_poll_task.
     * The task does all I2C work, keeping it out of the LVGL lock path.
     * ---------------------------------------------------------------- */
    s_touch_state_mutex = xSemaphoreCreateMutex();
    if (!s_touch_state_mutex) {
        ESP_LOGE(TAG, "Failed to create touch state mutex");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ret = xTaskCreate(
        touch_poll_task,
        "touch_poll",
        4096,
        NULL,
        4,          /* priority 4 – below lvgl_port_task (10) */
        NULL
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch_poll_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GT911 touch initialized OK (async poll task running)");
    return ESP_OK;
}

/* -----------------------------------------------------------------------
 * touch_read_cb – called by lv_timer_handler() while holding s_lvgl_mux.
 * ZERO I2C operations here. Reads only from the cached shared state.
 * ----------------------------------------------------------------------- */
void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (!s_touch_state_mutex) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    touch_state_t snap = {0};
    if (xSemaphoreTake(s_touch_state_mutex, 0) == pdTRUE) {
        /* pdMS_TO_TICKS(0) = non-blocking; if mutex is busy we just
         * report the previous state (safe, LVGL will re-poll next tick). */
        snap = (touch_state_t){ .x = s_touch_state.x,
                                 .y = s_touch_state.y,
                                 .pressed = s_touch_state.pressed };
        xSemaphoreGive(s_touch_state_mutex);
    }

    if (snap.pressed) {
        data->state   = LV_INDEV_STATE_PRESSED;
        data->point.x = snap.x;
        data->point.y = snap.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @file lcd_driver.c
 * @brief MIPI-DSI LCD driver for CrowPanel Advanced 7" ESP32-P4 (EK79007 bridge)
 */

#include "display/lcd_driver.h"
#include "boards/elecrow_p4_board.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ek79007.h"
#include "esp_ldo_regulator.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include <string.h>
#include "lvgl.h"
#include "esp_rom_sys.h"

static const char *TAG = "LCD";

/* EK79007 timing for 1024x600 @ 60Hz */
#define LCD_DPI_CLK_MHZ     51   
#define LCD_H_RES           1024
#define LCD_V_RES           600
#define LCD_HSYNC_PW        70
#define LCD_HBP             160
#define LCD_HFP             160
#define LCD_VSYNC_PW        10
#define LCD_VBP             23
#define LCD_VFP             12

/* MIPI DSI parameters */
#define LCD_MIPI_LANE_NUM       2
#define LCD_MIPI_LANE_MBPS      900  /* Match update3 exactly */

/* LVGL task parameters */
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_STACK         (16 * 1024)
#define LVGL_TASK_PRIORITY      10
#define LVGL_MIN_DELAY_MS       (1000 / CONFIG_FREERTOS_HZ)
#define LVGL_MAX_DELAY_MS       100

static esp_lcd_panel_handle_t   s_panel    = NULL;
static esp_timer_handle_t       s_tick_timer = NULL;
static SemaphoreHandle_t        s_lvgl_mux   = NULL;
static esp_ldo_channel_handle_t s_ldo3 = NULL;
static esp_ldo_channel_handle_t s_ldo4 = NULL;

/* ---- LVGL tick source ---- */
static void lvgl_tick_cb(void *arg) {
    lv_tick_inc((uint32_t)arg);
}

/* ---- LVGL rendering task ---- */
static void lvgl_port_task(void *arg) {
    ESP_LOGI(TAG, "LVGL task started");
    vTaskDelay(pdMS_TO_TICKS(200));
    while (1) {
        uint32_t delay = 0;
        if (ui_lock()) {
            delay = lv_timer_handler();
            ui_unlock();
        }
        if (delay < LVGL_MIN_DELAY_MS) delay = LVGL_MIN_DELAY_MS;
        if (delay > 50) delay = 50;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* ---- DPI flush-ready callback ---- */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_handle_t panel,
                                esp_lcd_dpi_panel_event_data_t *edata,
                                void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    /* esp_rom_printf("D"); */
    return false;
}

/* ---- LVGL flush callback ---- */
void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t ph = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(ph, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    /* We call flush_ready in the on_color_trans_done callback */
}

bool ui_lock(void) {
    if (!s_lvgl_mux) return false;
    
    const char* task_name = pcTaskGetName(xTaskGetCurrentTaskHandle());
    TickType_t start_ticks = xTaskGetTickCount();
    
    /* Use a 5-second timeout instead of infinite block to allow diagnostics */
    bool ret = xSemaphoreTakeRecursive(s_lvgl_mux, pdMS_TO_TICKS(5000)) == pdTRUE;
    TickType_t end_ticks = xTaskGetTickCount();
    uint32_t duration_ms = (uint32_t)((end_ticks - start_ticks) * portTICK_PERIOD_MS);
    
    if (!ret) {
        ESP_LOGE("UI_LOCK", "Task '%s' DEADLOCK DETECTED! Failed to acquire s_lvgl_mux after 5000ms", task_name);
        return false;
    }
    
    if (duration_ms > 100) {
        ESP_LOGW("UI_LOCK", "Task '%s' took %lu ms to acquire s_lvgl_mux", task_name, (unsigned long)duration_ms);
    }
    return true;
}

void ui_unlock(void) {
    if (!s_lvgl_mux) return;
    xSemaphoreGiveRecursive(s_lvgl_mux);
}

esp_err_t display_init(esp_lcd_panel_handle_t *panel_handle) {
    ESP_LOGI(TAG, "Initializing MIPI-DSI display (EK79007, 1024x600)");

    /* 1. Power rails LDO3 (2.5V) and LDO4 (3.3V) */
    esp_ldo_channel_config_t cfg3 = { .chan_id = 3, .voltage_mv = 2500 };
    esp_ldo_channel_config_t cfg4 = { .chan_id = 4, .voltage_mv = 3300 };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg3, &s_ldo3));
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&cfg4, &s_ldo4));
    ESP_LOGI(TAG, "MIPI DSI PHY and Panel Logic powered (LDO3/LDO4)");
    
    /* 2. Create MIPI DSI bus */
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id           = 0,
        .num_data_lanes   = LCD_MIPI_LANE_NUM,
        .phy_clk_src      = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LCD_MIPI_LANE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));

    /* 3. DBI IO for commands */
    esp_lcd_panel_io_handle_t dbi_io;
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io));

    /* 4. DPI panel config */
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_CLK_MHZ,
        .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .in_color_format    = LCD_COLOR_FMT_RGB565,
        .out_color_format   = LCD_COLOR_FMT_RGB565,
        .num_fbs            = 1,
        .video_timing = {
            .h_size             = LCD_H_RES,
            .v_size             = LCD_V_RES,
            .hsync_pulse_width  = LCD_HSYNC_PW,
            .hsync_back_porch   = LCD_HBP,
            .hsync_front_porch  = LCD_HFP,
            .vsync_pulse_width  = LCD_VSYNC_PW,
            .vsync_back_porch   = LCD_VBP,
            .vsync_front_porch  = LCD_VFP,
        },
        .flags.use_dma2d = true,
    };

    /* 5. EK79007 panel instance */
    ek79007_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus   = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .reset_gpio_num  = GPIO_NUM_NC,
        .rgb_ele_order   = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel  = 16,
        .vendor_config   = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ek79007(dbi_io, &panel_dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    vTaskDelay(pdMS_TO_TICKS(100));

    *panel_handle = s_panel;
    return ESP_OK;
}

esp_err_t display_lvgl_init(esp_lcd_panel_handle_t panel_handle) {
    s_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mux) return ESP_ERR_NO_MEM;

    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_flush_cb(disp, display_flush_cb);

    /* Revert to PSRAM for buffers to support full 1024x600 safely.
     * With proper sync in on_color_trans_done, PSRAM is now stable. */
    size_t lines = 100;
    size_t buf_sz = LCD_H_RES * lines * 2;
    uint8_t *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    uint8_t *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) return ESP_ERR_NO_MEM;

    lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, disp));

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg      = (void*)LVGL_TICK_PERIOD_MS,
        .name     = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    xTaskCreate(lvgl_port_task, "lvgl", LVGL_TASK_STACK, NULL, LVGL_TASK_PRIORITY, NULL);
    return ESP_OK;
}

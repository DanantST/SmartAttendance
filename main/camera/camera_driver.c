/**
 * @file camera_driver.c
 * @brief SC2336 MIPI-CSI camera driver for CrowPanel Advanced 7" ESP32-P4
 *
 * Architecture validated by hands-on experimentation (Phase 3):
 *  - Semaphore-based on_get_new_trans + on_trans_finished callbacks (no receive())
 *  - CSI output: RGB565 directly (ISP converts RAW8 -> RGB565)
 *  - Sensor stream enabled BEFORE CSI start
 *  - esp_cam_sensor_set_para_value() for mirror/flip/exposure/gain (NOT ioctl)
 *  - ISP: Color + AWB + AE + CCM all enabled
 *  - LDO3=2500mV (CSI PHY), LDO4=3300mV (sensor analog/IO)
 */

#include "sdkconfig.h"
#include "camera_driver.h"
#include "boards/elecrow_p4_board.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "driver/i2c_master.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "esp_ldo_regulator.h"
#include "driver/isp.h"
#include "esp_cache.h"
#include "esp_cam_sensor_xclk.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "CAM_DRV";

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static esp_cam_ctlr_handle_t    s_cam_handle  = NULL;
static esp_cam_sensor_device_t *s_cam_dev     = NULL;
static esp_sccb_io_handle_t     s_sccb_handle = NULL;
static esp_cam_sensor_xclk_handle_t s_xclk_handle = NULL;
static isp_proc_handle_t        s_isp_proc    = NULL;
static esp_ldo_channel_handle_t s_ldo3        = NULL;
static esp_ldo_channel_handle_t s_ldo4        = NULL;
static i2c_master_bus_handle_t  s_sccb_bus    = NULL;

/* Single frame buffer — semaphore signals when frame is ready */
static void *s_frame_buffer   = NULL;
static size_t s_frame_buf_size = 0;
static SemaphoreHandle_t s_frame_sem = NULL;
static esp_cam_ctlr_trans_t s_trans;
static camera_fb_t s_native_fb;
static void *s_scaled_frame_buffer = NULL;
static framesize_t s_current_framesize = FRAMESIZE_QVGA;

static uint16_t s_cam_width  = 1280;
static uint16_t s_cam_height = 720;
static uint16_t s_out_width  = 480;
static uint16_t s_out_height = 360;

/* AE statistics tracking */
static volatile uint32_t s_latest_luminance = 120;
static volatile bool s_ae_stats_ready = false;

/* ------------------------------------------------------------------ */
/* CSI callbacks (IRAM-safe)                                           */
/* ------------------------------------------------------------------ */
static IRAM_ATTR bool on_get_new_trans(esp_cam_ctlr_handle_t handle,
                                       esp_cam_ctlr_trans_t *trans,
                                       void *user_data)
{
    esp_cam_ctlr_trans_t *my = (esp_cam_ctlr_trans_t *)user_data;
    trans->buffer = my->buffer;
    trans->buflen = my->buflen;
    return false;
}

static IRAM_ATTR bool on_trans_finished(esp_cam_ctlr_handle_t handle,
                                        esp_cam_ctlr_trans_t *trans,
                                        void *user_data)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frame_sem, &woken);
    portYIELD_FROM_ISR(woken);
    return false;
}

/* ------------------------------------------------------------------ */
/* Hardware power-on                                                   */
/* ------------------------------------------------------------------ */
static void camera_hw_wakeup(void)
{
    ESP_LOGI(TAG, "Camera power-on sequence");

    /* 24 MHz XCLK */
    if (esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER,
                                     &s_xclk_handle) == ESP_OK) {
        esp_cam_sensor_xclk_config_t xclk_cfg = {
            .esp_clock_router_cfg = {
                .xclk_pin     = CAM_PIN_XCLK,
                .xclk_freq_hz = 24000000,
            }
        };
        esp_cam_sensor_xclk_start(s_xclk_handle, &xclk_cfg);
        ESP_LOGI(TAG, "XCLK 24MHz on GPIO %d", CAM_PIN_XCLK);
    }

    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CAM_PIN_PWDN) | (1ULL << CAM_PIN_RESET),
    };
    gpio_config(&io_conf);

    gpio_set_level(CAM_PIN_PWDN,  0);   /* Enable power */
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CAM_PIN_RESET, 0);   /* Assert reset */
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CAM_PIN_RESET, 1);   /* Release reset */
    vTaskDelay(pdMS_TO_TICKS(50));
}

/* ------------------------------------------------------------------ */
/* ISP init (Color + AWB + AE + CCM)                                   */
/* ------------------------------------------------------------------ */
static bool awb_stats_cb(isp_awb_ctlr_t awb_ctlr,
                         const esp_isp_awb_evt_data_t *edata,
                         void *user_data)
{
    return true;
}

static IRAM_ATTR bool ae_stats_cb(isp_ae_ctlr_t ae_ctlr,
                                   const esp_isp_ae_env_detector_evt_data_t *edata,
                                   void *user_data)
{
    /*
     * Center-weighted metering: the inner 3×3 grid cells are weighted 2×
     * so that a face standing in the middle of the frame drives the exposure
     * target rather than a bright window behind the subject.
     */
    uint32_t sum = 0, weight = 0;
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            int w = (i >= 1 && i <= 3 && j >= 1 && j <= 3) ? 2 : 1;
            sum    += edata->ae_result.luminance[i][j] * w;
            weight += w;
        }
    }
    s_latest_luminance = sum / weight;   /* weighted average: 0..255 */
    s_ae_stats_ready = true;
    return false;
}

static esp_err_t isp_init_proc(void)
{
    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz                = 80 * 1000 * 1000,
        .input_data_source     = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type = ISP_COLOR_RAW8,
        .output_data_color_type= ISP_COLOR_RGB565,
        .bayer_order           = COLOR_RAW_ELEMENT_ORDER_BGGR, /* SC2336 native, no mirror */
        .has_line_start_packet = false,
        .has_line_end_packet   = false,
        .h_res                 = s_cam_width,
        .v_res                 = s_cam_height,
    };
    esp_err_t err = esp_isp_new_processor(&isp_cfg, &s_isp_proc);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ISP new: %s", esp_err_to_name(err)); return err; }

    err = esp_isp_enable(s_isp_proc);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ISP enable: %s", esp_err_to_name(err)); return err; }

    /* Color processing */
    esp_isp_color_config_t color_cfg = {
        .color_contrast   = { .integer = 1, .decimal = 0  },
        .color_saturation = { .integer = 1, .decimal = 0  },
        .color_hue        = 0,
        .color_brightness = 0,
    };
    esp_isp_color_configure(s_isp_proc, &color_cfg);
    esp_isp_color_enable(s_isp_proc);

    /* Auto White Balance */
    esp_isp_awb_config_t awb_cfg = {
        .sample_point = ISP_AWB_SAMPLE_POINT_AFTER_CCM,
        .white_patch  = {
            .luminance        = { .min = 0,    .max = 255  },
            .red_green_ratio  = { .min = 0.7f, .max = 1.0f },
            .blue_green_ratio = { .min = 0.7f, .max = 1.0f },
        },
    };
    isp_awb_ctlr_t awb_ctlr = NULL;
    err = esp_isp_new_awb_controller(s_isp_proc, &awb_cfg, &awb_ctlr);
    if (err == ESP_OK) {
        esp_isp_awb_cbs_t awb_cb = { .on_statistics_done = awb_stats_cb };
        esp_err_t r_err = esp_isp_awb_register_event_callbacks(awb_ctlr, &awb_cb, NULL);
        esp_err_t e_err = esp_isp_awb_controller_enable(awb_ctlr);
        esp_err_t s_err = esp_isp_awb_controller_start_continuous_statistics(awb_ctlr);
        ESP_LOGI(TAG, "ISP AWB: register=%s, enable=%s, start=%s",
                 esp_err_to_name(r_err), esp_err_to_name(e_err), esp_err_to_name(s_err));
    } else {
        ESP_LOGE(TAG, "Failed to create AWB controller: %s", esp_err_to_name(err));
    }

    /* Auto Exposure */
    esp_isp_ae_config_t ae_cfg = {
        .sample_point = ISP_AE_SAMPLE_POINT_AFTER_GAMMA,
    };
    isp_ae_ctlr_t ae_ctlr = NULL;
    err = esp_isp_new_ae_controller(s_isp_proc, &ae_cfg, &ae_ctlr);
    if (err == ESP_OK) {
        esp_isp_ae_env_config_t env_cfg = {
            .interval = 3, /* trigger statistics callback every 3 frames */
        };
        esp_err_t c_err = esp_isp_ae_controller_set_env_detector(ae_ctlr, &env_cfg);
        
        esp_isp_ae_env_detector_evt_cbs_t ae_cbs = {
            .on_env_statistics_done = ae_stats_cb,
        };
        esp_err_t r_err = esp_isp_ae_env_detector_register_event_callbacks(ae_ctlr, &ae_cbs, NULL);
        esp_err_t e_err = esp_isp_ae_controller_enable(ae_ctlr);
        esp_err_t s_err = esp_isp_ae_controller_start_continuous_statistics(ae_ctlr);
        ESP_LOGI(TAG, "ISP AE: set_env=%s, register=%s, enable=%s, start=%s",
                 esp_err_to_name(c_err), esp_err_to_name(r_err), esp_err_to_name(e_err), esp_err_to_name(s_err));
    } else {
        ESP_LOGE(TAG, "Failed to create AE controller: %s", esp_err_to_name(err));
    }

    /* Color Correction Matrix */
    esp_isp_ccm_config_t ccm_cfg = {
        .matrix     = { {1.0f, 0.0f, 0.0f},
                        {0.0f, 0.5f, 0.0f},
                        {0.0f, 0.0f, 1.0f} },
        .saturation = false,
    };
    esp_isp_ccm_configure(s_isp_proc, &ccm_cfg);
    esp_isp_ccm_enable(s_isp_proc);

    ESP_LOGI(TAG, "ISP ready (Color+AWB+AE+CCM)");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
esp_err_t camera_init(void)
{
    ESP_LOGI(TAG, "Initializing SC2336 camera (validated sequence)");

    /* 1. Frame semaphore */
    s_frame_sem = xSemaphoreCreateBinary();
    if (!s_frame_sem) return ESP_ERR_NO_MEM;

    /* 2. LDO — validated: LDO3=2500mV (CSI PHY), LDO4=3300mV (sensor) */
    esp_ldo_channel_config_t ldo3_cfg = { .chan_id = 3, .voltage_mv = 2500 };
    esp_ldo_channel_config_t ldo4_cfg = { .chan_id = 4, .voltage_mv = 3300 };
    esp_ldo_acquire_channel(&ldo3_cfg, &s_ldo3);
    esp_ldo_acquire_channel(&ldo4_cfg, &s_ldo4);
    ESP_LOGI(TAG, "LDO3=2500mV LDO4=3300mV");

    /* 3. Power on */
    camera_hw_wakeup();

    /* 4. SCCB I2C bus (using board definition pins and port) */
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port    = CAM_SCCB_I2C_PORT,
        .sda_io_num  = CAM_PIN_SCCB_SDA,
        .scl_io_num  = CAM_PIN_SCCB_SCL,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &s_sccb_bus));

    /* 5. Detect sensor — try addr 0x30 and 0x36 */
    esp_cam_sensor_config_t cam_cfg = {
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
        .reset_pin   = -1,
        .pwdn_pin    = -1,
        .xclk_pin    = -1,
    };
    uint8_t addr_list[] = { 0x30, 0x36 };
    bool detected = false;

    for (int a = 0; a < (int)sizeof(addr_list) && !detected; a++) {
        for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start;
             p < &__esp_cam_sensor_detect_fn_array_end; ++p) {
            sccb_i2c_config_t sccb_cfg = {
                .scl_speed_hz    = 100000,
                .device_address  = addr_list[a],
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            };
            if (sccb_new_i2c_io(s_sccb_bus, &sccb_cfg, &s_sccb_handle) != ESP_OK) continue;
            cam_cfg.sccb_handle = s_sccb_handle;
            s_cam_dev = (*(p->detect))(&cam_cfg);
            if (s_cam_dev) {
                if (p->port != ESP_CAM_SENSOR_MIPI_CSI) {
                    ESP_LOGE(TAG, "Wrong interface"); esp_sccb_del_i2c_io(s_sccb_handle);
                    s_sccb_handle = NULL; s_cam_dev = NULL; continue;
                }
                ESP_LOGI(TAG, "Detected sensor at 0x%02x", addr_list[a]);
                detected = true; break;
            }
            esp_sccb_del_i2c_io(s_sccb_handle);
            s_sccb_handle = NULL;
        }
    }
    if (!detected) { ESP_LOGE(TAG, "Sensor not found!"); return ESP_FAIL; }

    /* 6. Query and select RAW8 1280x720 format */
    esp_cam_sensor_format_array_t fmt_arr = {0};
    esp_cam_sensor_query_format(s_cam_dev, &fmt_arr);
    const esp_cam_sensor_format_t *target_fmt = NULL;
    for (int i = 0; i < (int)fmt_arr.count; i++) {
        ESP_LOGI(TAG, "  fmt[%d] %s (%dx%d)", i,
                 fmt_arr.format_array[i].name,
                 fmt_arr.format_array[i].width,
                 fmt_arr.format_array[i].height);
        if (strstr(fmt_arr.format_array[i].name, "RAW8_1280x720")) {
            target_fmt = &fmt_arr.format_array[i];
        }
    }
    if (!target_fmt && fmt_arr.count > 0) target_fmt = &fmt_arr.format_array[0];
    if (!target_fmt) { ESP_LOGE(TAG, "No valid format"); return ESP_FAIL; }

    ESP_LOGI(TAG, "Using format: %s", target_fmt->name);
    if (esp_cam_sensor_set_format(s_cam_dev, target_fmt) != ESP_OK) {
        ESP_LOGE(TAG, "set_format failed"); return ESP_FAIL;
    }
    s_cam_width  = target_fmt->width;
    s_cam_height = target_fmt->height;

    /* 7. Disable HMIRROR & VFLIP (keep native BGGR Bayer order) */
    int mirror_off = 0;
    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_HMIRROR, &mirror_off, sizeof(int));
    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_VFLIP,   &mirror_off, sizeof(int));
    ESP_LOGI(TAG, "HMIRROR=0 VFLIP=0 (BGGR)");

    /* 8. SC2336 exposure baseline:
     *    exp_val=280 (shutter lines for brighter indoor lighting)
     *    gain=15 (slight gain boost for clear visibility)
     */
    uint32_t exp_val = 280;
    uint32_t gain    = 15;
    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_EXPOSURE_VAL, &exp_val, sizeof(uint32_t));
    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_GAIN,         &gain,    sizeof(uint32_t));
    ESP_LOGI(TAG, "Exposure baseline: val=%"PRIu32" gain_index=%"PRIu32, exp_val, gain);

    /* 9. ISP (must be before CSI controller) */
    if (isp_init_proc() != ESP_OK) return ESP_FAIL;

    /* 10. Frame buffer (output is RGB565: width*height*2) */
    /* ESP32-P4 external memory cache line = 64 bytes */
    const size_t cache_line = 64;
    s_frame_buf_size = s_cam_width * s_cam_height * 2; /* RGB565 */
    s_frame_buffer   = heap_caps_aligned_calloc(
        cache_line, 1, s_frame_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frame_buffer) { ESP_LOGE(TAG, "Buffer alloc failed"); return ESP_ERR_NO_MEM; }

    s_scaled_frame_buffer = heap_caps_malloc(640 * 480 * 2, MALLOC_CAP_SPIRAM);
    if (!s_scaled_frame_buffer) {
        ESP_LOGE(TAG, "Scaled buffer alloc failed");
        heap_caps_free(s_frame_buffer);
        s_frame_buffer = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_trans.buffer = s_frame_buffer;
    s_trans.buflen = s_frame_buf_size;

    /* 11. CSI Controller */
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id                = 1,
        .clk_src                = MIPI_CSI_PHY_CLK_SRC_DEFAULT,
        .h_res                  = s_cam_width,
        .v_res                  = s_cam_height,
        .lane_bit_rate_mbps     = 200,
        .input_data_color_type  = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565, /* ISP converts */
        .data_lane_num          = 2,
        .byte_swap_en           = false,
        .queue_items            = 1,
    };
    if (esp_cam_new_csi_ctlr(&csi_cfg, &s_cam_handle) != ESP_OK) {
        ESP_LOGE(TAG, "CSI ctlr create failed"); return ESP_FAIL;
    }

    /* 12. Register callbacks — semaphore-based */
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = on_get_new_trans,
        .on_trans_finished = on_trans_finished,
    };
    esp_cam_ctlr_register_event_callbacks(s_cam_handle, &cbs, &s_trans);

    /* 13. Enable CSI */
    if (esp_cam_ctlr_enable(s_cam_handle) != ESP_OK) {
        ESP_LOGE(TAG, "CSI enable failed"); return ESP_FAIL;
    }

    /* 14. Start sensor stream BEFORE starting CSI */
    int stream_en = 1;
    esp_cam_sensor_ioctl(s_cam_dev, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_en);
    ESP_LOGI(TAG, "Sensor streaming started");

    /* 15. Flush cache and start CSI DMA */
    memset(s_frame_buffer, 0, s_frame_buf_size);
    esp_cache_msync(s_frame_buffer, s_frame_buf_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (esp_cam_ctlr_start(s_cam_handle) != ESP_OK) {
        ESP_LOGE(TAG, "CSI start failed"); return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera ready (%dx%d -> %dx%d RGB565)",
             s_cam_width, s_cam_height, s_out_width, s_out_height);
    return ESP_OK;
}

esp_err_t camera_deinit(void)
{
    if (s_cam_dev) {
        int stream_off = 0;
        esp_cam_sensor_ioctl(s_cam_dev, ESP_CAM_SENSOR_IOC_S_STREAM, &stream_off);
    }
    if (s_cam_handle) {
        esp_cam_ctlr_stop(s_cam_handle);
        esp_cam_ctlr_disable(s_cam_handle);
        esp_cam_ctlr_del(s_cam_handle);
        s_cam_handle = NULL;
    }
    if (s_isp_proc) {
        esp_isp_disable(s_isp_proc);
        esp_isp_del_processor(s_isp_proc);
        s_isp_proc = NULL;
    }
    if (s_ldo3) { esp_ldo_release_channel(s_ldo3); s_ldo3 = NULL; }
    if (s_ldo4) { esp_ldo_release_channel(s_ldo4); s_ldo4 = NULL; }
    if (s_sccb_handle) { esp_sccb_del_i2c_io(s_sccb_handle); s_sccb_handle = NULL; }
    if (s_frame_sem)   { vSemaphoreDelete(s_frame_sem); s_frame_sem = NULL; }
    if (s_frame_buffer) { heap_caps_free(s_frame_buffer); s_frame_buffer = NULL; }
    if (s_scaled_frame_buffer) { heap_caps_free(s_scaled_frame_buffer); s_scaled_frame_buffer = NULL; }
    return ESP_OK;
}

camera_fb_t *camera_capture_frame(void)
{
    if (!s_cam_handle || !s_frame_sem) return NULL;
    if (xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
        /* Invalidate CPU-side cache so the CPU reads the fresh pixels
         * written by the CSI DMA (M2C = Memory-to-CPU direction). */
        esp_cache_msync(s_frame_buffer, s_frame_buf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        
        /*
         * Fast Software AE Loop — runs every captured frame (~33ms at 30fps).
         *
         * SC2336 driver parameter limits:
         *   exposure_val : 4 … 720  (shutter line units, 720 = 1/30s frame time)
         *   gain         : 0 … 140  (gain_index: 0 = 1.0x gain, 140 = ~16.0x gain)
         */
        if (s_ae_stats_ready && s_cam_dev) {
            s_ae_stats_ready = false;
            uint32_t lum = s_latest_luminance;

            static uint32_t s_exp_val  = 280;
            static uint32_t s_gain     = 15;

            const uint32_t TARGET_LUM = 145;
            const uint32_t DEADBAND   = 12;   /* ±12 counts before reacting */
            const uint32_t EXP_MIN    = 4;
            const uint32_t EXP_MAX    = 720;
            const uint32_t GAIN_MIN   = 0;
            const uint32_t GAIN_MAX   = 140;

            if (lum < TARGET_LUM - DEADBAND) {
                /* --- Too dark: brighten (increase exposure first, then gain) --- */
                uint32_t err = (TARGET_LUM - DEADBAND) - lum;
                uint32_t step = (err > 60) ? 30 : (err > 20) ? 15 : 4;

                if (s_exp_val < EXP_MAX) {
                    s_exp_val = (s_exp_val + step * 3 > EXP_MAX) ? EXP_MAX : s_exp_val + step * 3;
                    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_EXPOSURE_VAL,
                                                  &s_exp_val, sizeof(uint32_t));
                } else if (s_gain < GAIN_MAX) {
                    s_gain = (s_gain + step > GAIN_MAX) ? GAIN_MAX : s_gain + step;
                    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_GAIN,
                                                  &s_gain, sizeof(uint32_t));
                }
                ESP_LOGI(TAG, "AE dark  lum=%lu err=%lu exp=%lu gain=%lu",
                         (unsigned long)lum, (unsigned long)err,
                         (unsigned long)s_exp_val, (unsigned long)s_gain);

            } else if (lum > TARGET_LUM + DEADBAND) {
                /* --- Too bright: darken (decrease gain first, then exposure) --- */
                uint32_t err = lum - (TARGET_LUM + DEADBAND);
                uint32_t step = (err > 60) ? 30 : (err > 20) ? 15 : 4;

                if (s_gain > GAIN_MIN) {
                    s_gain = (s_gain < GAIN_MIN + step) ? GAIN_MIN : s_gain - step;
                    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_GAIN,
                                                  &s_gain, sizeof(uint32_t));
                } else if (s_exp_val > EXP_MIN) {
                    s_exp_val = (s_exp_val < EXP_MIN + step * 3) ? EXP_MIN : s_exp_val - step * 3;
                    esp_cam_sensor_set_para_value(s_cam_dev, ESP_CAM_SENSOR_EXPOSURE_VAL,
                                                  &s_exp_val, sizeof(uint32_t));
                }
                ESP_LOGI(TAG, "AE bright lum=%lu err=%lu exp=%lu gain=%lu",
                         (unsigned long)lum, (unsigned long)err,
                         (unsigned long)s_exp_val, (unsigned long)s_gain);
            }
        }
        
        uint16_t req_w = (s_current_framesize == FRAMESIZE_QVGA) ? 320 : 640;
        uint16_t req_h = (s_current_framesize == FRAMESIZE_QVGA) ? 240 : 480;

        uint16_t *src = (uint16_t *)s_frame_buffer;
        uint16_t *dst = (uint16_t *)s_scaled_frame_buffer;
        
        float x_ratio = (float)s_cam_width / req_w;
        float y_ratio = (float)s_cam_height / req_h;
        
        for (int y = 0; y < req_h; y++) {
            int src_y = (int)(y * y_ratio);
            for (int x = 0; x < req_w; x++) {
                int src_x = (int)(x * x_ratio);
                dst[y * req_w + x] = src[src_y * s_cam_width + src_x];
            }
        }
        
        s_native_fb.buf    = s_scaled_frame_buffer;
        s_native_fb.len    = req_w * req_h * 2;
        s_native_fb.width  = req_w;
        s_native_fb.height = req_h;
        s_native_fb.format = PIXFORMAT_RGB565;
        return &s_native_fb;
    }
    return NULL;
}

camera_fb_t *camera_capture_with_autofocus(void)
{
    return camera_capture_frame();
}

void camera_return_frame(camera_fb_t *fb)
{
    (void)fb; /* Buffer is reused in-place by on_get_new_trans */
}

void camera_set_framesize(framesize_t framesize) { 
    s_current_framesize = framesize;
}
void camera_continuous_autofocus(bool enable)    { (void)enable; }

SemaphoreHandle_t camera_get_frame_sem(void)
{
    return s_frame_sem;
}

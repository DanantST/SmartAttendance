/**
 * @file ov5647_autofocus.c
 * @brief OV5647 autofocus implementation via DW9714 VCM motor
 *
 * The OV5647 camera module uses a DW9714 Voice Coil Motor for autofocus.
 * On ESP32-P4, autofocus is controlled through the esp_cam_sensor framework
 * which manages both the sensor and the motor driver via SCCB/I2C.
 *
 * Note: The OV5647 is a MIPI-CSI only sensor and does NOT support DVP.
 * Therefore the non-P4 path always returns ESP_ERR_NOT_SUPPORTED.
 */

#include "sdkconfig.h"
#include "ov5647_autofocus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

static const char *TAG __attribute__((unused)) = "OV5647_AF";

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include "esp_cam_sensor.h"

static esp_cam_sensor_device_t *s_cam_dev = NULL;
static bool s_af_continuous = false;

esp_err_t ov5647_af_init(esp_cam_sensor_device_t *cam_dev) {
    if (!cam_dev) {
        ESP_LOGE(TAG, "Invalid camera device handle");
        return ESP_ERR_INVALID_ARG;
    }
    s_cam_dev = cam_dev;

    /* The DW9714 VCM motor is initialized automatically by the
     * esp_cam_sensor framework when CONFIG_CAM_MOTOR_DW9714 is enabled.
     * The motor is detected on the same I2C bus as the sensor. */

    ESP_LOGI(TAG, "OV5647 autofocus initialized (DW9714 VCM)");
    return ESP_OK;
}

esp_err_t ov5647_af_single(void) {
    if (!s_cam_dev) return ESP_FAIL;

    /* Trigger single autofocus via esp_cam_sensor AF API */
    int af_start = 1;
    esp_err_t ret = esp_cam_sensor_set_para_value(s_cam_dev,
                        ESP_CAM_SENSOR_AF_START, &af_start, sizeof(af_start));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Single AF trigger failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Wait for focus to settle */
    vTaskDelay(pdMS_TO_TICKS(AF_SETTLE_TIME_MS));

    ESP_LOGD(TAG, "Single AF triggered");
    return ESP_OK;
}

esp_err_t ov5647_af_continuous(bool enable) {
    if (!s_cam_dev) return ESP_FAIL;

    int af_auto = enable ? 1 : 0;
    esp_err_t ret = esp_cam_sensor_set_para_value(s_cam_dev,
                        ESP_CAM_SENSOR_AF_AUTO, &af_auto, sizeof(af_auto));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Continuous AF %s failed: %s",
                 enable ? "enable" : "disable", esp_err_to_name(ret));
        return ret;
    }

    s_af_continuous = enable;
    ESP_LOGI(TAG, "Continuous AF %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

uint8_t ov5647_af_get_status(void) {
    if (!s_cam_dev) return AF_STATUS_IDLE;

    int status = 0;
    esp_err_t ret = esp_cam_sensor_get_para_value(s_cam_dev,
                        ESP_CAM_SENSOR_AF_STATUS, &status, sizeof(status));
    if (ret != ESP_OK) {
        return AF_STATUS_IDLE;
    }

    /* Map sensor AF status to our status defines */
    if (status == 0) return AF_STATUS_IDLE;
    if (status == 1) return AF_STATUS_FOCUSED;
    if (status == 2) return AF_STATUS_FOCUSING;
    return AF_STATUS_FAILED;
}

bool ov5647_af_is_focused(void) {
    return (ov5647_af_get_status() == AF_STATUS_FOCUSED);
}

#else

/* OV5647 is a MIPI-CSI only sensor — not supported on non-P4 DVP platforms */
esp_err_t ov5647_af_init(void *cam_dev) {
    ESP_LOGW(TAG, "OV5647 not supported on this platform (MIPI-CSI only)");
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t ov5647_af_single(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t ov5647_af_continuous(bool enable) {
    return ESP_ERR_NOT_SUPPORTED;
}
uint8_t ov5647_af_get_status(void) {
    return AF_STATUS_IDLE;
}
bool ov5647_af_is_focused(void) {
    return false;
}

#endif

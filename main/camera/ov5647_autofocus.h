/**
 * @file ov5647_autofocus.h
 * @brief OV5647 autofocus control via DW9714 VCM motor
 *
 * The OV5647 is a MIPI-CSI only sensor. Autofocus is achieved via
 * the DW9714 Voice Coil Motor (VCM) actuator, controlled through
 * the esp_cam_sensor / esp_cam_motor framework on ESP32-P4.
 */

#ifndef OV5647_AUTOFOCUS_H
#define OV5647_AUTOFOCUS_H

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp_cam_sensor.h"
#endif

/* AF status values */
#define AF_STATUS_IDLE          0x00
#define AF_STATUS_FOCUSING      0x20
#define AF_STATUS_FOCUSED       0x10
#define AF_STATUS_FAILED        0x40

/**
 * @brief Initialize autofocus module
 * @param cam_dev camera sensor device handle (ESP32-P4 MIPI CSI)
 * @return ESP_OK on success
 */
#if defined(CONFIG_IDF_TARGET_ESP32P4)
esp_err_t ov5647_af_init(esp_cam_sensor_device_t *cam_dev);
#else
esp_err_t ov5647_af_init(void *cam_dev);
#endif

/**
 * @brief Trigger single autofocus
 * @return ESP_OK on success
 */
esp_err_t ov5647_af_single(void);

/**
 * @brief Enable/disable continuous autofocus
 * @param enable true to enable continuous AF
 * @return ESP_OK on success
 */
esp_err_t ov5647_af_continuous(bool enable);

/**
 * @brief Get current autofocus status
 * @return one of AF_STATUS_* values
 */
uint8_t ov5647_af_get_status(void);

/**
 * @brief Check if focus is achieved
 * @return true if focused
 */
bool ov5647_af_is_focused(void);


#ifdef __cplusplus
}
#endif

#endif /* OV5647_AUTOFOCUS_H */

/**
 * @file camera_driver.h
 * @brief OV5647 MIPI-CSI camera driver with autofocus support for ESP32-P4
 */

#ifndef CAMERA_DRIVER_H
#define CAMERA_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef enum {
    PIXFORMAT_RGB565,
    PIXFORMAT_YUV422,
    PIXFORMAT_GRAYSCALE,
    PIXFORMAT_RAW8,
    PIXFORMAT_JPEG,
} pixformat_t;

typedef enum {
    FRAMESIZE_QVGA,
    FRAMESIZE_VGA,
} framesize_t;

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t width;
    size_t height;
    pixformat_t format;
} camera_fb_t;
#include "esp_err.h"

/**
 * @brief Initialize camera with autofocus
 * @return ESP_OK on success
 */
esp_err_t camera_init(void);

/**
 * @brief Deinitialize camera and release resources
 * @return ESP_OK on success
 */
esp_err_t camera_deinit(void);

/**
 * @brief Capture a frame from camera
 * @return camera_fb_t pointer, must be freed with camera_return_frame()
 */
camera_fb_t* camera_capture_frame(void);

/**
 * @brief Capture a frame after triggering autofocus
 * @return camera_fb_t pointer, must be freed with camera_return_frame()
 */
camera_fb_t* camera_capture_with_autofocus(void);

/**
 * @brief Return frame buffer to driver
 * @param fb frame buffer to return
 */
void camera_return_frame(camera_fb_t *fb);

/**
 * @brief Set camera frame size (QVGA, VGA, etc.)
 * @param framesize desired framesize_t
 */
void camera_set_framesize(framesize_t framesize);

/**
 * @brief Enable/disable continuous autofocus
 * @param enable true to enable continuous AF
 */
void camera_continuous_autofocus(bool enable);

/**
 * @brief Get the frame-ready binary semaphore handle.
 * Callers can use this to drain a stale "give" before starting
 * a synchronised capture (e.g. enrollment) so the first frame
 * returned is guaranteed to be freshly DMA-filled.
 * @return SemaphoreHandle_t (do not delete or use recursively)
 */
SemaphoreHandle_t camera_get_frame_sem(void);


#ifdef __cplusplus
}
#endif

#endif /* CAMERA_DRIVER_H */
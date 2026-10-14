/**
 * @file face_detector.h
 * @brief Face detection using ESP-DL
 */

#ifndef FACE_DETECTOR_H
#define FACE_DETECTOR_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include "camera_driver.h"
#include "detection/face_alignment.h"   // for detected_face_t

/**
 * @brief Result of face detection
 */
typedef struct {
    detected_face_t faces[8];   // up to 8 faces
    int face_count;
} detection_result_t;

/**
 * @brief Initialize face detector
 * @return ESP_OK on success
 */
esp_err_t face_detector_init(void);

/**
 * @brief Run face detection on a camera frame
 * @param fb camera frame buffer
 * @param result output detection result
 * @return ESP_OK if detection completed (may have zero faces)
 */
esp_err_t face_detector_run(camera_fb_t *fb, detection_result_t *result);

/**
 * @brief Compute sharpness of face region using Laplacian variance
 * @param fb camera frame buffer
 * @param face detected face structure with bounding box
 * @return sharpness value (higher = sharper)
 */
float face_detector_compute_sharpness(camera_fb_t *fb, detected_face_t *face);

/**
 * @brief Compute average brightness of face region
 * @param fb camera frame buffer
 * @param face detected face structure
 * @return average luminance (0-255)
 */
float face_detector_compute_brightness(camera_fb_t *fb, detected_face_t *face);

/**
 * @brief Estimate yaw angle from landmarks
 * @param face detected face with landmarks
 * @return yaw angle in degrees (positive = right turn)
 */
float face_detector_compute_yaw(detected_face_t *face);

/**
 * @brief Estimate percentage of saturated/glare pixels in face bounding box
 * @return fraction in range [0.0, 1.0] of pixels with luminance > 240
 */
float face_detector_compute_glare_ratio(camera_fb_t *fb, detected_face_t *face);

/**
 * @brief Estimate mouth opening ratio: (mouth_y - nose_y) / eye_dist
 * Nominal closed mouth: ~0.50 - 0.70. Wide open / jaw drop: > 0.82.
 */
float face_detector_compute_mouth_ratio(detected_face_t *face);


#ifdef __cplusplus
}
#endif

#endif /* FACE_DETECTOR_H */
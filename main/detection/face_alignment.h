/**
 * @file face_alignment.h
 * @brief Face alignment using 5-point landmarks
 */

#ifndef FACE_ALIGNMENT_H
#define FACE_ALIGNMENT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include "camera_driver.h"

typedef struct {
    int x, y, w, h;         // bounding box
    float landmarks[10];    // 5 points (x0,y0,x1,y1,...,x4,y4)
    float confidence;
} detected_face_t;

typedef struct {
    uint8_t *data;          // aligned face image (grayscale, 112x112)
    int width;
    int height;
} aligned_face_t;

/**
 * @brief Align face to canonical 112x112 using affine transformation
 * @param fb camera frame buffer
 * @param face detected face with landmarks
 * @param aligned output aligned face structure (must be freed with face_alignment_free)
 * @return ESP_OK on success
 */
esp_err_t face_alignment_align(camera_fb_t *fb, detected_face_t *face, aligned_face_t *aligned);

/**
 * @brief Free memory allocated for aligned face
 * @param aligned aligned face structure
 */
void face_alignment_free(aligned_face_t *aligned);


#ifdef __cplusplus
}
#endif

#endif /* FACE_ALIGNMENT_H */
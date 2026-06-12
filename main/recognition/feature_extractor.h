/**
 * @file feature_extractor.h
 * @brief Face embedding extraction using MobileFaceNet
 */

#ifndef FEATURE_EXTRACTOR_H
#define FEATURE_EXTRACTOR_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include "detection/face_alignment.h"

typedef struct {
    int8_t values[128];
} face_embedding_t;

/**
 * @brief Initialize feature extractor (load model)
 * @return ESP_OK on success
 */
esp_err_t feature_extractor_init(void);

/**
 * @brief Extract embedding from aligned face
 * @param aligned aligned face (112x112 grayscale)
 * @param embedding output embedding
 * @return ESP_OK on success
 */
esp_err_t feature_extractor_run(aligned_face_t *aligned, face_embedding_t *embedding);


#ifdef __cplusplus
}
#endif

#endif /* FEATURE_EXTRACTOR_H */
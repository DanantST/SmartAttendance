/**
 * @file feature_extractor.c
 * @brief Face embedding extraction using ESP-DL MobileFaceNet
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

#include "recognition/feature_extractor.h"
#include "config.h"
#include "esp_log.h"
#include "dl_image.hpp"
#include "human_face_recognition.hpp"
#include "dl_tensor_base.hpp"
#include <vector>

#pragma GCC diagnostic pop

static const char* TAG = "FEATURE";
static HumanFaceFeat* s_extractor = nullptr;

esp_err_t feature_extractor_init(void) {
    ESP_LOGI(TAG, "Initializing feature extractor (MobileFaceNet)");
    
    /* Create extractor instance */
    s_extractor = new HumanFaceFeat();
    
    if (!s_extractor) {
        ESP_LOGE(TAG, "Failed to initialize feature extractor");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Feature extractor initialized");
    return ESP_OK;
}

esp_err_t feature_extractor_run(aligned_face_t *aligned, face_embedding_t *embedding) {
    if (!s_extractor || !aligned || !embedding) return ESP_ERR_INVALID_ARG;
    
    /* Prepare input image */
    dl::image::img_t input_img;
    input_img.data = aligned->data;
    input_img.width = aligned->width;
    input_img.height = aligned->height;
    input_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
    
    /* Run inference */
    /* Pass a dummy landmark array since image is pre-aligned */
    std::vector<int> landmarks = {34, 45, 78, 45, 56, 67, 45, 84, 67, 84};
    dl::TensorBase *output_base = s_extractor->run(input_img, landmarks);
    
    if (!output_base) {
        ESP_LOGE(TAG, "Feature extraction failed");
        return ESP_FAIL;
    }

    int8_t* out_data = output_base->get_element_ptr<int8_t>();
    int elements = output_base->get_size();
    
    /* Copy data to output embedding structure */
    for (int i = 0; i < EMBEDDING_DIM && i < elements; i++) {
        embedding->values[i] = out_data[i];
    }
    
    return ESP_OK;
}
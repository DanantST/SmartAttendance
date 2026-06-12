/**
 * @file face_detector.cpp
 * @brief Face detection using ESP-DL face detection model
 */

#include "face_detector.h"
#include "config.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#include <vector>
#include <list>
#include <math.h>
#include <string.h>

static const char* TAG = "FACE_DET";
static HumanFaceDetect* s_detector = nullptr;

esp_err_t face_detector_init(void) {
    ESP_LOGI(TAG, "Initializing face detector");
    
    /* Create face detector instance */
    s_detector = new HumanFaceDetect();
    
    if (!s_detector) {
        ESP_LOGE(TAG, "Failed to initialize face detector");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Face detector initialized");
    return ESP_OK;
}

esp_err_t face_detector_run(camera_fb_t *fb, detection_result_t *result) {
    if (!s_detector || !fb || !result) return ESP_ERR_INVALID_ARG;
    
    /* Prepare input tensor from camera frame */
    /* The frame is RGB565 */
    dl::image::img_t input_img;
    input_img.data = fb->buf;
    input_img.width = fb->width;
    input_img.height = fb->height;
    input_img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
    
    /* Run detection */
    auto &detections = s_detector->run(input_img);
    
    /* Convert to our structure */
    result->face_count = 0;
    for (auto it = detections.begin(); it != detections.end() && result->face_count < 8; ++it) {
        auto& d = *it;
        if (d.score < FACE_DETECT_CONFIDENCE_MIN) continue;  /* Confidence threshold from config.h */
        if ((d.box[2] - d.box[0]) < FACE_MIN_SIZE_PX || (d.box[3] - d.box[1]) < FACE_MIN_SIZE_PX) continue; /* Min face size from config.h */
        
        result->faces[result->face_count].x = d.box[0];
        result->faces[result->face_count].y = d.box[1];
        result->faces[result->face_count].w = d.box[2] - d.box[0];
        result->faces[result->face_count].h = d.box[3] - d.box[1];
        result->faces[result->face_count].confidence = d.score;
        
        /* Extract landmarks (if model provides them) */
        if (d.keypoint.size() >= 10) {
            for (int j = 0; j < 5; j++) {
                result->faces[result->face_count].landmarks[j*2] = d.keypoint[j*2];
                result->faces[result->face_count].landmarks[j*2+1] = d.keypoint[j*2+1];
            }
        } else {
            /* Fallback: approximate landmarks from bounding box */
            int cx = d.box[0] + d.box[2]/2;
            int cy = d.box[1] + d.box[3]/2;
            (void)cy;
            int eye_y = d.box[1] + d.box[3]*0.3;
            int nose_y = d.box[1] + d.box[3]*0.6;
            int mouth_y = d.box[1] + d.box[3]*0.8;
            result->faces[result->face_count].landmarks[0] = cx - d.box[2]*0.2;
            result->faces[result->face_count].landmarks[1] = eye_y;
            result->faces[result->face_count].landmarks[2] = cx + d.box[2]*0.2;
            result->faces[result->face_count].landmarks[3] = eye_y;
            result->faces[result->face_count].landmarks[4] = cx;
            result->faces[result->face_count].landmarks[5] = nose_y;
            result->faces[result->face_count].landmarks[6] = cx - d.box[2]*0.1;
            result->faces[result->face_count].landmarks[7] = mouth_y;
            result->faces[result->face_count].landmarks[8] = cx + d.box[2]*0.1;
            result->faces[result->face_count].landmarks[9] = mouth_y;
        }
        
        result->face_count++;
    }
    
    return ESP_OK;
}

/* Quality functions */
float face_detector_compute_sharpness(camera_fb_t *fb, detected_face_t *face) {
    float mean = 0, variance = 0;
    int n = face->w * face->h;
    if (n <= 0) return 0.0f;
    uint16_t *pixels = (uint16_t *)fb->buf;
    for (int y = 0; y < face->h; y++) {
        for (int x = 0; x < face->w; x++) {
            int px_idx = (face->y + y) * fb->width + (face->x + x);
            if (px_idx < fb->width * fb->height) {
                uint8_t g = (pixels[px_idx] >> 5) & 0x3F;
                mean += g;
            }
        }
    }
    mean /= (float)n;
    for (int y = 0; y < face->h; y++) {
        for (int x = 0; x < face->w; x++) {
            int px_idx = (face->y + y) * fb->width + (face->x + x);
            if (px_idx < fb->width * fb->height) {
                uint8_t g = (pixels[px_idx] >> 5) & 0x3F;
                variance += (g - mean) * (g - mean);
            }
        }
    }
    return variance / (float)n;
}

float face_detector_compute_brightness(camera_fb_t *fb, detected_face_t *face) {
    float mean = 0;
    int n = face->w * face->h;
    if (n <= 0) return 0.0f;
    uint16_t *pixels = (uint16_t *)fb->buf;
    for (int y = 0; y < face->h; y++) {
        for (int x = 0; x < face->w; x++) {
            int px_idx = (face->y + y) * fb->width + (face->x + x);
            if (px_idx < fb->width * fb->height) {
                uint16_t p = pixels[px_idx];
                uint8_t r = (p >> 11) & 0x1F;
                uint8_t g = (p >> 5) & 0x3F;
                uint8_t b = p & 0x1F;
                int luma = (r * 8 * 299 + g * 4 * 587 + b * 8 * 114) / 1000;
                mean += luma;
            }
        }
    }
    return mean / (float)n;
}

float face_detector_compute_yaw(detected_face_t *face) {
    /* Estimate yaw from eye positions */
    float dx = face->landmarks[2] - face->landmarks[0];
    float dy = face->landmarks[3] - face->landmarks[1];
    return atan2f(dy, dx) * 180.0f / 3.14159f;
}
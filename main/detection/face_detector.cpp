/**
 * @file face_detector.cpp
 * @brief Face detection using ESP-DL face detection model
 */

#include "face_detector.h"
#include "config.h"
#include "esp_log.h"
#include "human_face_detect.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>
#include <list>
#include <math.h>
#include <string.h>

static const char* TAG = "FACE_DET";
static HumanFaceDetect* s_detector = nullptr;
static SemaphoreHandle_t s_detector_mux = nullptr;

esp_err_t face_detector_init(void) {
    ESP_LOGI(TAG, "Initializing face detector");
    
    if (!s_detector_mux) {
        s_detector_mux = xSemaphoreCreateMutex();
    }

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
    
    if (s_detector_mux && xSemaphoreTake(s_detector_mux, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Face detector mutex timeout");
        return ESP_ERR_TIMEOUT;
    }

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
            /* ESP-DL HumanFaceDetect outputs keypoints natively in canonical order:
             * 0,1: Left Eye | 2,3: Left Mouth | 4,5: Nose | 6,7: Right Eye | 8,9: Right Mouth */
            for (int k = 0; k < 10; k++) {
                result->faces[result->face_count].landmarks[k] = (float)d.keypoint[k];
            }
        } else {
            /* Fallback: approximate landmarks from bounding box in expected order */
            int w = d.box[2] - d.box[0];
            int h = d.box[3] - d.box[1];
            int cx = d.box[0] + w/2;
            int eye_y = d.box[1] + (int)(h * 0.3f);
            int nose_y = d.box[1] + (int)(h * 0.6f);
            int mouth_y = d.box[1] + (int)(h * 0.8f);
            
            result->faces[result->face_count].landmarks[0] = cx - (int)(w * 0.2f);  // Left Eye X
            result->faces[result->face_count].landmarks[1] = eye_y;                // Left Eye Y
            result->faces[result->face_count].landmarks[2] = cx - (int)(w * 0.15f); // Left Mouth Corner X
            result->faces[result->face_count].landmarks[3] = mouth_y;               // Left Mouth Corner Y
            result->faces[result->face_count].landmarks[4] = cx;                    // Nose X
            result->faces[result->face_count].landmarks[5] = nose_y;               // Nose Y
            result->faces[result->face_count].landmarks[6] = cx + (int)(w * 0.2f);  // Right Eye X
            result->faces[result->face_count].landmarks[7] = eye_y;                // Right Eye Y
            result->faces[result->face_count].landmarks[8] = cx + (int)(w * 0.15f); // Right Mouth Corner X
            result->faces[result->face_count].landmarks[9] = mouth_y;               // Right Mouth Corner Y
        }
        
        result->face_count++;
    }
    
    if (s_detector_mux) xSemaphoreGive(s_detector_mux);
    return ESP_OK;
}

/* Quality functions */
float face_detector_compute_sharpness(camera_fb_t *fb, detected_face_t *face) {
    if (!fb || !fb->buf || !face) return 0.0f;
    float mean = 0, variance = 0;
    int valid_pixel_count = 0;
    uint16_t *pixels = (uint16_t *)fb->buf;

    for (int y = 0; y < face->h; y++) {
        int py = face->y + y;
        if (py < 0 || py >= fb->height) continue;
        for (int x = 0; x < face->w; x++) {
            int px = face->x + x;
            if (px < 0 || px >= fb->width) continue;
            int px_idx = py * fb->width + px;
            uint8_t g = (pixels[px_idx] >> 5) & 0x3F;
            mean += g;
            valid_pixel_count++;
        }
    }
    if (valid_pixel_count <= 0) return 0.0f;
    mean /= (float)valid_pixel_count;

    for (int y = 0; y < face->h; y++) {
        int py = face->y + y;
        if (py < 0 || py >= fb->height) continue;
        for (int x = 0; x < face->w; x++) {
            int px = face->x + x;
            if (px < 0 || px >= fb->width) continue;
            int px_idx = py * fb->width + px;
            uint8_t g = (pixels[px_idx] >> 5) & 0x3F;
            variance += (g - mean) * (g - mean);
        }
    }
    return variance / (float)valid_pixel_count;
}

float face_detector_compute_brightness(camera_fb_t *fb, detected_face_t *face) {
    if (!fb || !fb->buf || !face) return 0.0f;
    float mean = 0;
    int valid_pixel_count = 0;
    uint16_t *pixels = (uint16_t *)fb->buf;

    for (int y = 0; y < face->h; y++) {
        int py = face->y + y;
        if (py < 0 || py >= fb->height) continue;
        for (int x = 0; x < face->w; x++) {
            int px = face->x + x;
            if (px < 0 || px >= fb->width) continue;
            int px_idx = py * fb->width + px;
            uint16_t p = pixels[px_idx];
            uint8_t r = (p >> 11) & 0x1F;
            uint8_t g = (p >> 5) & 0x3F;
            uint8_t b = p & 0x1F;
            int luma = (r * 8 * 299 + g * 4 * 587 + b * 8 * 114) / 1000;
            mean += luma;
            valid_pixel_count++;
        }
    }
    if (valid_pixel_count <= 0) return 0.0f;
    return mean / (float)valid_pixel_count;
}

float face_detector_compute_yaw(detected_face_t *face) {
    /* Estimate yaw from eye and nose positions in ESP-DL order */
    float left_eye_x = face->landmarks[0];
    float right_eye_x = face->landmarks[6];
    float nose_x = face->landmarks[4];
    
    float mid_x = (left_eye_x + right_eye_x) / 2.0f;
    float eye_dist = fabsf(right_eye_x - left_eye_x);
    if (eye_dist < 1.0f) eye_dist = 1.0f;
    
    float deviation = (nose_x - mid_x) / eye_dist;
    return deviation * 90.0f;
}

float face_detector_compute_glare_ratio(camera_fb_t *fb, detected_face_t *face) {
    if (!fb || !fb->buf || !face) return 0.0f;
    int glare_count = 0;
    int valid_pixel_count = 0;
    uint16_t *pixels = (uint16_t *)fb->buf;

    /* Subsample every 2nd pixel horizontally and vertically for minimal CPU overhead */
    for (int y = 0; y < face->h; y += 2) {
        int py = face->y + y;
        if (py < 0 || py >= fb->height) continue;
        int row_offset = py * fb->width;
        for (int x = 0; x < face->w; x += 2) {
            int px = face->x + x;
            if (px < 0 || px >= fb->width) continue;
            uint16_t p = pixels[row_offset + px];
            uint8_t r = (p >> 11) & 0x1F;
            uint8_t g = (p >> 5) & 0x3F;
            uint8_t b = p & 0x1F;
            int luma = (r * 8 * 299 + g * 4 * 587 + b * 8 * 114) / 1000;
            if (luma > 240) glare_count++;
            valid_pixel_count++;
        }
    }
    if (valid_pixel_count <= 0) return 0.0f;
    return (float)glare_count / (float)valid_pixel_count;
}

float face_detector_compute_mouth_ratio(detected_face_t *face) {
    if (!face) return 0.0f;
    /* Landmarks in ESP-DL order:
     * 0,1: Left Eye; 2,3: Left Mouth; 4,5: Nose; 6,7: Right Eye; 8,9: Right Mouth */
    float left_eye_x   = face->landmarks[0];
    float left_eye_y   = face->landmarks[1];
    float left_mouth_y = face->landmarks[3];
    float nose_y       = face->landmarks[5];
    float right_eye_x  = face->landmarks[6];
    float right_eye_y  = face->landmarks[7];
    float right_mouth_y= face->landmarks[9];

    float dx = right_eye_x - left_eye_x;
    float dy = right_eye_y - left_eye_y;
    float eye_dist = sqrtf(dx * dx + dy * dy);
    if (eye_dist < 10.0f) return 0.60f; /* Fallback for tiny/degenerate boxes */

    float mouth_mid_y = (left_mouth_y + right_mouth_y) * 0.5f;
    float nose_to_mouth = mouth_mid_y - nose_y;
    return nose_to_mouth / eye_dist;
}
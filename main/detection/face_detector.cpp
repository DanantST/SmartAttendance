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
            /* Map from model order [Left Eye (0,1), Right Eye (2,3), Nose (4,5), Left Mouth (6,7), Right Mouth (8,9)]
             * to expected pipeline order [Left Eye, Left Mouth, Nose, Right Eye, Right Mouth] */
            result->faces[result->face_count].landmarks[0] = d.keypoint[0]; // Left Eye X
            result->faces[result->face_count].landmarks[1] = d.keypoint[1]; // Left Eye Y
            result->faces[result->face_count].landmarks[2] = d.keypoint[6]; // Left Mouth X
            result->faces[result->face_count].landmarks[3] = d.keypoint[7]; // Left Mouth Y
            result->faces[result->face_count].landmarks[4] = d.keypoint[4]; // Nose X
            result->faces[result->face_count].landmarks[5] = d.keypoint[5]; // Nose Y
            result->faces[result->face_count].landmarks[6] = d.keypoint[2]; // Right Eye X
            result->faces[result->face_count].landmarks[7] = d.keypoint[3]; // Right Eye Y
            result->faces[result->face_count].landmarks[8] = d.keypoint[8]; // Right Mouth X
            result->faces[result->face_count].landmarks[9] = d.keypoint[9]; // Right Mouth Y
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
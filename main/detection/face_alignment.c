/**
 * @file face_alignment.c
 * @brief Face alignment using affine transformation
 */

#include "face_alignment.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "config.h"
#include <math.h>
#include <string.h>

static const char *TAG = "ALIGN";

/* Canonical landmarks (normalized coordinates) for 112x112 output.
 * Set to match s_std_ldks_112 values from ESP-DL preprocessor exactly,
 * but ordered as [Left Eye, Right Eye, Nose, Left Mouth, Right Mouth]
 * to match face_alignment.c / face_detector.cpp ordering. */
static const float CANONICAL_LANDMARKS[10] = {
    38.2946f, 51.6963f,   // left eye
    41.5493f, 92.3655f,   // left mouth
    56.0252f, 71.7366f,   // nose
    73.5318f, 51.5014f,   // right eye
    70.7299f, 92.2041f    // right mouth
};

/* Compute affine transformation from source 2D points to destination 2D points.
 * Solves the 6x6 system arising from 3 point-pair correspondences using
 * Gaussian elimination with partial pivoting.
 * M = [a, b, c;  d, e, f]  such that  dst[i] = M * [src[i]; 1].
 * Returns true if the system was non-degenerate, false on failure (identity set). */
static bool compute_affine(float *src, float *dst, float *M) {
    /* Build 6x7 augmented matrix [A | B] */
    float aug[6][7] = {{0}};
    for (int i = 0; i < 3; i++) {
        float xs = src[i * 2];
        float ys = src[i * 2 + 1];
        float xd = dst[i * 2];
        float yd = dst[i * 2 + 1];
        /* Row for x: [xs, ys, 1, 0, 0, 0 | xd] */
        aug[i * 2][0] = xs; aug[i * 2][1] = ys; aug[i * 2][2] = 1.0f;
        aug[i * 2][6] = xd;
        /* Row for y: [0, 0, 0, xs, ys, 1 | yd] */
        aug[i * 2 + 1][3] = xs; aug[i * 2 + 1][4] = ys; aug[i * 2 + 1][5] = 1.0f;
        aug[i * 2 + 1][6] = yd;
    }

    /* Forward elimination with partial pivoting */
    for (int col = 0; col < 6; col++) {
        /* Find pivot row */
        int pivot = col;
        float max_val = fabsf(aug[col][col]);
        for (int row = col + 1; row < 6; row++) {
            if (fabsf(aug[row][col]) > max_val) {
                max_val = fabsf(aug[row][col]);
                pivot = row;
            }
        }
        if (max_val < 1e-6f) {
            /* Degenerate — fall back to identity */
            M[0] = 1.0f; M[1] = 0.0f; M[2] = 0.0f;
            M[3] = 0.0f; M[4] = 1.0f; M[5] = 0.0f;
            return false;
        }
        /* Swap rows */
        if (pivot != col) {
            for (int k = 0; k < 7; k++) {
                float tmp = aug[col][k]; aug[col][k] = aug[pivot][k]; aug[pivot][k] = tmp;
            }
        }
        /* Eliminate column below pivot */
        float inv = 1.0f / aug[col][col];
        for (int row = col + 1; row < 6; row++) {
            float factor = aug[row][col] * inv;
            for (int k = col; k < 7; k++) {
                aug[row][k] -= factor * aug[col][k];
            }
        }
    }

    /* Back-substitution */
    float x[6];
    for (int i = 5; i >= 0; i--) {
        x[i] = aug[i][6];
        for (int j = i + 1; j < 6; j++) {
            x[i] -= aug[i][j] * x[j];
        }
        x[i] /= aug[i][i];
    }

    /* M = [x[0] x[1] x[2]; x[3] x[4] x[5]] */
    M[0] = x[0]; M[1] = x[1]; M[2] = x[2];
    M[3] = x[3]; M[4] = x[4]; M[5] = x[5];
    return true;
}

esp_err_t face_alignment_align(camera_fb_t *fb, detected_face_t *face, aligned_face_t *aligned) {
    if (!fb || !face || !aligned) return ESP_ERR_INVALID_ARG;

    /* Extract source landmarks from face (Left Eye [0], Right Eye [3], Nose [2]) */
    float src[6];
    src[0] = face->landmarks[0]; // Left Eye X
    src[1] = face->landmarks[1]; // Left Eye Y
    src[2] = face->landmarks[6]; // Right Eye X
    src[3] = face->landmarks[7]; // Right Eye Y
    src[4] = face->landmarks[4]; // Nose X
    src[5] = face->landmarks[5]; // Nose Y

    float dst[6];
    dst[0] = CANONICAL_LANDMARKS[0]; // Left Eye X
    dst[1] = CANONICAL_LANDMARKS[1]; // Left Eye Y
    dst[2] = CANONICAL_LANDMARKS[6]; // Right Eye X
    dst[3] = CANONICAL_LANDMARKS[7]; // Right Eye Y
    dst[4] = CANONICAL_LANDMARKS[4]; // Nose X
    dst[5] = CANONICAL_LANDMARKS[5]; // Nose Y

    float M[6];
    bool affine_ok = compute_affine(dst, src, M);

    /* If landmarks are degenerate (e.g. fallback box landmarks), override M with
     * a simple scale+translate that maps the bounding box centre to the canonical
     * 112x112 crop — still far better than an identity warp from pixel(0,0). */
    if (!affine_ok) {
        float face_cx = face->x + face->w * 0.5f;
        float face_cy = face->y + face->h * 0.5f;
        float scale_x = (float)FACE_ALIGN_SIZE / (float)(face->w > 0 ? face->w : 1);
        float scale_y = (float)FACE_ALIGN_SIZE / (float)(face->h > 0 ? face->h : 1);
        float scale   = (scale_x < scale_y) ? scale_x : scale_y;
        if (scale < 0.01f) scale = 0.01f;
        float inv_scale = 1.0f / scale;
        /* M maps destination to source: src_pt = inv_scale * (dst_pt - 56) + face_centre */
        M[0] = inv_scale;  M[1] = 0.0f;       M[2] = -inv_scale * FACE_ALIGN_SIZE * 0.5f + face_cx;
        M[3] = 0.0f;       M[4] = inv_scale;  M[5] = -inv_scale * FACE_ALIGN_SIZE * 0.5f + face_cy;
        ESP_LOGD(TAG, "Affine fallback: bbox crop at (%.0f,%.0f) scale=%.2f", face_cx, face_cy, scale);
    }

    /* Allocate output buffer — 2 bytes/pixel for RGB565 to match feature_extractor.cpp
     * which configures DL_IMAGE_PIX_TYPE_RGB565LE. Allocating only width*height (1 byte/pixel)
     * would cause the model to read past the buffer. [Fix C1] */
    aligned->width = FACE_ALIGN_SIZE;
    aligned->height = FACE_ALIGN_SIZE;
    aligned->data = (uint8_t *)heap_caps_malloc(
        aligned->width * aligned->height * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!aligned->data) {
        ESP_LOGE(TAG, "Failed to allocate aligned face buffer");
        return ESP_ERR_NO_MEM;
    }

    /* Perform affine warp using bilinear interpolation on RGB565 source pixels */
    uint16_t *src_img = (uint16_t *)fb->buf;   /* camera outputs RGB565 (2 bytes/pixel) */
    uint16_t *dst_img = (uint16_t *)aligned->data;
    int src_w = fb->width;
    int src_h = fb->height;

    for (int y = 0; y < aligned->height; y++) {
        for (int x = 0; x < aligned->width; x++) {
            /* Transform destination point back to source coordinate */
            float src_x = M[0] * x + M[1] * y + M[2];
            float src_y = M[3] * x + M[4] * y + M[5];

            int x0 = (int)src_x;
            int y0 = (int)src_y;
            if (x0 >= 0 && x0 < src_w - 1 && y0 >= 0 && y0 < src_h - 1) {
                /* Bilinear interpolation on each RGB565 channel independently */
                float fx = src_x - x0;
                float fy = src_y - y0;
                uint16_t p00 = src_img[y0 * src_w + x0];
                uint16_t p01 = src_img[y0 * src_w + x0 + 1];
                uint16_t p10 = src_img[(y0 + 1) * src_w + x0];
                uint16_t p11 = src_img[(y0 + 1) * src_w + x0 + 1];

                /* Interpolate each 5-6-5 channel */
                uint8_t r = (uint8_t)((1-fx)*(1-fy)*((p00>>11)&0x1F) + fx*(1-fy)*((p01>>11)&0x1F)
                                    + (1-fx)*fy*((p10>>11)&0x1F)      + fx*fy*((p11>>11)&0x1F));
                uint8_t g = (uint8_t)((1-fx)*(1-fy)*((p00>>5)&0x3F)  + fx*(1-fy)*((p01>>5)&0x3F)
                                    + (1-fx)*fy*((p10>>5)&0x3F)       + fx*fy*((p11>>5)&0x3F));
                uint8_t b = (uint8_t)((1-fx)*(1-fy)*(p00&0x1F)        + fx*(1-fy)*(p01&0x1F)
                                    + (1-fx)*fy*(p10&0x1F)            + fx*fy*(p11&0x1F));
                dst_img[y * aligned->width + x] = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            } else {
                dst_img[y * aligned->width + x] = 0;
            }
        }
    }

    return ESP_OK;
}

void face_alignment_free(aligned_face_t *aligned) {
    if (aligned && aligned->data) {
        heap_caps_free(aligned->data);
        aligned->data = NULL;
    }
}

#define CLAHE_TILES_X 4
#define CLAHE_TILES_Y 4
#define CLAHE_TILE_W  (FACE_ALIGN_SIZE / CLAHE_TILES_X)  /* 112 / 4 = 28 */
#define CLAHE_TILE_H  (FACE_ALIGN_SIZE / CLAHE_TILES_Y)  /* 112 / 4 = 28 */
#define CLAHE_TILE_N  (CLAHE_TILE_W * CLAHE_TILE_H)     /* 784 pixels */

void face_alignment_normalize_brightness(aligned_face_t *aligned) {
    if (!aligned || !aligned->data || aligned->width != FACE_ALIGN_SIZE || aligned->height != FACE_ALIGN_SIZE) return;

    uint16_t *buf = (uint16_t *)aligned->data;
    int num_pixels = aligned->width * aligned->height;

    /* Allocate temp buffer for 8-bit luminance */
    uint8_t *luma = (uint8_t *)heap_caps_malloc(num_pixels, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!luma) {
        luma = (uint8_t *)malloc(num_pixels);
    }
    if (!luma) return;

    /* 1. Calculate 8-bit Luminance (0..255) for each pixel: Y = 0.299R + 0.587G + 0.114B */
    for (int i = 0; i < num_pixels; i++) {
        uint16_t p = buf[i];
        uint32_t r8 = ((p >> 11) & 0x1F) * 255 / 31;
        uint32_t g8 = ((p >> 5) & 0x3F) * 255 / 63;
        uint32_t b8 = (p & 0x1F) * 255 / 31;
        luma[i] = (uint8_t)((r8 * 299 + g8 * 587 + b8 * 114) / 1000);
    }

    /* 2. Compute 4x4 Grid Tile CLAHE CDF mappings */
    uint8_t (*tile_maps)[CLAHE_TILES_X][256] = (uint8_t (*)[CLAHE_TILES_X][256])heap_caps_malloc(
        sizeof(uint8_t) * CLAHE_TILES_Y * CLAHE_TILES_X * 256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tile_maps) {
        tile_maps = (uint8_t (*)[CLAHE_TILES_X][256])malloc(sizeof(uint8_t) * CLAHE_TILES_Y * CLAHE_TILES_X * 256);
    }
    if (!tile_maps) {
        free(luma);
        return;
    }

    int clip_limit = (int)(2.5f * ((float)CLAHE_TILE_N / 256.0f));
    if (clip_limit < 4) clip_limit = 4;

    for (int ty = 0; ty < CLAHE_TILES_Y; ty++) {
        for (int tx = 0; tx < CLAHE_TILES_X; tx++) {
            int hist[256] = {0};
            int y_start = ty * CLAHE_TILE_H;
            int x_start = tx * CLAHE_TILE_W;

            for (int y = y_start; y < y_start + CLAHE_TILE_H; y++) {
                int row_offset = y * aligned->width;
                for (int x = x_start; x < x_start + CLAHE_TILE_W; x++) {
                    hist[luma[row_offset + x]]++;
                }
            }

            int excess = 0;
            for (int i = 0; i < 256; i++) {
                if (hist[i] > clip_limit) {
                    excess += (hist[i] - clip_limit);
                    hist[i] = clip_limit;
                }
            }

            int bonus = excess / 256;
            for (int i = 0; i < 256; i++) {
                hist[i] += bonus;
            }

            uint32_t cdf = 0;
            for (int i = 0; i < 256; i++) {
                cdf += hist[i];
                uint32_t val = (cdf * 255) / CLAHE_TILE_N;
                if (val > 255) val = 255;
                tile_maps[ty][tx][i] = (uint8_t)val;
            }
        }
    }

    /* 3. Bilinear Interpolation across tile centers for each pixel */
    for (int y = 0; y < aligned->height; y++) {
        float ty_f = ((float)y - (float)CLAHE_TILE_H * 0.5f) / (float)CLAHE_TILE_H;
        int y1 = (int)floorf(ty_f);
        int y2 = y1 + 1;
        float wy = ty_f - (float)y1;

        if (y1 < 0) { y1 = 0; y2 = 0; wy = 0.0f; }
        if (y2 >= CLAHE_TILES_Y) { y1 = CLAHE_TILES_Y - 1; y2 = CLAHE_TILES_Y - 1; wy = 0.0f; }

        int row_offset = y * aligned->width;
        for (int x = 0; x < aligned->width; x++) {
            float tx_f = ((float)x - (float)CLAHE_TILE_W * 0.5f) / (float)CLAHE_TILE_W;
            int x1 = (int)floorf(tx_f);
            int x2 = x1 + 1;
            float wx = tx_f - (float)x1;

            if (x1 < 0) { x1 = 0; x2 = 0; wx = 0.0f; }
            if (x2 >= CLAHE_TILES_X) { x1 = CLAHE_TILES_X - 1; x2 = CLAHE_TILES_X - 1; wx = 0.0f; }

            uint8_t y_old = luma[row_offset + x];
            if (y_old == 0) continue;

            uint8_t v11 = tile_maps[y1][x1][y_old];
            uint8_t v12 = tile_maps[y1][x2][y_old];
            uint8_t v21 = tile_maps[y2][x1][y_old];
            uint8_t v22 = tile_maps[y2][x2][y_old];

            float y_top = (1.0f - wx) * (float)v11 + wx * (float)v12;
            float y_bot = (1.0f - wx) * (float)v21 + wx * (float)v22;
            float y_new = (1.0f - wy) * y_top + wy * y_bot;

            float scale = y_new / (float)y_old;
            if (scale < 0.4f) scale = 0.4f;
            if (scale > 1.8f) scale = 1.8f;

            uint16_t p = buf[row_offset + x];
            uint32_t r5 = (p >> 11) & 0x1F;
            uint32_t g6 = (p >> 5) & 0x3F;
            uint32_t b5 = p & 0x1F;

            uint32_t new_r5 = (uint32_t)(r5 * scale + 0.5f);
            uint32_t new_g6 = (uint32_t)(g6 * scale + 0.5f);
            uint32_t new_b5 = (uint32_t)(b5 * scale + 0.5f);

            if (new_r5 > 31) new_r5 = 31;
            if (new_g6 > 63) new_g6 = 63;
            if (new_b5 > 31) new_b5 = 31;

            buf[row_offset + x] = (uint16_t)((new_r5 << 11) | (new_g6 << 5) | new_b5);
        }
    }

    free(tile_maps);
    free(luma);
}
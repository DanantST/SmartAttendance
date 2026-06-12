/**
 * @file recognizer.cpp
 * @brief Face recognition engine
 */

#include "recognizer.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#include "config.h"
#include "esp_heap_caps.h"

static const char *TAG = "RECOG";

/* Cache of users */
static user_t *s_user_cache = NULL;
static int s_cache_size = 0;
static int s_cache_capacity = 0;

/* Cosine similarity between two int8 embeddings */
static float cosine_similarity(face_embedding_t *a, face_embedding_t *b) {
    long long dot = 0;
    long long norm_a = 0;
    long long norm_b = 0;
    for (int i = 0; i < EMBEDDING_DIM; i++) {
        dot += (int)a->values[i] * (int)b->values[i];
        norm_a += (int)a->values[i] * (int)a->values[i];
        norm_b += (int)b->values[i] * (int)b->values[i];
    }
    if (norm_a == 0 || norm_b == 0) return 0.0f;
    return (float)dot / (sqrtf(norm_a) * sqrtf(norm_b));
}

esp_err_t recognizer_init(void) {
    /* Initialize feature extractor (model) */
    esp_err_t ret = feature_extractor_init();
    if (ret != ESP_OK) return ret;

    /* Allocate cache capacity */
    s_cache_capacity = EMBEDDING_CACHE_SIZE;
    s_user_cache = (user_t*)heap_caps_malloc(s_cache_capacity * sizeof(user_t), MALLOC_CAP_SPIRAM);
    if (!s_user_cache) {
        ESP_LOGE(TAG, "Failed to allocate user cache");
        return ESP_ERR_NO_MEM;
    }
    s_cache_size = 0;
    return ESP_OK;
}

void recognizer_load_cache(void) {
    ESP_LOGI(TAG, "Loading users into cache from database...");
    user_t *users = NULL;
    int count = 0;
    if (db_get_all_users(&users, &count) == ESP_OK && users && count > 0) {
        int to_load = count < s_cache_capacity ? count : s_cache_capacity;
        memcpy(s_user_cache, users, to_load * sizeof(user_t));
        s_cache_size = to_load;
        free(users);
        ESP_LOGI(TAG, "Loaded %d users into recognition cache", s_cache_size);
    } else {
        s_cache_size = 0;
        ESP_LOGI(TAG, "No users found or failed to load");
    }
}

esp_err_t recognizer_identify(face_embedding_t *query, user_t **user, float *confidence) {
    if (!query || !user || !confidence) return ESP_ERR_INVALID_ARG;

    *user = NULL;
    *confidence = 0.0f;

    if (s_cache_size == 0) {
        return ESP_OK;
    }

    float best_sim = -1.0f;
    int best_idx = -1;

    for (int i = 0; i < s_cache_size; i++) {
        float sim = cosine_similarity(query, &s_user_cache[i].embedding);
        if (sim > best_sim) {
            best_sim = sim;
            best_idx = i;
        }
    }

    if (best_sim >= RECOGNITION_THRESHOLD) {
        *user = &s_user_cache[best_idx];
        *confidence = best_sim;
    }

    return ESP_OK;
}

void recognizer_add_to_cache(user_t *user) {
    if (s_cache_size < s_cache_capacity) {
        memcpy(&s_user_cache[s_cache_size], user, sizeof(user_t));
        s_cache_size++;
        ESP_LOGD(TAG, "Added user %s to cache", user->name);
    } else {
        ESP_LOGW(TAG, "Cache full, cannot add user %s", user->name);
    }
}

int recognizer_get_cache_size(void) {
    return s_cache_size;
}
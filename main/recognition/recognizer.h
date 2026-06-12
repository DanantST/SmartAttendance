/**
 * @file recognizer.h
 * @brief Face recognition engine with database cache
 */

#ifndef RECOGNIZER_H
#define RECOGNIZER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "feature_extractor.h"
#include "database/db_manager.h"

/**
 * @brief Initialize recognizer (load cache from DB)
 * @return ESP_OK on success
 */
esp_err_t recognizer_init(void);

/**
 * @brief Load user embeddings from database into cache
 */
void recognizer_load_cache(void);

/**
 * @brief Identify a face from embedding
 * @param query embedding to identify
 * @param user output matched user (NULL if not found)
 * @param confidence output confidence score
 * @return ESP_OK on success
 */
esp_err_t recognizer_identify(face_embedding_t *query, user_t **user, float *confidence);

/**
 * @brief Add a new user to cache
 * @param user user record with embedding
 */
void recognizer_add_to_cache(user_t *user);

/**
 * @brief Get number of users in cache
 * @return count
 */
int recognizer_get_cache_size(void);


#ifdef __cplusplus
}
#endif

#endif /* RECOGNIZER_H */
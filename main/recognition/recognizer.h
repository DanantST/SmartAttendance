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

/**
 * @brief Multi-Frame Temporal Pattern Fusion & Consensus Voting for attendance scanning.
 *
 * Implements Plan v4 Final pipeline:
 *  1. Pairwise similarity matrix over all N frames.
 *  2. Adaptive outlier filtering (mu - 1.5*sigma), dynamic inlier floor:
 *       MinInliers(N) = max(2, ceil(0.6 * N))
 *  3. Centroid synthesis on INLIER frames only, with L2 norm restoration -> q_fused.
 *  4a. hint_candidate != NULL (early-exit path): skip full DB scan.
 *  4b. hint_candidate == NULL (full-window path): single O(N_users) DB scan on q_fused.
 *  5. Vote INLIER frames against Top-3 candidates at tau_vote = SCAN_VOTE_THRESHOLD.
 *  6. Winner k* requires >= ceil(0.6 * N_inlier) consensus votes.
 *     IMPORTANT: voting pool = inlier frames only; denominator = N_inlier (not raw N).
 *  7. Acceptance gate: S_fused(k*) >= RECOGNITION_THRESHOLD.
 *
 * @param frames          Array of N face_embedding_t (task-local, no shared access).
 * @param N               Number of frames in window (SCAN_EARLY_EXIT_FRAMES or SCAN_WINDOW_SIZE).
 * @param hint_candidate  Pre-identified candidate from 3-frame unanimity check, or NULL.
 * @param out_user        Output: matched user_t pointer, or NULL if no match.
 * @param out_fused_confidence Output: S_fused score of winning candidate.
 * @param out_consensus_votes  Output: inlier vote count for winning candidate.
 * @return ESP_OK on success.
 */
esp_err_t recognizer_identify_multiframe(
    face_embedding_t *frames,
    int N,
    user_t *hint_candidate,
    user_t **out_user,
    float *out_fused_confidence,
    int   *out_consensus_votes
);


#ifdef __cplusplus
}
#endif

#endif /* RECOGNIZER_H */
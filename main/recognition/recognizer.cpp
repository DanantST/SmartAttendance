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
        for (int i = 0; i < s_cache_size; i++) {
            ESP_LOGI(TAG, "  User %s: id=%u, role=%s, embedding sample=%d %d %d %d %d",
                     s_user_cache[i].name, (unsigned int)s_user_cache[i].id, s_user_cache[i].role,
                     s_user_cache[i].embedding.values[0],
                     s_user_cache[i].embedding.values[1],
                     s_user_cache[i].embedding.values[2],
                     s_user_cache[i].embedding.values[3],
                     s_user_cache[i].embedding.values[4]);
        }
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
        ESP_LOGW(TAG, "recognizer_identify: Cache is empty!");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Query embedding sample: %d %d %d %d %d",
             query->values[0], query->values[1], query->values[2], query->values[3], query->values[4]);

    float best_sim = -1.0f;
    int best_idx = -1;
    float second_sim = -1.0f;
    int second_idx = -1;

    for (int i = 0; i < s_cache_size; i++) {
        float sim = cosine_similarity(query, &s_user_cache[i].embedding);
        if (sim > best_sim) {
            second_sim = best_sim;
            second_idx = best_idx;
            best_sim = sim;
            best_idx = i;
        } else if (sim > second_sim) {
            second_sim = sim;
            second_idx = i;
        }
    }

    float margin = (second_idx >= 0) ? (best_sim - second_sim) : 1.0f;

    ESP_LOGI(TAG, "Best match: idx=%d (user=%s), sim=%.3f, runner_up=%s (sim=%.3f, margin=%.3f, threshold=%.3f)",
             best_idx, (best_idx >= 0) ? s_user_cache[best_idx].name : "None", best_sim,
             (second_idx >= 0) ? s_user_cache[second_idx].name : "None", second_sim,
             margin, RECOGNITION_THRESHOLD);

    bool pass = false;
    if (best_sim >= RECOGNITION_HIGH_CONF_THRESHOLD) {
        pass = (margin >= RECOGNITION_MARGIN_HIGH);
    } else if (best_sim >= RECOGNITION_THRESHOLD) {
        pass = (margin >= RECOGNITION_MARGIN_MIN);
    }

    if (pass && best_idx >= 0) {
        *user = &s_user_cache[best_idx];
        *confidence = best_sim;
    } else if (best_idx >= 0 && best_sim >= RECOGNITION_THRESHOLD) {
        ESP_LOGW(TAG, "recognizer_identify: Ambiguous match rejected! Top1: %s (%.3f) vs Top2: %s (%.3f), margin=%.3f < required",
                 s_user_cache[best_idx].name, best_sim,
                 (second_idx >= 0) ? s_user_cache[second_idx].name : "None", second_sim,
                 margin);
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

/* ---------------------------------------------------------------------------
 * recognizer_identify_multiframe()
 *
 * Multi-Frame Temporal Pattern Fusion & Consensus Voting — Plan v4 Final
 *
 * Pipeline:
 *  1. Pairwise cosine similarity matrix S_ij over all N frames.
 *  2. Adaptive outlier filtering (mu - 1.5*sigma) with N-scaled inlier floor:
 *       MinInliers(N) = max(2, ceil(0.6 * N))
 *     Voting pool = INLIER frames only. Consensus denominator = N_inlier.
 *  3. Centroid synthesis on inliers with L2 norm restoration -> q_fused.
 *  4a. If hint_candidate != NULL (early-exit path): skip full DB scan;
 *      use hint_candidate as Top-1. Top-2/Top-3 filled from cache head.
 *  4b. If hint_candidate == NULL (full-window path): one full O(N_users) DB
 *      scan on q_fused to extract Top-3 candidates.
 *  5. Vote INLIER frames against Top-3 at tau_vote=SCAN_VOTE_THRESHOLD.
 *  6. Winner k* must reach >= ceil(0.6 * N_inlier) votes.
 *  7. Acceptance gate: S_fused(k*) >= RECOGNITION_THRESHOLD.
 *
 * IMPORTANT — voting pool and consensus denominator are BOTH inlier-only.
 * Outlier frames are excluded from voting so a noisy/blurry frame cannot
 * drag down the consensus of an otherwise clear genuine match.
 * ---------------------------------------------------------------------------*/
esp_err_t recognizer_identify_multiframe(
    face_embedding_t *frames,   /* Array of N embeddings                     */
    int N,                      /* Number of frames (3 or SCAN_WINDOW_SIZE)  */
    user_t *hint_candidate,     /* Pre-known candidate from early-exit check (may be NULL) */
    user_t **out_user,          /* Output: matched user or NULL              */
    float *out_fused_confidence,/* Output: S_fused score for k*              */
    int   *out_consensus_votes  /* Output: number of inlier votes for k*     */
) {
    if (!frames || N < 2 || !out_user || !out_fused_confidence || !out_consensus_votes) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_user             = NULL;
    *out_fused_confidence = 0.0f;
    *out_consensus_votes  = 0;

    if (s_cache_size == 0) {
        ESP_LOGW(TAG, "multiframe: cache empty");
        return ESP_OK;
    }

    /* ------------------------------------------------------------------ */
    /* Step 1: Pairwise cosine similarity matrix & per-frame mean sim       */
    /* ------------------------------------------------------------------ */
    float mean_sims[SCAN_WINDOW_SIZE] = {0.0f};
    float sum_all = 0.0f;

    for (int i = 0; i < N; i++) {
        float sum_sim = 0.0f;
        for (int j = 0; j < N; j++) {
            if (i == j) continue;
            sum_sim += cosine_similarity(&frames[i], &frames[j]);
        }
        mean_sims[i] = (N > 1) ? (sum_sim / (float)(N - 1)) : 1.0f;
        sum_all += mean_sims[i];
    }

    float grand_mean = sum_all / (float)N;
    float var_sum = 0.0f;
    for (int i = 0; i < N; i++) {
        float d = mean_sims[i] - grand_mean;
        var_sum += d * d;
    }
    float std_dev = sqrtf(var_sum / (float)N);
    float filter_thresh = grand_mean - 1.5f * std_dev;

    /* ------------------------------------------------------------------ */
    /* Step 2: Outlier filtering with N-scaled inlier floor guard           */
    /* MinInliers(N) = max(2, ceil(0.6 * N))                               */
    /* Voting pool = inlier frames only (consistent with centroid inputs)   */
    /* ------------------------------------------------------------------ */
    int min_inliers = (int)ceilf(0.6f * (float)N);
    if (min_inliers < 2) min_inliers = 2;

    int inlier_idx[SCAN_WINDOW_SIZE];
    int N_inlier = 0;

    for (int i = 0; i < N; i++) {
        if (mean_sims[i] >= filter_thresh) {
            inlier_idx[N_inlier++] = i;
        }
    }

    /* Fallback: if too few inliers, use all frames (avoids degenerate centroid) */
    if (N_inlier < min_inliers) {
        ESP_LOGW(TAG, "multiframe: only %d inliers (min=%d), using all %d frames unfiltered",
                 N_inlier, min_inliers, N);
        N_inlier = 0;
        for (int i = 0; i < N; i++) inlier_idx[N_inlier++] = i;
    }

    ESP_LOGI(TAG, "multiframe: N=%d grand_mean=%.3f std=%.3f thresh=%.3f inliers=%d",
             N, grand_mean, std_dev, filter_thresh, N_inlier);

    /* ------------------------------------------------------------------ */
    /* Step 3: Centroid synthesis on inliers + L2 norm restoration         */
    /* ------------------------------------------------------------------ */
    float centroid[EMBEDDING_DIM] = {0.0f};
    for (int k = 0; k < N_inlier; k++) {
        int idx = inlier_idx[k];
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            centroid[m] += (float)frames[idx].values[m];
        }
    }
    for (int m = 0; m < EMBEDDING_DIM; m++) {
        centroid[m] /= (float)N_inlier;
    }

    /* Compute mean L2 norm of inliers to restore after averaging */
    float sum_norms = 0.0f;
    for (int k = 0; k < N_inlier; k++) {
        int idx = inlier_idx[k];
        float norm_sq = 0.0f;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            float v = (float)frames[idx].values[m];
            norm_sq += v * v;
        }
        sum_norms += sqrtf(norm_sq);
    }
    float target_norm = sum_norms / (float)N_inlier;

    float centroid_norm_sq = 0.0f;
    for (int m = 0; m < EMBEDDING_DIM; m++) centroid_norm_sq += centroid[m] * centroid[m];
    float centroid_norm = sqrtf(centroid_norm_sq);
    float scale = (centroid_norm > 1e-6f) ? (target_norm / centroid_norm) : 1.0f;

    face_embedding_t q_fused;
    for (int m = 0; m < EMBEDDING_DIM; m++) {
        int v = (int)roundf(centroid[m] * scale);
        if (v < -128) v = -128;
        if (v >  127) v =  127;
        q_fused.values[m] = (int8_t)v;
    }

    /* ------------------------------------------------------------------ */
    /* Step 4: Candidate extraction                                         */
    /* 4a. Hint path (early-exit): skip full DB scan, use pre-known user.  */
    /* 4b. Full path: one O(N_users) scan on q_fused -> Top-3 candidates.  */
    /* ------------------------------------------------------------------ */
    user_t *top_candidates[SCAN_TOP_CANDIDATES] = {NULL, NULL, NULL};
    float   top_sims[SCAN_TOP_CANDIDATES]       = {-1.0f, -1.0f, -1.0f};

    /* Full scan over cached users to extract true Top-3 candidates */
    for (int i = 0; i < s_cache_size; i++) {
        float sim = cosine_similarity(&q_fused, &s_user_cache[i].embedding);
        /* Insert into sorted top-3 list (descending) */
        for (int t = 0; t < SCAN_TOP_CANDIDATES; t++) {
            if (sim > top_sims[t]) {
                /* Shift lower entries down */
                for (int r = SCAN_TOP_CANDIDATES - 1; r > t; r--) {
                    top_sims[r]       = top_sims[r-1];
                    top_candidates[r] = top_candidates[r-1];
                }
                top_sims[t]       = sim;
                top_candidates[t] = &s_user_cache[i];
                break;
            }
        }
    }
    ESP_LOGI(TAG, "multiframe: top-1=%s (S_fused=%.3f), top-2=%s (S_fused=%.3f)",
             top_candidates[0] ? top_candidates[0]->name : "None", top_sims[0],
             top_candidates[1] ? top_candidates[1]->name : "None", top_sims[1]);

    /* ------------------------------------------------------------------ */
    /* Step 5 & 6: Inlier-only consensus voting                            */
    /* Vote INLIER frames against Top-3 candidates at tau_vote.            */
    /* Consensus threshold: ceil(0.6 * N_inlier).                          */
    /* Winner must be the same candidate as the fused-gate candidate.      */
    /* ------------------------------------------------------------------ */
    int vote_counts[SCAN_TOP_CANDIDATES] = {0, 0, 0};
    int min_votes = (int)ceilf(0.6f * (float)N_inlier);
    if (min_votes < 2) min_votes = 2;

    for (int k = 0; k < N_inlier; k++) {
        int idx = inlier_idx[k];
        int best_t = -1;
        float best_vote_sim = SCAN_VOTE_THRESHOLD; /* must clear relaxed vote gate to count */
        for (int t = 0; t < SCAN_TOP_CANDIDATES; t++) {
            if (!top_candidates[t]) continue;
            float sim = cosine_similarity(&frames[idx], &top_candidates[t]->embedding);
            if (sim > best_vote_sim) {
                best_vote_sim = sim;
                best_t = t;
            }
        }
        if (best_t >= 0) vote_counts[best_t]++;
    }

    /* Find winning candidate with >= min_votes */
    int winner_t = -1;
    for (int t = 0; t < SCAN_TOP_CANDIDATES; t++) {
        ESP_LOGI(TAG, "multiframe: candidate[%d]=%s votes=%d/%d (need %d)",
                 t, top_candidates[t] ? top_candidates[t]->name : "None",
                 vote_counts[t], N_inlier, min_votes);
        if (vote_counts[t] >= min_votes) {
            if (winner_t < 0 || vote_counts[t] > vote_counts[winner_t]) {
                winner_t = t;
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* Step 7: Fused acceptance gate with Top-1 vs Top-2 Margin Check       */
    /* ------------------------------------------------------------------ */
    if (winner_t >= 0 && top_candidates[winner_t] != NULL) {
        float s_fused_winner = top_sims[winner_t];

        /* Find runner-up (the best candidate other than winner_t) */
        float runner_up_sim = -1.0f;
        user_t *runner_up = NULL;
        for (int t = 0; t < SCAN_TOP_CANDIDATES; t++) {
            if (t != winner_t && top_candidates[t] != NULL) {
                if (top_sims[t] > runner_up_sim) {
                    runner_up_sim = top_sims[t];
                    runner_up = top_candidates[t];
                }
            }
        }

        float margin = (runner_up != NULL) ? (s_fused_winner - runner_up_sim) : 1.0f;

        ESP_LOGI(TAG, "multiframe: winner=%s S_fused=%.3f votes=%d/%d, runner_up=%s (sim=%.3f, margin=%.3f)",
                 top_candidates[winner_t]->name, s_fused_winner,
                 vote_counts[winner_t], N_inlier,
                 runner_up ? runner_up->name : "None", runner_up_sim, margin);

        bool pass = false;
        if (s_fused_winner >= RECOGNITION_HIGH_CONF_THRESHOLD) {
            pass = (margin >= RECOGNITION_MARGIN_HIGH);
        } else if (s_fused_winner >= RECOGNITION_THRESHOLD) {
            pass = (margin >= RECOGNITION_MARGIN_MIN);
        }

        if (pass) {
            *out_user             = top_candidates[winner_t];
            *out_fused_confidence = s_fused_winner;
            *out_consensus_votes  = vote_counts[winner_t];
        } else {
            if (s_fused_winner < RECOGNITION_THRESHOLD) {
                ESP_LOGI(TAG, "multiframe: winner %s rejected — S_fused=%.3f below threshold=%.2f",
                         top_candidates[winner_t]->name, s_fused_winner, RECOGNITION_THRESHOLD);
            } else {
                ESP_LOGW(TAG, "multiframe: winner %s REJECTED due to ambiguity! S_fused=%.3f, margin=%.3f < required (runner_up=%s @%.3f)",
                         top_candidates[winner_t]->name, s_fused_winner, margin,
                         runner_up ? runner_up->name : "None", runner_up_sim);
            }
        }
    } else {
        ESP_LOGI(TAG, "multiframe: no consensus winner (min_votes=%d, N_inlier=%d)", min_votes, N_inlier);
    }

    /* Liveness telemetry: compute cumulative landmark-plane displacement across frames
     * as a cheap motion sanity check. A static photo will show near-zero variance.
     * NOTE: advisory log only — does not gate the accept/reject decision. */
    if (N_inlier >= 2) {
        float liveness_var = 0.0f;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            float ref = (float)frames[inlier_idx[0]].values[m];
            for (int k = 1; k < N_inlier; k++) {
                float d = (float)frames[inlier_idx[k]].values[m] - ref;
                liveness_var += d * d;
            }
        }
        liveness_var = sqrtf(liveness_var / (float)(N_inlier - 1));
        ESP_LOGI(TAG, "LIVENESS: embedding_variance=%.2f (low=static/photo risk)", liveness_var);
    }

    return ESP_OK;
}
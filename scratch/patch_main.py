import os

file_path = r"c:\Users\user\Documents\projects\SmartAttendance\main\main.c"

with open(file_path, "r", encoding="utf-8") as f:
    content = f.read()

# 1. Add ui_enrollment.h include if not there
if '#include "ui/ui_enrollment.h"' not in content:
    content = content.replace('#include "ui/ui_attendance.h"', '#include "ui/ui_attendance.h"\n#include "ui/ui_enrollment.h"')

# 2. Replace start_enrollment_task and process_enrollment_frames
old_code_start = """void start_enrollment_task(void *pvParam) {"""
old_code_end = """    /* Restore normal detection framesize */
    camera_set_framesize(CAMERA_FRAME_SIZE);
    
    return ret;
}"""

new_code = """static esp_err_t process_enrollment_frames_for_user(user_t* new_user) {
    /* Heap-allocate large arrays to prevent stack overflow */
    camera_fb_t **frames = (camera_fb_t**)calloc(ENROLL_FRAMES_TOTAL, sizeof(camera_fb_t *));
    aligned_face_t *aligned_frames = (aligned_face_t*)calloc(ENROLL_FRAMES_TOTAL, sizeof(aligned_face_t));
    face_embedding_t *embeddings = (face_embedding_t*)calloc(ENROLL_FRAMES_TOTAL, sizeof(face_embedding_t));
    float *quality_scores = (float*)calloc(ENROLL_FRAMES_TOTAL, sizeof(float));

    if (!frames || !aligned_frames || !embeddings || !quality_scores) {
        ESP_LOGE(TAG, "Failed to allocate enrollment buffers");
        free(frames); free(aligned_frames); free(embeddings); free(quality_scores);
        return ESP_ERR_NO_MEM;
    }

    int captured = 0;
    int kept = 0;
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Starting enrollment for %s, capturing %d frames", new_user->name, ENROLL_FRAMES_TOTAL);
    
    camera_set_framesize(CAMERA_ENROLL_FRAME_SIZE);

    #if ENABLE_BLE_ENROLLMENT
    if (ble_registration_is_connected()) {
        ble_registration_update_status(ENROLL_STATUS_CAPTURING, "Capturing face frames...");
    }
    #endif
    
    /* Capture burst of frames. Wait if face is not detected. */
    int i = 0;
    while (i < ENROLL_FRAMES_TOTAL) {
        if (g_enrollment_cancel) {
            ret = ESP_FAIL;
            goto cleanup;
        }
        
        ui_acquire();
        ui_enrollment_set_capture_progress(i, ENROLL_FRAMES_TOTAL);
        ui_release();
        
        /* Capture frame with autofocus */
        camera_fb_t* fb = camera_capture_with_autofocus();
        if (fb == NULL) {
            ESP_LOGW(TAG, "Frame %d capture failed", i);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        /* Detect face in frame */
        detection_result_t det_result;
        if (face_detector_run(fb, &det_result) != ESP_OK || det_result.face_count == 0) {
            camera_return_frame(fb);
            ui_acquire();
            ui_enrollment_set_face_detected(false);
            ui_release();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue; /* Do not increment 'i', force user to show face */
        }
        
        ui_acquire();
        ui_enrollment_set_face_detected(true);
        ui_release();

        detected_face_t *face = &det_result.faces[0];
        
        /* Compute quality metrics */
        float sharpness = face_detector_compute_sharpness(fb, face);
        float brightness = face_detector_compute_brightness(fb, face);
        float yaw = face_detector_compute_yaw(face);
        
        quality_scores[i] = (sharpness / 100.0f) * 0.5f;
        quality_scores[i] += (1.0f - (fabs(yaw) / ENROLL_YAW_MAX_DEG)) * 0.3f;
        quality_scores[i] += (brightness >= ENROLL_BRIGHTNESS_MIN && 
                              brightness <= ENROLL_BRIGHTNESS_MAX) ? 0.2f : 0.0f;
        
        /* Align face */
        if (face_alignment_align(fb, face, &aligned_frames[i]) != ESP_OK) {
            camera_return_frame(fb);
            continue;
        }
        
        frames[i] = fb;
        i++; /* Frame successfully captured and aligned */
    }
    
    #if ENABLE_BLE_ENROLLMENT
    if (ble_registration_is_connected()) {
        ble_registration_update_status(ENROLL_STATUS_PROCESSING, "Processing face template...");
    }
    #endif
    
    /* Select best frames */
    int selected_indices[ENROLL_FRAMES_KEEP];
    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        selected_indices[j] = -1;
    }
    
    for (int j = 0; j < ENROLL_FRAMES_TOTAL; j++) {
        if (frames[j] == NULL) continue;
        for (int k = 0; k < ENROLL_FRAMES_KEEP; k++) {
            if (selected_indices[k] == -1 || quality_scores[j] > quality_scores[selected_indices[k]]) {
                for (int m = ENROLL_FRAMES_KEEP - 1; m > k; m--) {
                    selected_indices[m] = selected_indices[m-1];
                }
                selected_indices[k] = j;
                break;
            }
        }
    }
    
    /* Extract embeddings */
    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        int idx = selected_indices[j];
        if (idx == -1) continue;
        if (feature_extractor_run(&aligned_frames[idx], &embeddings[idx]) != ESP_OK) {
            selected_indices[j] = -1;
            continue;
        }
        kept++;
    }
    
    if (kept == 0) {
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    /* Average embeddings */
    face_embedding_t final_embedding;
    memset(&final_embedding, 0, sizeof(face_embedding_t));
    for (int j = 0; j < ENROLL_FRAMES_KEEP; j++) {
        int idx = selected_indices[j];
        if (idx == -1) continue;
        for (int m = 0; m < EMBEDDING_DIM; m++) {
            final_embedding.values[m] += embeddings[idx].values[m];
        }
    }
    for (int m = 0; m < EMBEDDING_DIM; m++) {
        final_embedding.values[m] /= kept;
    }
    
    /* Store in database */
    new_user->embedding = final_embedding;
    new_user->created_at = time(NULL);
    generate_uuid_hex(new_user->uuid, sizeof(new_user->uuid));
    
    ret = db_insert_user(new_user);
    if (ret == ESP_OK) {
        recognizer_add_to_cache(new_user);
    }
    
cleanup:
    for (int j = 0; j < ENROLL_FRAMES_TOTAL; j++) {
        if (frames[j]) camera_return_frame(frames[j]);
        if (aligned_frames[j].data) face_alignment_free(&aligned_frames[j]);
    }
    free(frames);
    free(aligned_frames);
    free(embeddings);
    free(quality_scores);
    
    camera_set_framesize(CAMERA_FRAME_SIZE);
    return ret;
}

void start_single_capture_task(void *pvParam) {
    int student_idx = (int)(intptr_t)pvParam;
    
    enrollment_data_t ble_data;
    if (!ble_registration_peek_student(student_idx, &ble_data)) {
        ui_acquire();
        ui_enrollment_set_status(false, "Failed to load student data.");
        ui_release();
        vTaskDelete(NULL);
        return;
    }
    
    user_t new_user;
    memset(&new_user, 0, sizeof(user_t));
    strncpy(new_user.name, ble_data.name, sizeof(new_user.name) - 1);
    strncpy(new_user.student_id, ble_data.student_id, sizeof(new_user.student_id) - 1);
    strncpy(new_user.role, ble_data.role, sizeof(new_user.role) - 1);

    esp_err_t ret = process_enrollment_frames_for_user(&new_user);

    ui_acquire();
    if (ret == ESP_OK) {
        ble_registration_consume_student(student_idx);
        ui_enrollment_show_success(new_user.name, student_idx);
        
        #if ENABLE_BLE_ENROLLMENT
        ble_registration_set_result(true, new_user.id);
        #endif
        
        #if ENABLE_AUDIO_GUIDANCE
        audio_play(AUDIO_PROMPTS_PATH "enroll_success.wav", true);
        #endif
    } else {
        ui_enrollment_set_status(false, "Face capture failed. Try again.");
        
        #if ENABLE_BLE_ENROLLMENT
        ble_registration_set_result(false, 0);
        #endif
        
        #if ENABLE_AUDIO_GUIDANCE
        audio_play(AUDIO_PROMPTS_PATH "enroll_fail.wav", true);
        #endif
    }
    ui_release();

    vTaskDelete(NULL);
}

void start_enrollment_task(void *pvParam) {
    (void)pvParam;
    set_system_state(SYSTEM_STATE_ENROLLMENT);
    
    #if ENABLE_AUDIO_GUIDANCE
    audio_play(AUDIO_PROMPTS_PATH "enroll_start.wav", true);
    #endif
    
    ui_acquire();
    ui_show_enrollment_screen();
    ui_release();
    
    #if ENABLE_BLE_ENROLLMENT
    ble_registration_start_advertising();
    #endif
    
    g_enrollment_cancel = false;
    
    /* Wait until the admin clicks the close button */
    while (!g_enrollment_cancel) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    #if ENABLE_BLE_ENROLLMENT
    ble_registration_stop_advertising();
    #endif
    
    set_system_state(SYSTEM_STATE_NORMAL);
    
    /* Handle wizard role override completion */
    if (strlen((char*)g_enrollment_role_override) > 0) {
        memset((char*)g_enrollment_role_override, 0, sizeof(g_enrollment_role_override));
        g_wizard_admin_enrolled = true;
    }

    ui_acquire();
    ui_close_enrollment_screen();
    ui_release();

    vTaskDelete(NULL);
}"""

start_idx = content.find(old_code_start)
end_idx = content.find(old_code_end)

if start_idx != -1 and end_idx != -1:
    content = content[:start_idx] + new_code + content[end_idx + len(old_code_end):]
    with open(file_path, "w", encoding="utf-8") as f:
        f.write(content)
    print("Patched main.c successfully.")
else:
    print("Could not find the target code block in main.c")


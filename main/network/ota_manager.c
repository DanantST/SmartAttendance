/**
 * @file ota_manager.c
 * @brief Over-The-Air (OTA) update manager implementation
 */

#include "ota_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "config.h"
#include <string.h>

static const char *TAG = "OTA";
static int s_ota_progress = -1;

static void ota_task(void *pvParameter) {
    char* url = (char*)pvParameter;
    ESP_LOGI(TAG, "Starting OTA update from: %s", url);
    s_ota_progress = 0;

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t update_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        s_ota_progress = -1;
        free(url);
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(update_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed");
        esp_https_ota_abort(update_handle);
        s_ota_progress = -1;
        free(url);
        vTaskDelete(NULL);
        return;
    }

    /* Version check (Optional: only update if version is higher) */
    ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);

    while (1) {
        err = esp_https_ota_perform(update_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        /* Update progress */
        size_t len_total = esp_https_ota_get_image_size(update_handle);
        size_t len_read = esp_https_ota_get_image_len_read(update_handle);
        if (len_total > 0) {
            s_ota_progress = (len_read * 100) / len_total;
        }
    }

    if (esp_https_ota_is_complete_data_received(update_handle)) {
        err = esp_https_ota_finish(update_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA upgrade successful. Rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            ESP_LOGE(TAG, "OTA upgrade failed: %d", err);
        }
    } else {
        ESP_LOGE(TAG, "Complete data not received");
        esp_https_ota_abort(update_handle);
    }

    s_ota_progress = -1;
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_manager_start(const char* url) {
    if (s_ota_progress != -1) {
        return ESP_ERR_INVALID_STATE; // Already in progress
    }

    char* url_copy = strdup(url);
    if (!url_copy) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, NULL);
    if (ret != pdPASS) {
        free(url_copy);
        return ESP_FAIL;
    }

    return ESP_OK;
}

int ota_manager_get_progress(void) {
    return s_ota_progress;
}

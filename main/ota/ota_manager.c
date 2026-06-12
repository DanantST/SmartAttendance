#include "ota_manager.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"

static const char *TAG = "OTA_MGR";

esp_err_t ota_manager_update(const char *url) {
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    ESP_LOGI(TAG, "Attempting to download update from %s", url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed: %d", ret);
    }
    return ret;
}

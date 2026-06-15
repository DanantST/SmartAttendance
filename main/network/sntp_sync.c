/**
 * @file sntp_sync.c
 * @brief SNTP time synchronization implementation
 */

#include "network/sntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

static const char *TAG = "SNTP";
static SemaphoreHandle_t s_sntp_mutex = NULL;

static void sntp_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    
    char strftime_buf[64];
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

esp_err_t sntp_sync_init(void) {
    if (s_sntp_mutex == NULL) {
        s_sntp_mutex = xSemaphoreCreateMutex();
    }
    
    if (s_sntp_mutex && xSemaphoreTake(s_sntp_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "SNTP init busy, skipping concurrent call");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing SNTP");
    
    /* Set timezone to WAT (West Africa Time) - GMT+1, no DST */
    setenv("TZ", "WAT-1", 1);
    tzset();
    
    /* Clean up any existing SNTP instance to force a fresh sync on reconnection */
    esp_netif_sntp_deinit();
    
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
    config.sync_cb = sntp_sync_notification_cb;
    
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %d", ret);
        if (s_sntp_mutex) xSemaphoreGive(s_sntp_mutex);
        return ret;
    }
    
    if (s_sntp_mutex) xSemaphoreGive(s_sntp_mutex);
    return ESP_OK;
}

bool sntp_sync_is_synchronized(void) {
    sntp_sync_status_t status = sntp_get_sync_status();
    return (status == SNTP_SYNC_STATUS_COMPLETED);
}

esp_err_t sntp_sync_wait_for_sync(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (timeout %" PRIu32 "ms)", timeout_ms);
    
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (sntp_sync_is_synchronized()) {
            ESP_LOGI(TAG, "System time synchronized");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        waited += 100;
    }
    
    ESP_LOGW(TAG, "Failed to update system time within timeout");
    return ESP_ERR_TIMEOUT;
}

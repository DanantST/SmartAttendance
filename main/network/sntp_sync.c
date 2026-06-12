/**
 * @file sntp_sync.c
 * @brief SNTP time synchronization implementation
 */

#include "network/sntp_sync.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

static const char *TAG = "SNTP";

static void sntp_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
    
    char strftime_buf[64];
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

esp_err_t sntp_sync_init(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    
    /* Set timezone to UK (GMT/BST) - 0 hours from UTC, but handles DST */
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = sntp_sync_notification_cb;
    
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %d", ret);
        return ret;
    }
    
    return ESP_OK;
}

bool sntp_sync_is_synchronized(void) {
    sntp_sync_status_t status = sntp_get_sync_status();
    return (status == SNTP_SYNC_STATUS_COMPLETED);
}

esp_err_t sntp_sync_wait_for_sync(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (timeout %" PRIu32 "ms)", timeout_ms);
    
    esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update system time within timeout");
    } else {
        ESP_LOGI(TAG, "System time synchronized");
    }
    
    return ret;
}

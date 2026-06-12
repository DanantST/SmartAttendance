/**
 * @file wifi_manager.c
 * @brief Wi-Fi connection manager implementation with NVS storage
 */

#include "wifi_manager.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "config.h"
#include "network/sntp_sync.h"
#include <string.h>

/* Note: On ESP32-P4, esp_wifi headers and implementations are provided by the esp_hosted_fg component over SDIO */

static const char* TAG __attribute__((unused)) = "WIFI_MGR";

#if CONFIG_IDF_TARGET_ESP32P4 && !CONFIG_ESP_WIFI_REMOTE_LIBRARY_HOSTED
#error "ESP32-P4 Wi-Fi requires esp_wifi_remote hosted support to be enabled."
#endif

#ifndef CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM
#define CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM 10
#define CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM 32
#define CONFIG_ESP_WIFI_TX_BUFFER_TYPE 1
#define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF 1
#define CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM 7
#endif

/* NVS namespace for Wi-Fi credentials */
#define WIFI_NVS_NAMESPACE "wifi_creds"
#define NVS_KEY_COUNT      "count"
#define NVS_KEY_SSID_PFX   "s_"
#define NVS_KEY_PASS_PFX   "p_"
#define MAX_KNOWN_NETWORKS 5

/* Event group bits */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static int s_retry_count = 0;
static char s_current_ssid[32] = {0};
static char s_current_password[64] = {0};

/* Forward declarations */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data);
static esp_err_t wifi_init_sta(void);

/**
 * Issue 5.1: Replaced ESP_ERROR_CHECK (which aborts on failure) with
 * graceful error returns so the system doesn't crash-loop on Wi-Fi init failure.
 */
esp_err_t wifi_manager_init(void) {
    ESP_LOGI(TAG, "Initializing Wi-Fi manager (via esp_hosted)");
    esp_err_t ret;
    
    /* Initialize TCP/IP stack */
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %d", ret);
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE means loop already exists — that's fine */
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %d", ret);
        return ret;
    }
    
    /* Create default Wi-Fi station and AP interfaces */
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    
    /* Initialize Wi-Fi stack */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %d", ret);
        return ret;
    }
    
    /* Register event handler */
    s_wifi_event_group = xEventGroupCreate();
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WIFI event handler register failed: %d", ret);
        return ret;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "IP event handler register failed: %d", ret);
        return ret;
    }
    
    /* Start Wi-Fi */
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %d", ret);
        return ret;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %d", ret);
        return ret;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(50);
    
    ESP_LOGI(TAG, "Wi-Fi manager initialized");
    return ESP_OK;
}

static void sntp_sync_on_connected_task(void *pvParameters) {
    ESP_LOGI("WIFI_MGR", "Wi-Fi connected. Starting SNTP synchronization...");
    sntp_sync_init();
    sntp_sync_wait_for_sync(10000);
    vTaskDelete(NULL);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* Wi-Fi started, attempt to connect if credentials exist */
        ESP_LOGI(TAG, "Wi-Fi started");
        wifi_manager_connect_saved();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_count++;
        ESP_LOGW(TAG, "Wi-Fi disconnected, retry count: %d", s_retry_count);
        
        if (s_retry_count < WIFI_MAX_RETRY) {
            s_wifi_status = WIFI_STATUS_CONNECTING;
            esp_wifi_connect();
        } else {
            s_wifi_status = WIFI_STATUS_CONNECTION_FAILED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Wi-Fi connection failed after %d retries", WIFI_MAX_RETRY);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_retry_count = 0;
        s_wifi_status = WIFI_STATUS_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xTaskCreate(sntp_sync_on_connected_task, "sntp_conn_sync", 4096, NULL, 5, NULL);
    }
}

static esp_err_t wifi_init_sta(void) {
    if (strlen(s_current_ssid) == 0) {
        ESP_LOGW(TAG, "No SSID configured");
        return ESP_FAIL;
    }
    
    wifi_config_t wifi_config = {0};
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
    /* Dynamically configure authmode. Empty password implies open Wi-Fi network. */
    if (strlen(s_current_password) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }
    
    strncpy((char*)wifi_config.sta.ssid, s_current_ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    
    strncpy((char*)wifi_config.sta.password, s_current_password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    
    ESP_LOGI(TAG, "Connecting to SSID: %s", s_current_ssid);
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %d", ret);
        return ret;
    }
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %d", ret);
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char* ssid, const char* password) {
    if (!ssid) return ESP_ERR_INVALID_ARG;
    s_retry_count = 0;
    
    /* Save credentials to NVS */
    esp_err_t ret = wifi_manager_save_credentials(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials: %d", ret);
        return ret;
    }
    
    strncpy(s_current_ssid, ssid, sizeof(s_current_ssid) - 1);
    s_current_ssid[sizeof(s_current_ssid) - 1] = '\0';

    if (password) {
        strncpy(s_current_password, password, sizeof(s_current_password) - 1);
        s_current_password[sizeof(s_current_password) - 1] = '\0';
    } else {
        s_current_password[0] = '\0';
    }
    
    return wifi_init_sta();
}

static void connect_saved_task(void* arg) {
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) { vTaskDelete(NULL); return; }
    
    int32_t count = 0;
    nvs_get_i32(nvs, NVS_KEY_COUNT, &count);
    
    if (count == 0) {
        nvs_close(nvs);
        vTaskDelete(NULL); return;
    }
    
    /* Scan for networks to find a match among saved ones */
    wifi_ap_record_t scan_results[20];
    int found_count = wifi_manager_scan(scan_results, 20);
    
    for (int i = 0; i < count; i++) {
        char key[16], saved_ssid[32];
        size_t len = sizeof(saved_ssid);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_SSID_PFX, i);
        if (nvs_get_str(nvs, key, saved_ssid, &len) != ESP_OK) continue;
        
        for (int j = 0; j < found_count; j++) {
            if (strcmp(saved_ssid, (char*)scan_results[j].ssid) == 0) {
                ESP_LOGI(TAG, "Found known network in range: %s", saved_ssid);
                strncpy(s_current_ssid, saved_ssid, sizeof(s_current_ssid));
                
                /* Get associated password */
                char p_key[16];
                size_t p_len = sizeof(s_current_password);
                snprintf(p_key, sizeof(p_key), "%s%d", NVS_KEY_PASS_PFX, i);
                if (nvs_get_str(nvs, p_key, s_current_password, &p_len) != ESP_OK) {
                    s_current_password[0] = '\0';
                }
                
                nvs_close(nvs);
                wifi_init_sta();
                vTaskDelete(NULL); return;
            }
        }
    }
    
    /* If no scan match, try the most recently used (index 0) */
    char key[16], p_key[16];
    size_t len = sizeof(s_current_ssid);
    size_t p_len = sizeof(s_current_password);
    snprintf(key, sizeof(key), "%s0", NVS_KEY_SSID_PFX);
    snprintf(p_key, sizeof(p_key), "%s0", NVS_KEY_PASS_PFX);
    
    nvs_get_str(nvs, key, s_current_ssid, &len);
    if (nvs_get_str(nvs, p_key, s_current_password, &p_len) != ESP_OK) {
        s_current_password[0] = '\0';
    }
    
    nvs_close(nvs);
    
    wifi_init_sta();
    vTaskDelete(NULL);
}

esp_err_t wifi_manager_connect_saved(void) {
    xTaskCreate(connect_saved_task, "connect_saved", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;
    
    int32_t count = 0;
    nvs_get_i32(nvs, NVS_KEY_COUNT, &count);
    
    /* Check if already exists and move to top, or add new at top */
    int idx = count;
    for (int i = 0; i < count; i++) {
        char key[16], saved_ssid[32]; size_t len = sizeof(saved_ssid);
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_SSID_PFX, i);
        if (nvs_get_str(nvs, key, saved_ssid, &len) == ESP_OK && strcmp(saved_ssid, ssid) == 0) {
            idx = i; break;
        }
    }
    if (idx >= MAX_KNOWN_NETWORKS) idx = MAX_KNOWN_NETWORKS - 1;
    
    for (int i = idx; i > 0; i--) {
        char k_old[16], p_old[16], k_new[16], p_new[16];
        char saved_ssid[32], saved_pass[64]; size_t len=32, p_len=64;
        snprintf(k_old, 16, "%s%d", NVS_KEY_SSID_PFX, i - 1); snprintf(p_old, 16, "%s%d", NVS_KEY_PASS_PFX, i - 1);
        snprintf(k_new, 16, "%s%d", NVS_KEY_SSID_PFX, i);     snprintf(p_new, 16, "%s%d", NVS_KEY_PASS_PFX, i);
        if (nvs_get_str(nvs, k_old, saved_ssid, &len) == ESP_OK) {
            nvs_set_str(nvs, k_new, saved_ssid);
            if (nvs_get_str(nvs, p_old, saved_pass, &p_len) == ESP_OK) nvs_set_str(nvs, p_new, saved_pass);
            else nvs_erase_key(nvs, p_new);
        }
    }
    
    nvs_set_str(nvs, "s_0", ssid);
    nvs_set_str(nvs, "p_0", password ? password : "");
    if (count < MAX_KNOWN_NETWORKS && idx == count) nvs_set_i32(nvs, NVS_KEY_COUNT, count + 1);
    
    nvs_commit(nvs);
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t wifi_manager_clear_credentials(void) {
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) return ret;
    
    ret = nvs_erase_all(nvs);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Cleared saved Wi-Fi credentials");
        memset(s_current_ssid, 0, sizeof(s_current_ssid));
    }
    
    return ret;
}

esp_err_t wifi_manager_disconnect(void) {
    s_retry_count = 0;
    s_wifi_status = WIFI_STATUS_DISCONNECTED;
    return esp_wifi_disconnect();
}

wifi_status_t wifi_manager_get_status(void) {
    return s_wifi_status;
}

int wifi_manager_get_rssi(void) {
    if (s_wifi_status != WIFI_STATUS_CONNECTED) return 0;
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

int wifi_manager_scan(wifi_ap_record_t* networks, int max_networks) {
    uint16_t ap_count = 0;
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %d", ret);
        return 0;
    }
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan get num failed: %d", ret);
        return 0;
    }
    
    if (ap_count > max_networks) ap_count = max_networks;
    
    ret = esp_wifi_scan_get_ap_records(&ap_count, networks);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan get records failed: %d", ret);
        return 0;
    }
    return ap_count;
}

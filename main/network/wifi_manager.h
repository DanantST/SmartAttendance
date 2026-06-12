/**
 * @file wifi_manager.h
 * @brief Wi-Fi connection management with NVS storage
 * 
 * Handles Wi-Fi connection, credential storage in encrypted NVS,
 * and automatic reconnection [citation:2][citation:5].
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "esp_err.h"
#include <stdbool.h>
#include "esp_wifi.h"

/**
 * @brief Wi-Fi connection status
 */
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_CONNECTION_FAILED
} wifi_status_t;

/**
 * @brief Initialize Wi-Fi manager
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to a Wi-Fi network
 * @param ssid network SSID
 * @param password network password (NULL for open network)
 * @return ESP_OK if connection started successfully
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password);

/**
 * @brief Connect to saved network (from NVS)
 * @return ESP_OK if saved credentials found and connection started
 */
esp_err_t wifi_manager_connect_saved(void);

/**
 * @brief Disconnect from current network
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Get current connection status
 * @return wifi_status_t current status
 */
wifi_status_t wifi_manager_get_status(void);

/**
 * @brief Get current Wi-Fi signal strength (RSSI)
 * @return RSSI in dBm, 0 if not connected
 */
int wifi_manager_get_rssi(void);

/**
 * @brief Save current network credentials to NVS
 * @param ssid network SSID
 * @param password network password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_save_credentials(const char* ssid, const char* password);

/**
 * @brief Clear saved credentials from NVS
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_clear_credentials(void);

/**
 * @brief Scan for available networks
 * @param networks array to store scan results (caller allocates)
 * @param max_networks maximum number of networks to return
 * @return number of networks found, negative on error
 */
int wifi_manager_scan(wifi_ap_record_t* networks, int max_networks);


#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
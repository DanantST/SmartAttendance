/**
 * @file wifi_ap_portal.h
 * @brief Dynamic SoftAP and Captive Portal Web Server for standalone enrollment
 */

#ifndef WIFI_AP_PORTAL_H
#define WIFI_AP_PORTAL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Wi-Fi AP and launch the Captive Portal (HTTP + DNS redirect)
 * @return ESP_OK on success
 */
esp_err_t wifi_ap_portal_start(void);

/**
 * @brief Stop the Captive Portal servers and shutdown the Wi-Fi AP,
 *        restoring previous station-mode Wi-Fi state.
 */
void wifi_ap_portal_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_AP_PORTAL_H */

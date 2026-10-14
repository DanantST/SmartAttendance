/**
 * @file ui_firmware_mgr.cpp
 * @brief Firmware Management & Rollback Screen Implementation
 */

#include "ui_firmware_mgr.h"
#include "ui_main.h"
#include "ui_theme.h"
#include "ui_settings.h"
#include "network/ota_manager.h"
#include "display/lcd_driver.h"
#include "config.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "UI_FW_MGR";

/* UI Screen and Widgets */
static lv_obj_t *s_fw_screen          = NULL;
static lv_obj_t *s_list_container     = NULL;
static lv_obj_t *s_cur_version_lbl    = NULL;
static lv_obj_t *s_cur_slot_lbl       = NULL;
static lv_obj_t *s_cur_build_lbl      = NULL;

/* Progress Modal Widgets */
static lv_obj_t *s_progress_modal     = NULL;
static lv_obj_t *s_progress_bar       = NULL;
static lv_obj_t *s_progress_title     = NULL;
static lv_obj_t *s_progress_msg_label = NULL;

/* Context for Selected Firmware Flash */
static char s_selected_filepath[256]  = {0};
static char s_selected_filename[96]   = {0};
static char s_selected_version[32]    = {0};

/* Forward Declarations */
static void create_firmware_screen(void);
static void refresh_sd_firmwares_async(void);
static void show_confirm_modal(const firmware_info_t *info);
static void show_progress_modal(const char *title, const char *initial_msg);
static void hide_progress_modal(void);
static void start_flash_process(void);
static void start_cloud_download(const char *url, const char *ver);
static void show_cloud_catalog_modal(void);

/* ------------------------------------------------------------------ */
/*  Public Screen Navigation API                                      */
/* ------------------------------------------------------------------ */

void ui_show_firmware_mgr_screen(void) {
    ESP_LOGI(TAG, "Opening Firmware Manager screen");
    if (s_fw_screen && lv_obj_is_valid(s_fw_screen)) {
        lv_scr_load(s_fw_screen);
        refresh_sd_firmwares_async();
        return;
    }
    s_fw_screen = NULL;
    create_firmware_screen();
}

void ui_close_firmware_mgr_screen(void) {
    if (!s_fw_screen) return;

    if (lv_scr_act() == s_fw_screen) {
        lv_obj_t *scr = s_fw_screen;
        s_fw_screen = NULL;
        s_list_container = NULL;
        /* Return to Settings screen */
        ui_show_settings_screen();
        lv_obj_delete_async(scr);
        return;
    }

    lv_obj_delete(s_fw_screen);
    s_fw_screen = NULL;
    s_list_container = NULL;
}

/* ------------------------------------------------------------------ */
/*  OTA Progress & Completion Callbacks (Thread-Safe)                 */
/* ------------------------------------------------------------------ */

static void ota_progress_callback(int percent, const char *status_msg) {
    if (ui_lock()) {
        if (s_progress_bar && lv_obj_is_valid(s_progress_bar)) {
            lv_bar_set_value(s_progress_bar, percent, LV_ANIM_OFF);
        }
        if (s_progress_msg_label && lv_obj_is_valid(s_progress_msg_label)) {
            lv_label_set_text(s_progress_msg_label, status_msg ? status_msg : "");
        }
        ui_unlock();
    }
}

static void ota_flash_done_callback(esp_err_t err, const char *message) {
    ESP_LOGI(TAG, "OTA Flash completed with result %d: %s", err, message ? message : "");
    if (ui_lock()) {
        if (err == ESP_OK) {
            if (s_progress_msg_label && lv_obj_is_valid(s_progress_msg_label)) {
                lv_label_set_text(s_progress_msg_label, "Flashing successful! Restarting...");
            }
        } else {
            hide_progress_modal();
            ui_show_notification(NOTIFY_ERROR, "Rollback Failed", message ? message : "Flash error", 4000);
        }
        ui_unlock();
    }
}

static void ota_download_done_callback(esp_err_t err, const char *message) {
    ESP_LOGI(TAG, "OTA Download completed with result %d: %s", err, message ? message : "");
    if (ui_lock()) {
        hide_progress_modal();
        if (err == ESP_OK) {
            ui_show_notification(NOTIFY_SUCCESS, "OTA Download", "Firmware downloaded to SD card!", 3000);
            refresh_sd_firmwares_async();
        } else {
            ui_show_notification(NOTIFY_ERROR, "Download Failed", message ? message : "Network error", 4000);
        }
        ui_unlock();
    }
}

/* ------------------------------------------------------------------ */
/*  Progress Modal                                                    */
/* ------------------------------------------------------------------ */

static void show_progress_modal(const char *title, const char *initial_msg) {
    if (s_progress_modal && lv_obj_is_valid(s_progress_modal)) {
        lv_obj_delete(s_progress_modal);
        s_progress_modal = NULL;
    }

    /* Modal Backdrop on Top Layer */
    s_progress_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_progress_modal, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(s_progress_modal, 0, 0);
    lv_obj_set_style_bg_color(s_progress_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_progress_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_progress_modal, 0, 0);
    lv_obj_set_style_pad_all(s_progress_modal, 0, 0);
    lv_obj_remove_flag(s_progress_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* Modal Card */
    lv_obj_t *card = lv_obj_create(s_progress_modal);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 540, 260);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E293B), 0); /* Dark slate */
    lv_obj_set_style_border_color(card, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);

    /* Title */
    s_progress_title = lv_label_create(card);
    lv_label_set_text(s_progress_title, title);
    lv_obj_set_style_text_color(s_progress_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_progress_title, &lv_font_montserrat_16, 0);
    lv_obj_align(s_progress_title, LV_ALIGN_TOP_MID, 0, 5);

    /* Spinner */
    lv_obj_t *spinner = lv_spinner_create(card);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_TOP_MID, 0, 35);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);

    /* Progress Bar */
    s_progress_bar = lv_bar_create(card);
    lv_obj_set_size(s_progress_bar, 480, 16);
    lv_obj_align(s_progress_bar, LV_ALIGN_TOP_MID, 0, 95);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x334155), 0);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, 8, 0);
    lv_obj_set_style_radius(s_progress_bar, 8, LV_PART_INDICATOR);

    /* Message Status */
    s_progress_msg_label = lv_label_create(card);
    lv_label_set_text(s_progress_msg_label, initial_msg);
    lv_obj_set_style_text_color(s_progress_msg_label, lv_color_hex(0xCBD5E1), 0);
    lv_obj_set_style_text_font(s_progress_msg_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_progress_msg_label, LV_ALIGN_TOP_MID, 0, 125);

    /* Critical Warning Notice */
    lv_obj_t *warn_lbl = lv_label_create(card);
    lv_label_set_text(warn_lbl, LV_SYMBOL_WARNING " DO NOT POWER OFF OR UNPLUG SD CARD");
    lv_obj_set_style_text_color(warn_lbl, lv_color_hex(0xF59E0B), 0); /* Amber */
    lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(warn_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void hide_progress_modal(void) {
    if (s_progress_modal && lv_obj_is_valid(s_progress_modal)) {
        lv_obj_delete(s_progress_modal);
        s_progress_modal = NULL;
        s_progress_bar = NULL;
        s_progress_title = NULL;
        s_progress_msg_label = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Confirmation Modal Before Flashing                                */
/* ------------------------------------------------------------------ */

static void show_confirm_modal(const firmware_info_t *info) {
    if (!info) return;

    strncpy(s_selected_filepath, info->filepath, sizeof(s_selected_filepath) - 1);
    strncpy(s_selected_filename, info->filename, sizeof(s_selected_filename) - 1);
    strncpy(s_selected_version, info->version, sizeof(s_selected_version) - 1);

    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(modal);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 560, 310);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 25, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_REFRESH " Confirm Firmware Rollback / Flash");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Description */
    lv_obj_t *desc = lv_label_create(card);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "You are about to install this firmware build:\n\n"
             "  • Version: %s\n"
             "  • File: %s\n"
             "  • Compiled: %s %s\n"
             "  • Size: %.2f MB\n\n"
             "The device will flash the standby partition and reboot.",
             info->version, info->filename, info->compile_date, info->compile_time,
             (double)info->size_bytes / (1024.0 * 1024.0));
    lv_label_set_text(desc, buf);
    lv_obj_set_style_text_color(desc, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 0, 35);

    /* Cancel Button */
    lv_obj_t *btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, 130, 42);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 10, 0);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x475569), 0);
    lv_obj_set_style_radius(btn_cancel, 8, 0);
    lv_obj_t *lbl_c = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_c, "Cancel");
    lv_obj_center(lbl_c);
    lv_obj_add_event_cb(btn_cancel, [](lv_event_t *e) {
        lv_obj_t *m = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_delete_async(m);
    }, LV_EVENT_CLICKED, modal);

    /* Flash Button */
    lv_obj_t *btn_flash = lv_btn_create(card);
    lv_obj_set_size(btn_flash, 180, 42);
    lv_obj_align(btn_flash, LV_ALIGN_BOTTOM_RIGHT, -10, 0);
    lv_obj_set_style_bg_color(btn_flash, lv_color_hex(0x10B981), 0); /* Vibrant Green */
    lv_obj_set_style_radius(btn_flash, 8, 0);
    lv_obj_t *lbl_f = lv_label_create(btn_flash);
    lv_label_set_text(lbl_f, LV_SYMBOL_OK " Flash & Reboot");
    lv_obj_center(lbl_f);
    lv_obj_add_event_cb(btn_flash, [](lv_event_t *e) {
        lv_obj_t *m = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_delete_async(m);
        start_flash_process();
    }, LV_EVENT_CLICKED, modal);
}

static void start_flash_process(void) {
    if (strlen(s_selected_filepath) == 0) return;
    ESP_LOGI(TAG, "Starting flash process for %s", s_selected_filepath);

    show_progress_modal("Flashing Firmware from SD Card", "Initializing flash buffer...");

    esp_err_t err = ota_manager_flash_from_sd_async(s_selected_filepath,
                                                   ota_progress_callback,
                                                   ota_flash_done_callback);
    if (err != ESP_OK) {
        hide_progress_modal();
        ui_show_notification(NOTIFY_ERROR, "Flash Start Failed", "Could not start flashing task", 3000);
    }
}

/* ------------------------------------------------------------------ */
/*  Cloud Download Flow                                               */
/* ------------------------------------------------------------------ */

static void start_cloud_download(const char *url, const char *ver) {
    if (!url || strlen(url) == 0) {
        ui_show_notification(NOTIFY_WARNING, "Cloud Update", "No URL specified", 3000);
        return;
    }

    show_progress_modal("Downloading Firmware to SD Card", "Connecting to update server...");

    esp_err_t err = ota_manager_download_to_sd_async(url, ver,
                                                    ota_progress_callback,
                                                    ota_download_done_callback);
    if (err != ESP_OK) {
        hide_progress_modal();
        ui_show_notification(NOTIFY_ERROR, "Download Failed", "Could not start download task", 3000);
    }
}

/* ------------------------------------------------------------------ */
/*  Cloud Firmware Catalog Browser Modal                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char url[256];
    char version[32];
    lv_obj_t *modal;
} cloud_dl_param_t;

typedef struct {
    char endpoint[128];
    lv_obj_t *modal;
    lv_obj_t *list_box;
    lv_obj_t *status_lbl;
    lv_obj_t *spinner;
} catalog_worker_param_t;

static void fetch_cloud_catalog_worker_task(void *pvParam) {
    catalog_worker_param_t *params = (catalog_worker_param_t *)pvParam;
    char url[256];
    snprintf(url, sizeof(url), "%s/api/firmware/list", params->endpoint);

    ESP_LOGI(TAG, "Querying cloud firmware catalog from: %s", url);

    esp_http_client_config_t config = {};
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        if (ui_lock()) {
            if (params->status_lbl && lv_obj_is_valid(params->status_lbl)) {
                lv_label_set_text(params->status_lbl, "Failed to initialize HTTP client");
            }
            if (params->spinner && lv_obj_is_valid(params->spinner)) {
                lv_obj_add_flag(params->spinner, LV_OBJ_FLAG_HIDDEN);
            }
            ui_unlock();
        }
        free(params);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to cloud catalog: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        if (ui_lock()) {
            if (params->status_lbl && lv_obj_is_valid(params->status_lbl)) {
                lv_label_set_text_fmt(params->status_lbl, "Failed to connect to server (%s)", esp_err_to_name(err));
            }
            if (params->spinner && lv_obj_is_valid(params->spinner)) {
                lv_obj_add_flag(params->spinner, LV_OBJ_FLAG_HIDDEN);
            }
            ui_unlock();
        }
        free(params);
        vTaskDelete(NULL);
        return;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int buf_size = (content_len > 0 && content_len < 32768) ? (content_len + 512) : 16384;
    char *json_buf = (char *)malloc(buf_size);
    if (!json_buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(params);
        vTaskDelete(NULL);
        return;
    }

    int total_read = 0;
    while (total_read < buf_size - 1) {
        int r = esp_http_client_read(client, json_buf + total_read, buf_size - 1 - total_read);
        if (r <= 0) break;
        total_read += r;
    }
    json_buf[total_read] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(json_buf);
    free(json_buf);

    if (ui_lock()) {
        if (params->modal && lv_obj_is_valid(params->modal)) {
            if (params->spinner && lv_obj_is_valid(params->spinner)) {
                lv_obj_add_flag(params->spinner, LV_OBJ_FLAG_HIDDEN);
            }

            cJSON *firmwares = root ? cJSON_GetObjectItem(root, "firmwares") : NULL;
            int count = (firmwares && cJSON_IsArray(firmwares)) ? cJSON_GetArraySize(firmwares) : 0;

            if (count == 0) {
                if (params->status_lbl && lv_obj_is_valid(params->status_lbl)) {
                    lv_label_set_text(params->status_lbl, "No firmware releases available on server.");
                }
            } else {
                if (params->status_lbl && lv_obj_is_valid(params->status_lbl)) {
                    lv_obj_add_flag(params->status_lbl, LV_OBJ_FLAG_HIDDEN);
                }

                for (int i = 0; i < count; i++) {
                    cJSON *item = cJSON_GetArrayItem(firmwares, i);
                    if (!item) continue;

                    const char *ver = cJSON_GetObjectItem(item, "version") ? cJSON_GetObjectItem(item, "version")->valuestring : "Unknown";
                    const char *fname = cJSON_GetObjectItem(item, "filename") ? cJSON_GetObjectItem(item, "filename")->valuestring : "";
                    const char *dl_url = cJSON_GetObjectItem(item, "download_url") ? cJSON_GetObjectItem(item, "download_url")->valuestring : "";
                    const char *cdate = cJSON_GetObjectItem(item, "compile_date") ? cJSON_GetObjectItem(item, "compile_date")->valuestring : "";
                    const char *ctime = cJSON_GetObjectItem(item, "compile_time") ? cJSON_GetObjectItem(item, "compile_time")->valuestring : "";
                    double sz_mb = 0.0;
                    cJSON *sz_item = cJSON_GetObjectItem(item, "size_bytes");
                    if (sz_item && cJSON_IsNumber(sz_item)) {
                        sz_mb = sz_item->valuedouble / (1024.0 * 1024.0);
                    }

                    char sd_check_path[256];
                    snprintf(sd_check_path, sizeof(sd_check_path), "%s/SmartAttendance_%s.bin", OTA_FIRMWARE_DIR, ver);
                    struct stat st;
                    bool on_sd = (stat(sd_check_path, &st) == 0 && st.st_size > 1024);

                    /* Card Item Container */
                    lv_obj_t *item_box = lv_obj_create(params->list_box);
                    lv_obj_remove_flag(item_box, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(item_box, 630, 68);
                    lv_obj_set_style_bg_color(item_box, lv_color_hex(0x0F172A), 0);
                    lv_obj_set_style_border_color(item_box, lv_color_hex(0x334155), 0);
                    lv_obj_set_style_border_width(item_box, 1, 0);
                    lv_obj_set_style_radius(item_box, 10, 0);
                    lv_obj_set_style_pad_all(item_box, 10, 0);

                    /* Left: Version + info */
                    lv_obj_t *ver_lbl = lv_label_create(item_box);
                    lv_label_set_text_fmt(ver_lbl, "%s", ver);
                    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_16, 0);
                    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0x38BDF8), 0);
                    lv_obj_align(ver_lbl, LV_ALIGN_TOP_LEFT, 10, 0);

                    lv_obj_t *sub_lbl = lv_label_create(item_box);
                    lv_label_set_text_fmt(sub_lbl, "%s  •  %.2f MB  •  %s %s", fname, sz_mb, cdate, ctime);
                    lv_obj_set_style_text_font(sub_lbl, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(sub_lbl, lv_color_hex(0x94A3B8), 0);
                    lv_obj_align(sub_lbl, LV_ALIGN_BOTTOM_LEFT, 10, 0);

                    /* Right: Status / Download Button */
                    if (on_sd) {
                        lv_obj_t *sd_badge = lv_label_create(item_box);
                        lv_label_set_text(sd_badge, LV_SYMBOL_OK " On SD Card");
                        lv_obj_set_style_text_font(sd_badge, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(sd_badge, lv_color_hex(0x10B981), 0);
                        lv_obj_align(sd_badge, LV_ALIGN_RIGHT_MID, -15, 0);
                    } else {
                        lv_obj_t *dl_btn = lv_btn_create(item_box);
                        lv_obj_set_size(dl_btn, 130, 36);
                        lv_obj_align(dl_btn, LV_ALIGN_RIGHT_MID, -10, 0);
                        lv_obj_set_style_bg_color(dl_btn, lv_color_hex(0x7C3AED), 0);
                        lv_obj_set_style_radius(dl_btn, 6, 0);

                        lv_obj_t *dl_lbl = lv_label_create(dl_btn);
                        lv_label_set_text(dl_lbl, LV_SYMBOL_DOWNLOAD " Download");
                        lv_obj_set_style_text_font(dl_lbl, &lv_font_montserrat_12, 0);
                        lv_obj_center(dl_lbl);

                        cloud_dl_param_t *dl_p = (cloud_dl_param_t *)malloc(sizeof(cloud_dl_param_t));
                        if (dl_p) {
                            snprintf(dl_p->url, sizeof(dl_p->url), "%s%s", params->endpoint, dl_url);
                            strncpy(dl_p->version, ver, sizeof(dl_p->version) - 1);
                            dl_p->modal = params->modal;

                            lv_obj_add_event_cb(dl_btn, [](lv_event_t *e) {
                                lv_event_code_t code = lv_event_get_code(e);
                                cloud_dl_param_t *p = (cloud_dl_param_t *)lv_event_get_user_data(e);
                                if (!p) return;
                                if (code == LV_EVENT_CLICKED) {
                                    char full_url[256];
                                    char ver_tag[32];
                                    strncpy(full_url, p->url, sizeof(full_url) - 1);
                                    strncpy(ver_tag, p->version, sizeof(ver_tag) - 1);
                                    lv_obj_t *m = p->modal;
                                    lv_obj_delete_async(m);
                                    start_cloud_download(full_url, ver_tag);
                                } else if (code == LV_EVENT_DELETE) {
                                    free(p);
                                }
                            }, LV_EVENT_ALL, dl_p);
                        }
                    }
                }
            }
        }
        ui_unlock();
    }

    if (root) cJSON_Delete(root);
    free(params);
    vTaskDelete(NULL);
}

static void show_cloud_catalog_modal(void) {
    char endpoint[128] = {0};
    nvs_handle_t nvs;
    if (nvs_open("telegram", NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(endpoint);
        nvs_get_str(nvs, "endpoint", endpoint, &len);
        nvs_close(nvs);
    }

    if (strlen(endpoint) == 0) {
        ui_show_notification(NOTIFY_WARNING, "No Cloud Endpoint",
                             "Please set Cloud Server URL in Settings first.", 3500);
        return;
    }

    lv_obj_t *modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_pos(modal, 0, 0);
    lv_obj_set_style_bg_color(modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal, 0, 0);
    lv_obj_set_style_pad_all(modal, 0, 0);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(modal);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 700, 440);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 20, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_DOWNLOAD " Cloud Firmware Releases Catalog");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Select any version to download directly to your SD card:");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 24);

    /* Close Button */
    lv_obj_t *close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 100, 34);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, -5);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x475569), 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE " Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
        lv_obj_t *m = (lv_obj_t *)lv_event_get_user_data(e);
        lv_obj_delete_async(m);
    }, LV_EVENT_CLICKED, modal);

    /* Scrollable List Container */
    lv_obj_t *list_box = lv_obj_create(card);
    lv_obj_set_size(list_box, 660, 345);
    lv_obj_align(list_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list_box, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_color(list_box, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(list_box, 1, 0);
    lv_obj_set_style_radius(list_box, 10, 0);
    lv_obj_set_style_pad_all(list_box, 10, 0);
    lv_obj_set_scroll_dir(list_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_box, LV_SCROLLBAR_MODE_AUTO);

    /* Loading Spinner */
    lv_obj_t *spinner = lv_spinner_create(list_box);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);

    lv_obj_t *status_lbl = lv_label_create(list_box);
    lv_label_set_text(status_lbl, "Querying releases catalog from cloud server...");
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 45);

    catalog_worker_param_t *params = (catalog_worker_param_t *)malloc(sizeof(catalog_worker_param_t));
    if (params) {
        strncpy(params->endpoint, endpoint, sizeof(params->endpoint) - 1);
        params->modal = modal;
        params->list_box = list_box;
        params->status_lbl = status_lbl;
        params->spinner = spinner;
        xTaskCreate(fetch_cloud_catalog_worker_task, "cat_fetch", 8192, params, 5, NULL);
    }
}

/* ------------------------------------------------------------------ */
/*  SD Card Scanning & List Population Task                           */
/* ------------------------------------------------------------------ */

static void scan_firmwares_worker_task(void *param) {
    firmware_info_t *list = (firmware_info_t *)malloc(sizeof(firmware_info_t) * MAX_FIRMWARE_ENTRIES);
    if (!list) {
        ESP_LOGE(TAG, "Failed to allocate memory for firmware scan list");
        vTaskDelete(NULL);
        return;
    }

    int count = ota_manager_list_sd_firmwares(list, MAX_FIRMWARE_ENTRIES);

    firmware_info_t running_info;
    ota_manager_get_running_info(&running_info);

    if (ui_lock()) {
        /* Update Active Running System Card */
        if (s_cur_version_lbl && lv_obj_is_valid(s_cur_version_lbl)) {
            lv_label_set_text_fmt(s_cur_version_lbl, "Version: %s", running_info.version);
        }
        if (s_cur_slot_lbl && lv_obj_is_valid(s_cur_slot_lbl)) {
            lv_label_set_text_fmt(s_cur_slot_lbl, "Slot: %s", running_info.slot_name);
        }
        if (s_cur_build_lbl && lv_obj_is_valid(s_cur_build_lbl)) {
            lv_label_set_text_fmt(s_cur_build_lbl, "Build: %s %s", running_info.compile_date, running_info.compile_time);
        }

        /* Populate SD Card List */
        if (s_list_container && lv_obj_is_valid(s_list_container)) {
            lv_obj_clean(s_list_container);

            if (count <= 0) {
                lv_obj_t *empty_card = lv_obj_create(s_list_container);
                lv_obj_remove_flag(empty_card, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_size(empty_card, 590, 160);
                lv_obj_set_style_bg_color(empty_card, lv_color_hex(0x1E293B), 0);
                lv_obj_set_style_border_color(empty_card, lv_color_hex(0x334155), 0);
                lv_obj_set_style_border_width(empty_card, 1, 0);
                lv_obj_set_style_radius(empty_card, 12, 0);

                lv_obj_t *empty_icon = lv_label_create(empty_card);
                lv_label_set_text(empty_icon, LV_SYMBOL_FILE);
                lv_obj_set_style_text_font(empty_icon, &lv_font_montserrat_16, 0);
                lv_obj_set_style_text_color(empty_icon, lv_color_hex(0x64748B), 0);
                lv_obj_align(empty_icon, LV_ALIGN_TOP_MID, 0, 15);

                lv_obj_t *empty_lbl = lv_label_create(empty_card);
                lv_label_set_text(empty_lbl, "No firmware binaries found on SD card.\n\n"
                                             "Directory: /sdcard/firmware/\n"
                                             "Place .bin files here or tap 'Download from Cloud'.");
                lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x94A3B8), 0);
                lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(empty_lbl, LV_ALIGN_BOTTOM_MID, 0, -15);
            } else {
                for (int i = 0; i < count; i++) {
                    const firmware_info_t *item = &list[i];

                    lv_obj_t *row = lv_obj_create(s_list_container);
                    lv_obj_set_size(row, 590, 80);
                    lv_obj_set_style_bg_color(row, lv_color_hex(0x1E293B), 0);
                    lv_obj_set_style_border_color(row, item->is_current ? lv_color_hex(0x10B981) : lv_color_hex(0x334155), 0);
                    lv_obj_set_style_border_width(row, item->is_current ? 2 : 1, 0);
                    lv_obj_set_style_radius(row, 10, 0);
                    lv_obj_set_style_pad_all(row, 12, 0);
                    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                    /* Firmware file icon & title */
                    lv_obj_t *lbl_title = lv_label_create(row);
                    lv_label_set_text_fmt(lbl_title, "%s %s", LV_SYMBOL_DRIVE, item->filename);
                    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
                    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);

                    /* Version and size metadata */
                    char meta_buf[128];
                    uint32_t mb_whole = (uint32_t)(item->size_bytes / (1024 * 1024));
                    uint32_t mb_dec = (uint32_t)(((item->size_bytes % (1024 * 1024)) * 100) / (1024 * 1024));
                    snprintf(meta_buf, sizeof(meta_buf), "Version: %s | %lu.%02lu MB | %s %s",
                             item->version, (unsigned long)mb_whole, (unsigned long)mb_dec,
                             item->compile_date, item->compile_time);
                    lv_obj_t *lbl_sub = lv_label_create(row);
                    lv_label_set_text(lbl_sub, meta_buf);
                    lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_12, 0);
                    lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x94A3B8), 0);
                    lv_obj_align(lbl_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

                    if (item->is_current) {
                        /* Active Badge */
                        lv_obj_t *badge = lv_obj_create(row);
                        lv_obj_set_size(badge, 120, 32);
                        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
                        lv_obj_set_style_bg_color(badge, lv_color_hex(0x064E3B), 0);
                        lv_obj_set_style_border_color(badge, lv_color_hex(0x10B981), 0);
                        lv_obj_set_style_border_width(badge, 1, 0);
                        lv_obj_set_style_radius(badge, 6, 0);
                        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

                        lv_obj_t *badge_lbl = lv_label_create(badge);
                        lv_label_set_text(badge_lbl, LV_SYMBOL_OK " CURRENT");
                        lv_obj_set_style_text_color(badge_lbl, lv_color_hex(0x34D399), 0);
                        lv_obj_set_style_text_font(badge_lbl, &lv_font_montserrat_12, 0);
                        lv_obj_center(badge_lbl);
                    } else {
                        /* Action Button (Upgrade or Rollback) */
                        lv_obj_t *action_btn = lv_btn_create(row);
                        lv_obj_set_size(action_btn, 130, 36);
                        lv_obj_align(action_btn, LV_ALIGN_RIGHT_MID, 0, 0);
                        lv_obj_set_style_radius(action_btn, 6, 0);

                        bool is_upgrade = (strcmp(item->version, running_info.version) > 0);
                        if (is_upgrade) {
                            lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x2563EB), 0); /* Blue for upgrade */
                        } else {
                            lv_obj_set_style_bg_color(action_btn, lv_color_hex(0xD97706), 0); /* Amber for rollback */
                        }

                        lv_obj_t *btn_lbl = lv_label_create(action_btn);
                        lv_label_set_text(btn_lbl, is_upgrade ? LV_SYMBOL_UP " Upgrade" : LV_SYMBOL_REFRESH " Rollback");
                        lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_12, 0);
                        lv_obj_center(btn_lbl);

                        /* Store copy of firmware_info_t on heap for click handler */
                        firmware_info_t *heap_info = (firmware_info_t *)malloc(sizeof(firmware_info_t));
                        if (heap_info) {
                            memcpy(heap_info, item, sizeof(firmware_info_t));
                            lv_obj_add_event_cb(action_btn, [](lv_event_t *e) {
                                firmware_info_t *info = (firmware_info_t *)lv_event_get_user_data(e);
                                if (info) {
                                    show_confirm_modal(info);
                                }
                            }, LV_EVENT_CLICKED, heap_info);

                            /* Auto-free heap_info when row object is deleted */
                            lv_obj_add_event_cb(action_btn, [](lv_event_t *e) {
                                firmware_info_t *info = (firmware_info_t *)lv_event_get_user_data(e);
                                if (info) free(info);
                            }, LV_EVENT_DELETE, heap_info);
                        }
                    }
                }
            }
        }
        ui_unlock();
    }
    free(list);
    vTaskDelete(NULL);
}

static void refresh_sd_firmwares_async(void) {
    if (s_list_container && lv_obj_is_valid(s_list_container)) {
        lv_obj_clean(s_list_container);
        lv_obj_t *loading = lv_label_create(s_list_container);
        lv_label_set_text(loading, "Scanning SD card /sdcard/firmware/ ...");
        lv_obj_set_style_text_color(loading, lv_color_hex(0x94A3B8), 0);
        lv_obj_align(loading, LV_ALIGN_CENTER, 0, 40);
    }
    xTaskCreate(scan_firmwares_worker_task, "fw_scan", 8192, NULL, 5, NULL);
}

/* ------------------------------------------------------------------ */
/*  Screen Creation                                                   */
/* ------------------------------------------------------------------ */

static void create_firmware_screen(void) {
    s_fw_screen = lv_obj_create(NULL);
    ui_add_double_tap_to_screen(s_fw_screen);
    lv_obj_set_style_bg_color(s_fw_screen, lv_color_hex(0x0F172A), 0); /* Dark slate background */
    lv_scr_load(s_fw_screen);

    /* 1. Top Header Bar */
    lv_obj_t *header = lv_obj_create(s_fw_screen);
    lv_obj_set_size(header, DISPLAY_WIDTH, 50);
    lv_obj_set_pos(header, 0, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(header, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    /* Back Button */
    lv_obj_t *back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 140, 36);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(back_btn, 8, 0);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " Settings");
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        ui_close_firmware_mgr_screen();
    }, LV_EVENT_CLICKED, NULL);

    /* Header Title */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Firmware & Rollback Management");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* Rescan SD Button */
    lv_obj_t *rescan_btn = lv_btn_create(header);
    lv_obj_set_size(rescan_btn, 130, 36);
    lv_obj_align(rescan_btn, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_set_style_bg_color(rescan_btn, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(rescan_btn, 8, 0);
    lv_obj_t *rescan_lbl = lv_label_create(rescan_btn);
    lv_label_set_text(rescan_lbl, LV_SYMBOL_REFRESH " Rescan SD");
    lv_obj_center(rescan_lbl);
    lv_obj_add_event_cb(rescan_btn, [](lv_event_t *e) {
        refresh_sd_firmwares_async();
    }, LV_EVENT_CLICKED, NULL);

    /* 2. Main Content Area (Two Columns) */
    lv_obj_t *content = lv_obj_create(s_fw_screen);
    lv_obj_set_size(content, DISPLAY_WIDTH - 30, 448);
    lv_obj_set_pos(content, 15, 84);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* === Left Column (System Status & Cloud Update) === */
    lv_obj_t *left_col = lv_obj_create(content);
    lv_obj_remove_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(left_col, 340, 444);
    lv_obj_align(left_col, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(left_col, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(left_col, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(left_col, 1, 0);
    lv_obj_set_style_radius(left_col, 14, 0);
    lv_obj_set_style_pad_all(left_col, 16, 0);

    /* Card Title */
    lv_obj_t *sys_title = lv_label_create(left_col);
    lv_label_set_text(sys_title, LV_SYMBOL_SETTINGS " CURRENT FIRMWARE");
    lv_obj_set_style_text_font(sys_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sys_title, lv_color_hex(0x38BDF8), 0); /* Light cyan */
    lv_obj_align(sys_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_cur_version_lbl = lv_label_create(left_col);
    lv_label_set_text(s_cur_version_lbl, "Version: Reading...");
    lv_obj_set_style_text_font(s_cur_version_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cur_version_lbl, lv_color_white(), 0);
    lv_obj_align(s_cur_version_lbl, LV_ALIGN_TOP_LEFT, 0, 26);

    s_cur_slot_lbl = lv_label_create(left_col);
    lv_label_set_text(s_cur_slot_lbl, "Slot: Reading...");
    lv_obj_set_style_text_font(s_cur_slot_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_cur_slot_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_cur_slot_lbl, LV_ALIGN_TOP_LEFT, 0, 50);

    s_cur_build_lbl = lv_label_create(left_col);
    lv_label_set_text(s_cur_build_lbl, "Build: Reading...");
    lv_obj_set_style_text_font(s_cur_build_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_cur_build_lbl, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_cur_build_lbl, LV_ALIGN_TOP_LEFT, 0, 70);

    /* Rollback Badge */
    lv_obj_t *sec_badge = lv_obj_create(left_col);
    lv_obj_remove_flag(sec_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sec_badge, 305, 46);
    lv_obj_align(sec_badge, LV_ALIGN_TOP_LEFT, 0, 98);
    lv_obj_set_style_bg_color(sec_badge, lv_color_hex(0x064E3B), 0);
    lv_obj_set_style_border_color(sec_badge, lv_color_hex(0x10B981), 0);
    lv_obj_set_style_border_width(sec_badge, 1, 0);
    lv_obj_set_style_radius(sec_badge, 8, 0);

    lv_obj_t *badge_icon = lv_label_create(sec_badge);
    lv_label_set_text(badge_icon, LV_SYMBOL_OK " Bootloader Self-Healing Active");
    lv_obj_set_style_text_color(badge_icon, lv_color_hex(0x34D399), 0);
    lv_obj_set_style_text_font(badge_icon, &lv_font_montserrat_12, 0);
    lv_obj_align(badge_icon, LV_ALIGN_TOP_LEFT, 5, 0);

    lv_obj_t *badge_sub = lv_label_create(sec_badge);
    lv_label_set_text(badge_sub, "Automatic rollback protects against boot failures");
    lv_obj_set_width(badge_sub, 290);
    lv_label_set_long_mode(badge_sub, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(badge_sub, lv_color_hex(0xA7F3D0), 0);
    lv_obj_set_style_text_font(badge_sub, &lv_font_montserrat_12, 0);
    lv_obj_align(badge_sub, LV_ALIGN_BOTTOM_LEFT, 5, 0);

    /* Cloud OTA Section */
    lv_obj_t *cloud_divider = lv_obj_create(left_col);
    lv_obj_remove_flag(cloud_divider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cloud_divider, 305, 1);
    lv_obj_align(cloud_divider, LV_ALIGN_TOP_LEFT, 0, 158);
    lv_obj_set_style_bg_color(cloud_divider, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(cloud_divider, 0, 0);

    lv_obj_t *cloud_title = lv_label_create(left_col);
    lv_label_set_text(cloud_title, LV_SYMBOL_DOWNLOAD " CLOUD OTA BUFFER");
    lv_obj_set_style_text_font(cloud_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cloud_title, lv_color_hex(0xA855F7), 0); /* Purple */
    lv_obj_align(cloud_title, LV_ALIGN_TOP_LEFT, 0, 172);

    lv_obj_t *cloud_desc = lv_label_create(left_col);
    lv_label_set_text(cloud_desc, "OTA updates download to SD card first.\n"
                                  "Eliminates bricking from network drops.");
    lv_obj_set_style_text_color(cloud_desc, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_text_font(cloud_desc, &lv_font_montserrat_12, 0);
    lv_obj_align(cloud_desc, LV_ALIGN_TOP_LEFT, 0, 200);

    /* Button 1: Download Latest Release */
    lv_obj_t *cloud_btn = lv_btn_create(left_col);
    lv_obj_set_size(cloud_btn, 305, 38);
    lv_obj_align(cloud_btn, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_set_style_bg_color(cloud_btn, lv_color_hex(0x7C3AED), 0); /* Vibrant Violet */
    lv_obj_set_style_radius(cloud_btn, 8, 0);
    lv_obj_t *cloud_lbl = lv_label_create(cloud_btn);
    lv_label_set_text(cloud_lbl, LV_SYMBOL_DOWNLOAD " Download Latest Release");
    lv_obj_set_style_text_font(cloud_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(cloud_lbl);

    lv_obj_add_event_cb(cloud_btn, [](lv_event_t *e) {
        /* Read Cloud Endpoint from NVS */
        char endpoint[128] = {0};
        nvs_handle_t nvs;
        if (nvs_open("telegram", NVS_READONLY, &nvs) == ESP_OK) {
            size_t len = sizeof(endpoint);
            nvs_get_str(nvs, "endpoint", endpoint, &len);
            nvs_close(nvs);
        }

        if (strlen(endpoint) == 0) {
            /* Prompt user to configure endpoint in Settings */
            ui_show_notification(NOTIFY_WARNING, "No Cloud Endpoint",
                                 "Please set Cloud Server URL in Settings first.", 3500);
        } else {
            char ota_url[256];
            snprintf(ota_url, sizeof(ota_url), "%s/firmware/latest.bin", endpoint);
            start_cloud_download(ota_url, "latest");
        }
    }, LV_EVENT_CLICKED, NULL);

    /* Button 2: Browse All Cloud Releases */
    lv_obj_t *catalog_btn = lv_btn_create(left_col);
    lv_obj_set_size(catalog_btn, 305, 38);
    lv_obj_align(catalog_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(catalog_btn, lv_color_hex(0x2563EB), 0); /* Blue */
    lv_obj_set_style_radius(catalog_btn, 8, 0);
    lv_obj_t *catalog_lbl = lv_label_create(catalog_btn);
    lv_label_set_text(catalog_lbl, LV_SYMBOL_LIST " Browse Cloud Releases");
    lv_obj_set_style_text_font(catalog_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(catalog_lbl);

    lv_obj_add_event_cb(catalog_btn, [](lv_event_t *e) {
        show_cloud_catalog_modal();
    }, LV_EVENT_CLICKED, NULL);

    /* === Right Column (SD Card Archived Firmwares) === */
    lv_obj_t *right_col = lv_obj_create(content);
    lv_obj_set_size(right_col, 630, 444);
    lv_obj_align(right_col, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(right_col, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_color(right_col, lv_color_hex(0x334155), 0);
    lv_obj_set_style_border_width(right_col, 1, 0);
    lv_obj_set_style_radius(right_col, 14, 0);
    lv_obj_set_style_pad_all(right_col, 15, 0);
    lv_obj_remove_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);

    /* Right Header */
    lv_obj_t *list_title = lv_label_create(right_col);
    lv_label_set_text(list_title, LV_SYMBOL_SAVE " SD CARD ARCHIVED FIRMWARES (/sdcard/firmware)");
    lv_obj_set_style_text_font(list_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(list_title, lv_color_hex(0xF59E0B), 0); /* Amber */
    lv_obj_align(list_title, LV_ALIGN_TOP_LEFT, 5, 0);

    /* Scrollable Container for items */
    s_list_container = lv_obj_create(right_col);
    lv_obj_set_size(s_list_container, 600, 390);
    lv_obj_align(s_list_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_list_container, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_border_width(s_list_container, 0, 0);
    lv_obj_set_style_pad_all(s_list_container, 5, 0);
    lv_obj_set_flex_flow(s_list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(s_list_container, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_list_container, LV_DIR_VER);

    /* Initial Async Scan */
    refresh_sd_firmwares_async();
}

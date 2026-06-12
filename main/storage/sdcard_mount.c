#include "storage/sdcard_mount.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "boards/elecrow_p4_board.h"
#include <string.h>
#include <errno.h>

static const char *TAG = "SDCARD";
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_mount(void) {
    if (s_card != NULL) return ESP_OK;

    ESP_LOGI(TAG, "Initializing SD card");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  /* Force 1-line mode for ESP32-P4 HMI Board */
    slot_config.clk = SDMMC_CLK;
    slot_config.cmd = SDMMC_CMD;
    slot_config.d0 = SDMMC_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &s_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (ESP_ERR: %d).", ret);
        }
        return ret;
    }

    /* Print card info */
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card mounted successfully.");

    /* Diagnostic File Write Test */
    ESP_LOGI(TAG, "Running diagnostic write/read test on SD card...");
    FILE *f_test = fopen("/sdcard/diag_test.txt", "w");
    if (f_test == NULL) {
        ESP_LOGE(TAG, "DIAG: Failed to open file for writing! errno = %d (%s)", errno, strerror(errno));
    } else {
        int w_bytes = fprintf(f_test, "SmartAttendance SD Card Diagnostic Write Test");
        fclose(f_test);
        if (w_bytes < 0) {
            ESP_LOGE(TAG, "DIAG: Failed to write bytes to file!");
        } else {
            ESP_LOGI(TAG, "DIAG: Wrote %d bytes successfully to diag_test.txt.", w_bytes);
            f_test = fopen("/sdcard/diag_test.txt", "r");
            if (f_test == NULL) {
                ESP_LOGE(TAG, "DIAG: Failed to open file for reading! errno = %d (%s)", errno, strerror(errno));
            } else {
                char read_buf[64] = {0};
                if (fgets(read_buf, sizeof(read_buf), f_test)) {
                    ESP_LOGI(TAG, "DIAG: Read back content: '%s'", read_buf);
                } else {
                    ESP_LOGE(TAG, "DIAG: Failed to read from file!");
                }
                fclose(f_test);
                remove("/sdcard/diag_test.txt");
            }
        }
    }

    return ESP_OK;
}

void sdcard_unmount(void) {
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
        ESP_LOGI(TAG, "Card unmounted");
        s_card = NULL;
    }
}

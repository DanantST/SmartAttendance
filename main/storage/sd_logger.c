#include "storage/sd_logger.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "SD_LOG";

#define LOG_QUEUE_SIZE        64
#define LOG_ENTRY_MAX_LEN     256
#define MAX_LOG_FILE_BYTES    (500 * 1024)   /* 500 KB per log file */

typedef struct {
    char text[LOG_ENTRY_MAX_LEN];
} log_msg_t;

static QueueHandle_t s_log_queue = NULL;
static SemaphoreHandle_t s_log_mutex = NULL;
static bool s_logger_active = false;

static void rotate_logs_if_needed(void) {
    struct stat st;
    if (stat("/sdcard/logs/system.log", &st) == 0) {
        if (st.st_size >= MAX_LOG_FILE_BYTES) {
            remove("/sdcard/logs/system.log.old");
            rename("/sdcard/logs/system.log", "/sdcard/logs/system.log.old");
        }
    }
}

static void sd_logger_task(void *pvParameters) {
    (void)pvParameters;
    log_msg_t msg;

    /* Ensure /sdcard/logs directory exists */
    mkdir("/sdcard/logs", 0777);

    while (1) {
        if (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                rotate_logs_if_needed();

                FILE *f = fopen("/sdcard/logs/system.log", "a");
                if (f) {
                    fputs(msg.text, f);
                    fputc('\n', f);
                    fclose(f);
                }
                xSemaphoreGive(s_log_mutex);
            }
        }
    }
}

void sd_logger_write(const char *tag, const char *fmt, ...) {
    if (!s_logger_active || !s_log_queue) return;

    log_msg_t msg;
    int offset = 0;

    /* Add simple timestamp prefix */
    int64_t now_ms = esp_timer_get_time() / 1000LL;
    offset = snprintf(msg.text, sizeof(msg.text), "[%lld ms][%s] ", (long long)now_ms, tag ? tag : "APP");
    if (offset < 0 || offset >= sizeof(msg.text)) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(msg.text + offset, sizeof(msg.text) - offset, fmt, args);
    va_end(args);

    /* Send non-blocking to avoid stalling caller */
    xQueueSend(s_log_queue, &msg, 0);
}

esp_err_t sd_logger_init(void) {
    if (s_logger_active) return ESP_OK;

    s_log_mutex = xSemaphoreCreateMutex();
    if (!s_log_mutex) return ESP_ERR_NO_MEM;

    s_log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(log_msg_t));
    if (!s_log_queue) return ESP_ERR_NO_MEM;

    /* Start background SD file writer task */
    BaseType_t tr = xTaskCreate(sd_logger_task, "sd_logger", 4096, NULL, 2, NULL);
    if (tr != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sd_logger_task");
        return ESP_FAIL;
    }

    s_logger_active = true;

    ESP_LOGI(TAG, "SD Card Usage Logger initialized (/sdcard/logs/system.log)");
    sd_logger_write("SYSTEM", "SD Card Logger initialized");
    return ESP_OK;
}

int sd_logger_dump(void) {
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        printf("--- SD LOGGER: File busy ---\n");
        return -1;
    }

    int lines = 0;
    printf("\n==================== SD CARD LOG DUMP START ====================\n");

    /* Check rotated log first if it exists */
    FILE *f_old = fopen("/sdcard/logs/system.log.old", "r");
    if (f_old) {
        printf("--- Archived Log (/sdcard/logs/system.log.old) ---\n");
        char line[256];
        while (fgets(line, sizeof(line), f_old)) {
            fputs(line, stdout);
            lines++;
        }
        fclose(f_old);
    }

    /* Print current log file */
    FILE *f_curr = fopen("/sdcard/logs/system.log", "r");
    if (f_curr) {
        printf("--- Active Log (/sdcard/logs/system.log) ---\n");
        char line[256];
        while (fgets(line, sizeof(line), f_curr)) {
            fputs(line, stdout);
            lines++;
        }
        fclose(f_curr);
    } else {
        printf("No active log file found at /sdcard/logs/system.log\n");
    }

    printf("==================== SD CARD LOG DUMP END (%d lines) ====================\n\n", lines);

    xSemaphoreGive(s_log_mutex);
    return lines;
}

void sd_logger_check_serial_trigger(void) {
    /* Poll stdin non-blockingly for 'log_dump' trigger */
    int c = getchar();
    if (c == 'D' || c == 'd') {
        sd_logger_dump();
    }
}

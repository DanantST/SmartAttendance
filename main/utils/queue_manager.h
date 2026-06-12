/**
 * @file queue_manager.h
 * @brief Queue definitions and helpers
 */

#ifndef QUEUE_MANAGER_H
#define QUEUE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* External queue handles */
extern QueueHandle_t g_camera_frame_queue;
extern QueueHandle_t g_system_event_queue;
extern QueueHandle_t g_db_request_queue;


#ifdef __cplusplus
}
#endif

#endif /* QUEUE_MANAGER_H */
/**
 * @file queue_manager.c
 * @brief Queue definitions (global handles)
 */

#include "queue_manager.h"

QueueHandle_t g_camera_frame_queue = NULL;
QueueHandle_t g_system_event_queue = NULL;
QueueHandle_t g_db_request_queue = NULL;
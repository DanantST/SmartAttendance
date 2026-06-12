#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the MIPI-DSI display panel (EK79007 bridge, 1024x600).
 *        Powers MIPI PHY via LDO3, creates DSI bus, DBI IO, and panel.
 * @param[out] panel_handle  Returned LCD panel handle
 */
esp_err_t display_init(esp_lcd_panel_handle_t *panel_handle);

/**
 * @brief Initialize LVGL, wire it to the panel, start tick timer and render task.
 *        Must be called after display_init().
 * @param[in] panel_handle  Panel handle returned from display_init()
 */
esp_err_t display_lvgl_init(esp_lcd_panel_handle_t panel_handle);

/**
 * @brief LVGL flush callback — passes a rendered buffer to the DPI panel.
 *        flush_ready is signalled via DPI on_color_trans_done event callback.
 */
void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

#include "stdbool.h"

/**
 * @brief Acquire / release the LVGL mutex for thread-safe API access.
 * ui_lock returns true if the lock was acquired within 2 seconds.
 */
bool ui_lock(void);
void ui_unlock(void);

#ifdef __cplusplus
}
#endif

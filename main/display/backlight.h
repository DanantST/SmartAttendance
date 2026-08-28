#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t backlight_init(void);
void      backlight_set(uint8_t percent);

/**
 * @brief Mark the backlight LEDC channel as already initialised.
 *
 * Call this after board_backlight_init() succeeds so that a subsequent
 * backlight_init() call from the UI layer (AR-3) becomes a no-op and
 * does not re-configure the same LEDC timer/channel on GPIO31.
 */
void backlight_mark_initialized(void);

#ifdef __cplusplus
}
#endif

#pragma once
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/i2c_master.h"

esp_err_t touch_init(void);
void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data);
i2c_master_bus_handle_t touch_get_i2c_bus(void);

#ifdef __cplusplus
}
#endif

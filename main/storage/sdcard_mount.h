#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sdcard_mount(void);
void sdcard_unmount(void);

#ifdef __cplusplus
}
#endif

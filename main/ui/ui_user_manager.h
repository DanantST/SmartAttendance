/**
 * @file ui_user_manager.h
 * @brief User management interface
 */

#ifndef UI_USER_MANAGER_H
#define UI_USER_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Show the user management screen
 */
void ui_show_user_manager(void);

/**
 * @brief Close the user management screen
 */
void ui_close_user_manager(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_USER_MANAGER_H */

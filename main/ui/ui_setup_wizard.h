/**
 * @file ui_setup_wizard.h
 * @brief Setup wizard interface
 */

#ifndef UI_SETUP_WIZARD_H
#define UI_SETUP_WIZARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Show the setup wizard
 */
void ui_show_setup_wizard(void);

/**
 * @brief Close the setup wizard
 */
void ui_close_setup_wizard(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SETUP_WIZARD_H */

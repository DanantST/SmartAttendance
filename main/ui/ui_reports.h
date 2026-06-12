/**
 * @file ui_reports.h
 * @brief Reports screen to view attendance logs and generate CSV
 */

#ifndef UI_REPORTS_H
#define UI_REPORTS_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lvgl.h"

/**
 * @brief Show reports screen
 */
void ui_show_reports_screen(void);

/**
 * @brief Close reports screen and return to main
 */
void ui_close_reports_screen(void);

/**
 * @brief Populate course list in reports screen
 * @param courses array of course names
 * @param count number of courses
 */
void ui_reports_populate_courses(const char** courses, int count);

/**
 * @brief Show attendance report data (CSV or formatted)
 * @param data report data string
 * @param len data length
 */
void ui_reports_show_data(const char* data, size_t len);


#ifdef __cplusplus
}
#endif

#endif /* UI_REPORTS_H */
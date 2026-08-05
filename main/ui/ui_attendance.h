#ifndef UI_ATTENDANCE_H
#define UI_ATTENDANCE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_show_attendance_screen(void);
void ui_close_attendance_screen(void);
void ui_update_camera_frame(uint8_t* img, int width, int height);
void ui_show_recognition_result(const char* name, float confidence);
void ui_update_detection_bounding_box(int x, int y, int w, int h, bool detected);
void ui_attendance_increment_count(void);
void ui_attendance_set_count(int count);

/**
 * @brief Show a full-width attendance feedback message below the camera preview.
 * @param msg         Text to display
 * @param is_duplicate true = orange "already recorded" style, false = green "recorded" style
 */
void ui_show_attendance_feedback(const char* msg, int type);

#ifdef __cplusplus
}
#endif

#endif /* UI_ATTENDANCE_H */

#pragma once

#include <stdbool.h>

#include "status_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATUS_UI_PHYSICAL_GESTURE_KEY1_SHORT = 0,
    STATUS_UI_PHYSICAL_GESTURE_KEY1_LONG,
    STATUS_UI_PHYSICAL_GESTURE_KEY2_SINGLE,
    STATUS_UI_PHYSICAL_GESTURE_KEY2_DOUBLE,
    STATUS_UI_PHYSICAL_GESTURE_KEY2_LONG,
} status_ui_physical_gesture_t;

bool status_ui_input_from_physical_gesture(status_ui_physical_gesture_t gesture,
                                           status_ui_input_t *out_input);
/* Used only while a device approval prompt owns focus. */
bool status_ui_web_auth_decision(status_ui_input_t input, bool *approve);

#ifdef __cplusplus
}
#endif

#include "status_ui_input_map.h"

bool status_ui_web_auth_decision(status_ui_input_t input, bool *approve)
{
    if (approve == NULL || input < STATUS_UI_INPUT_SELECT || input > STATUS_UI_INPUT_BACK) return false;
    *approve = input == STATUS_UI_INPUT_SELECT;
    return true;
}

bool status_ui_input_from_physical_gesture(status_ui_physical_gesture_t gesture,
                                           status_ui_input_t *out_input)
{
    if (out_input == NULL) {
        return false;
    }

    switch (gesture) {
    case STATUS_UI_PHYSICAL_GESTURE_KEY1_SHORT:
        *out_input = STATUS_UI_INPUT_SELECT;
        return true;
    case STATUS_UI_PHYSICAL_GESTURE_KEY2_SINGLE:
        *out_input = STATUS_UI_INPUT_NEXT;
        return true;
    case STATUS_UI_PHYSICAL_GESTURE_KEY2_DOUBLE:
        *out_input = STATUS_UI_INPUT_PREV;
        return true;
    case STATUS_UI_PHYSICAL_GESTURE_KEY2_LONG:
        *out_input = STATUS_UI_INPUT_BACK;
        return true;
    case STATUS_UI_PHYSICAL_GESTURE_KEY1_LONG:
    default:
        return false;
    }
}

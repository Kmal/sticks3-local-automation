#include "status_ui_input_map.h"

#include <stdio.h>
#include <stdlib.h>

#define ASSERT_TRUE(value) do { \
    if (!(value)) { \
        fprintf(stderr, "%s:%d assertion failed: %s\n", __FILE__, __LINE__, #value); \
        exit(1); \
    } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        fprintf(stderr, "%s:%d expected %d got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        exit(1); \
    } \
} while (0)

static void assert_maps(status_ui_physical_gesture_t gesture, status_ui_input_t expected)
{
    status_ui_input_t actual = STATUS_UI_INPUT_BACK;
    ASSERT_TRUE(status_ui_input_from_physical_gesture(gesture, &actual));
    ASSERT_EQ(expected, actual);
}

static void test_physical_gestures_map_to_global_inputs(void)
{
    assert_maps(STATUS_UI_PHYSICAL_GESTURE_KEY1_SHORT, STATUS_UI_INPUT_SELECT);
    assert_maps(STATUS_UI_PHYSICAL_GESTURE_KEY2_SINGLE, STATUS_UI_INPUT_NEXT);
    assert_maps(STATUS_UI_PHYSICAL_GESTURE_KEY2_DOUBLE, STATUS_UI_INPUT_PREV);
    assert_maps(STATUS_UI_PHYSICAL_GESTURE_KEY2_LONG, STATUS_UI_INPUT_BACK);
}

static void test_key1_long_stays_out_of_global_input_mapping(void)
{
    status_ui_input_t input = STATUS_UI_INPUT_SELECT;
    ASSERT_TRUE(!status_ui_input_from_physical_gesture(STATUS_UI_PHYSICAL_GESTURE_KEY1_LONG, &input));
    ASSERT_EQ(STATUS_UI_INPUT_SELECT, input);
}

int main(void)
{
    test_physical_gestures_map_to_global_inputs();
    test_key1_long_stays_out_of_global_input_mapping();
    bool approve = false;
    ASSERT_TRUE(status_ui_web_auth_decision(STATUS_UI_INPUT_SELECT, &approve));
    ASSERT_TRUE(approve);
    for (int input = STATUS_UI_INPUT_NEXT; input <= STATUS_UI_INPUT_BACK; ++input) {
        approve = true;
        ASSERT_TRUE(status_ui_web_auth_decision((status_ui_input_t)input, &approve));
        ASSERT_TRUE(!approve);
    }
    ASSERT_TRUE(!status_ui_web_auth_decision((status_ui_input_t)99, &approve));
    ASSERT_TRUE(!status_ui_web_auth_decision(STATUS_UI_INPUT_SELECT, NULL));
    puts("status_ui_input_map tests passed");
    return 0;
}

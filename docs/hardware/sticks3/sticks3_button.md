# StickS3 button hardware notes

Only GPIO11 (`KEY1`) and GPIO12 (`KEY2`) are treated as StickS3 user keys. GPIO35, GPIO37, and GPIO39 are not used as status buttons. GPIO39 is documented as LCD MOSI and must not be configured as a button.

The current firmware polls the two documented keys, logs their presses, displays per-key press counters on the optional LCD dashboard, and emits normalized automation facts. In the status UI, physical button gestures are first mapped to the global input contract (`STATUS_UI_INPUT_SELECT`, `STATUS_UI_INPUT_NEXT`, `STATUS_UI_INPUT_PREV`, and `STATUS_UI_INPUT_BACK`) before the focused UI layer routes them to the keyboard, Wi-Fi scan list, active menu, or idle callback path. In the menu, KEY1 short selects, KEY2 short moves next, KEY2 double moves previous, KEY2 long goes back, and KEY1 long from the idle status view opens `Main`. Product actions that require more than the two documented keys remain transport/UX decisions.

KEY2 double-click allows up to 500 ms between the two debounced releases. A single click is dispatched after this window expires. The long-press threshold is 600 ms and debounce is 50 ms.

## Sources

- M5Stack StickS3 product page pin map for KEY1=GPIO11 and KEY2=GPIO12: https://docs.m5stack.com/en/core/StickS3
- M5Stack StickS3 button Arduino API page for the official button feature: https://docs.m5stack.com/en/arduino/m5sticks3/button
- Firmware board constants for `BOARD_BUTTON_KEY1_GPIO`, `BOARD_BUTTON_KEY2_GPIO`, and LCD MOSI GPIO39: ../../../src/board/board_sticks3.h
- Firmware physical gesture to global input mapping: ../../../src/ui/status_ui_input_map.c
- Firmware UI input handling for KEY1/KEY2 navigation behavior: ../../../src/ui/status_ui.c
- Status UI input refactor review and checklist: ../../status_ui_input_review.md

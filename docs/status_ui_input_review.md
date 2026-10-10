# Status UI input refactor review

This review records the follow-up audit of the status UI button/input refactor
against the StickS3 hardware references, the original focused-input
architecture plan, and the repository documentation/comment update policy.

## Review result

No additional StickS3 hardware-spec mismatch was found in the touched status UI
input code. The refactor keeps the board facts in `src/board/board_sticks3.h`
unchanged and confines button-gesture changes to software routing. The original
status UI input architecture requirements are implemented, except for optional
module/gesture extraction items that the original plan explicitly allowed to be
left for a follow-up. Documentation and comments were updated to describe the new
input flow and the UI-state-blind physical gesture boundary.

The original input-refactor review lacked an ESP-IDF environment. The current
2026-10-10 follow-up builds with ESP-IDF v6.1 for ESP32-S3 successfully; physical
StickS3 button/display validation remains pending.

## Official/reference material checked

The review used the repository hardware notes and the official/source references
below. Board facts were cross-checked against `src/board/board_sticks3.h` and the
machine-readable board manifest where applicable.

- M5Stack StickS3 documentation and pin map: https://docs.m5stack.com/en/core/StickS3
- M5Stack StickS3 Arduino programming documentation: https://docs.m5stack.com/en/arduino/m5sticks3/program
- M5Stack StickS3 Battery Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/battery
- M5Stack StickS3 Button Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/button
- M5Stack StickS3 Display Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/display
- M5Stack StickS3 IMU Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/imu
- M5Stack StickS3 IR NEC Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec
- M5Stack StickS3 Microphone Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/mic
- M5Stack StickS3 Speaker Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/speaker
- M5Stack StickS3 Wakeup Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/wakeup
- M5Stack StickS3 M5PM1 Arduino documentation: https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- M5Stack M5PM1 source repository: https://github.com/m5stack/M5PM1
- M5Stack M5Unified source repository: https://github.com/m5stack/M5Unified
- M5Stack M5GFX source repository: https://github.com/m5stack/M5GFX
- M5Stack M5GFX StickS3 initialization source: https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp
- StickS3 schematic PDF: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf
- ES8311 datasheet: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf
- BMI270 datasheet: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/BMI270.PDF
- ESP32-S3 technical reference manual: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/477/esp32-s3_technical_reference_manual_cn.pdf
- Espressif Bluetooth architecture documentation: https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/bt-architecture/overview.html
- In-repository hardware entry point: `docs/hardware/sticks3/sticks3.md`
- In-repository button notes: `docs/hardware/sticks3/sticks3_button.md`
- Firmware board constants: `src/board/board_sticks3.h`

## Hardware/spec conformance check

| Area | Reviewed implementation | Result |
| --- | --- | --- |
| User buttons | `BOARD_BUTTON_KEY1_GPIO` remains GPIO11, `BOARD_BUTTON_KEY2_GPIO` remains GPIO12, and `BOARD_BUTTON_ACTIVE_LEVEL` remains active-low. | Matches StickS3 button/pin-map docs. |
| Non-button pins | GPIO35, GPIO37, and GPIO39 are still not configured as status buttons; GPIO39 remains LCD MOSI. | Matches repository hardware notes and official pin map. |
| Display | ST7789P3 135x240 pin constants remain MOSI=GPIO39, SCLK=GPIO40, RS/DC=GPIO45, CS=GPIO41, RST=GPIO21, BL=GPIO38. | Matches StickS3 display/pin-map docs. |
| I2C devices | Shared bus and device-address constants for BMI270 and M5PM1 were not changed by this refactor. | No new mismatch introduced. |
| Audio/codec pins | ES8311/I2S constants were not changed by this refactor. | No new mismatch introduced. |
| Bluetooth capability | No Bluetooth stack behavior was changed; ESP32-S3 Classic Bluetooth remains unsupported and custom BLE GATT remains the intended path. | No new mismatch introduced. |

## Original plan / acceptance checklist

| Requirement | Status | Evidence / notes |
| --- | --- | --- |
| Keep `status_ui_input_t` as `SELECT`, `NEXT`, `PREV`, `BACK`. | Done | `src/ui/status_ui.h`. |
| Rename menu-specific queue helper to a global queue helper. | Done | `status_ui_queue_global_input()` in `src/ui/status_ui.c`. |
| Physical KEY1 short, KEY2 single, KEY2 double, KEY2 long dispatch only global inputs. | Done | Dispatchers call `status_ui_input_from_physical_gesture()` and then queue the resulting `STATUS_UI_INPUT_*`. |
| Physical dispatchers do not inspect keyboard/menu/scan foreground state. | Done | The short/single/double/long dispatchers no longer call keyboard/menu handlers or inspect UI focus; the KEY1-long exception delegates to a UI-owned helper instead of reading `s_ui.menu_active` in GPIO polling. |
| Preserve KEY1 long menu-open behavior. | Done | KEY1 long remains the explicit exception, but GPIO polling now delegates the UI-state check/open to `status_ui_handle_key1_long()` so the physical polling path stays UI-state blind. |
| Move idle KEY1/KEY2 callback behavior into idle global input handling. | Done | Idle consumer sets deferred callback effects for `SELECT` and `NEXT`. |
| Pending browser authorization takes focus before keyboard, scan, menu, or idle input. | Done | The request ID actually rendered by the LCD binds the device decision. KEY1 short approves; KEY2 single/double/long rejects; KEY1 long is ignored. Approval effects call the owner outside the UI spinlock. |
| Collect side effects in an input-effects struct and apply them after routing. | Done | `status_ui_input_effects_t` plus `status_ui_apply_input_effects()`. |
| Make `status_ui_handle_input()` the global entry point. | Done | It dispatches focused input and then applies collected effects. |
| Dispatch focused input by keyboard, scan, menu, idle priority. | Done | `status_ui_dispatch_focused_input()`. |
| Keyboard/input overlay consumes global inputs explicitly. | Done | `status_ui_keyboard_consume_input()` handles `SELECT`, `NEXT`, `PREV`, `BACK`. |
| Menu-edit keyboard handling maps global inputs directly. | Done | `status_ui_keyboard_handle_menu_global_input()` maps global inputs to keyboard operations/cancel. |
| Blocking read-line keyboard queue accepts global inputs. | Done | `s_virtual_keyboard_queue` stores `status_ui_input_t`. |
| Remove public keyboard-specific Back/navigation event contract. | Done | Public `STATUS_UI_KEYBOARD_EVENT_*` enum is removed from `ui_keyboard.h`. |
| Add explicit keyboard cancel metadata. | Done | `ui_keyboard_menu_edit_t` has `back_on_cancel`, `has_cancel_target`, `cancel_target`, and `opened_screen`. |
| Remove cancel dependency on `screen->item_count == 0u`. | Done | No status UI keyboard cancel logic uses that heuristic. |
| Set cancel policy for keyboard open paths. | Done | Wi-Fi, manual SSID/password, AP name/password/channel open paths pass explicit cancel policy. |
| Scan screen consumes global input. | Done | `status_ui_scan_consume_input()` handles scan select/next/prev and lets Back fall through to menu navigation. |
| Menu consumes global input. | Done | `status_ui_menu_consume_input()` handles active-menu select/next/prev/back and preserves Web UI exit-screen Back service-disable behavior for Web UI Wi-Fi/AP result screens only. |
| Idle fallback consumes global input. | Done | `status_ui_idle_consume_input_to_effects()` handles idle `SELECT` and `NEXT` callback paths. |
| Keep ISR/UI separation strict. | Done | No ISR code was introduced; a code comment now documents that GPIO polling/future ISR paths must not inspect UI focus or run cleanup. |
| Optional module split. | Not done (optional) | The original plan allowed leaving this as a follow-up. |
| Optional full gesture-recognition extraction. | Partially done (optional) | Full debounce/gesture state remains in `status_ui.c`; only the pure physical gesture-to-global-input mapper was extracted for host coverage. |

### Intentional behavior resolution

The original task text contained both an idle-callback preservation requirement
and a menu-consumer suggestion that could be read as opening the menu whenever a
global input arrives while the menu is inactive. The implementation follows the
required behavior table and idle-callback acceptance criteria: when no foreground
layer is active and the menu is inactive, `STATUS_UI_INPUT_SELECT` and
`STATUS_UI_INPUT_NEXT` go to the existing KEY1/KEY2 idle callback path; KEY1 long
remains the preserved menu-open gesture.

## Documentation/comment update checklist

| File | Update |
| --- | --- |
| `docs/hardware/sticks3/sticks3_button.md` | Documents the global input contract for StickS3 KEY1/KEY2 gestures and links the input mapper/review. |
| `docs/implementation_inventory.md` | Lists `status_ui_input_map.c` and updates `status_ui.c` / `ui_keyboard.c` summaries. |
| `docs/README.md` | Links this status UI input review from the hardware/reference section. |
| `src/ui/status_ui.c` | Documents that physical gesture dispatch must remain UI-state blind and that foreground cleanup belongs to focused input handling. |
| `tests/host/run_host_tests.sh` | Builds and runs the new input-mapping host test. |
| `src/CMakeLists.txt` | Includes the new mapper source in the firmware component. |

## Test coverage status

Implemented host coverage:

- `tests/host/test_status_ui_input_map.c` verifies the physical gesture to
  global input mapping for KEY1 short, KEY2 single, KEY2 double, and KEY2 long,
  and verifies KEY1 long remains outside that mapping. It also verifies the
  authorization decision mapping.
- `tests/tools/test_web_auth_input.py` compiles the production input dispatcher
  and approval effects with owner/menu spies, checking prompt focus and
  rejection of approval before the matching request is rendered. It compiles
  the production pop-up renderer with display-text helpers to check that all
  instructions fit the 135×240 display, including the largest request ID.
- `tests/host/test_ui_keyboard.c` verifies keyboard cancel commits pending
  multi-tap state and records cancel metadata explicitly.

Validation commands used for this review:

- `python3 tools/check_docs_consistency.py`
- `python3 tools/check_source_inventory.py`
- `./tests/host/run_host_tests.sh`
- `git diff --check`

Known validation limitation:

- The default ESP32-S3 firmware now builds with ESP-IDF v6.1. Host input/layout
  tests and the build do not establish physical display or button behavior.

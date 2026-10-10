# StickS3 display hardware notes

The onboard LCD is documented as an ST7789P3 panel with 135x240 resolution. The firmware maps MOSI=GPIO39, SCLK=GPIO40, RS/DC=GPIO45, CS=GPIO41, RST=GPIO21, and BL=GPIO38, uses SPI3_HOST at 40 MHz, and applies the source-backed ST7789 controller RAM gap X=52/Y=40. `CONFIG_APP_STATUS_UI_LCD` defaults to enabled and makes `status_ui_init()` initialize the panel, enable the backlight, and render the launcher/menu UI with Wi-Fi, BLE status, automation, and settings screens. LCD bring-up is intentionally non-fatal so Wi-Fi, BLE, and automation validation can continue if display hardware is unavailable or panel initialization fails.

When validating the L3B/LCD fix on UART, the expected M5PM1 lines include `active_level=high`, `out_reg=0x11`, `out_value=0x04`, and a later GPIO output update with `value=0x04`. If the UART still reports `active_level=low` or `GPIO2 driven low`, the board is running an older application image and must be rebuilt/reflashed before judging the LCD or ES8311 result.

Display-related pins are internal board resources. GPIO39 is LCD MOSI and must not be configured as a status button, user GPIO, rule input, or rule output.

A pending Web UI browser request shows an authorization pop-up over the current LCD screen with its request number and KEY1 approve / KEY2 reject instructions. It hides keyboard overlays and toasts until the request is decided, cancelled or expires; the underlying navigation state is preserved. Wi-Fi/AP URL screens no longer display a manual access code.

## Sources

- M5Stack StickS3 product page specifications and pin map for ST7789P3, 135x240 resolution, and LCD GPIOs: https://docs.m5stack.com/en/core/StickS3
- M5Stack StickS3 display Arduino API page for the official display feature: https://docs.m5stack.com/en/arduino/m5sticks3/display
- M5GFX StickS3 initialization source for ST7789 offsets/backlight behavior: https://github.com/m5stack/M5GFX/blob/master/src/M5GFX.cpp
- Firmware LCD implementation and UART validation log strings: ../../../src/ui/status_lcd.c
- Firmware M5PM1 L3B enable implementation used before LCD reset: ../../../src/board/m5pm1.c

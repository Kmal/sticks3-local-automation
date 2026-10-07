# M5Stack StickS3 hardware overview and shared facts

This directory records the StickS3 hardware facts that the firmware is allowed to rely on. Keep `docs/hardware/sticks3/sticks3.board.json`, this hardware note set, and `src/board/board_sticks3.h` synchronized for board facts. Keep product behavior and user-facing firmware instructions in `docs/README.md`.

## Evidence map

| Item | Value | Source |
| --- | --- | --- |
| Controller | ESP32-S3-PICO-1-N8R8 | M5Stack StickS3 documentation |
| Flash | 8 MB | M5Stack StickS3 documentation |
| Bluetooth capability relevant to this project | ESP32-S3 supports Bluetooth LE, not Bluetooth Classic / BR/EDR | Espressif ESP32-S3 Bluetooth documentation |
| Audio codec | ES8311 | M5Stack StickS3 documentation and schematic |
| ES8311 I2C address | `0x18` | M5Stack StickS3 pin map |
| ES8311 MCLK | GPIO18 | M5Stack StickS3 pin map |
| ESP32-S3 I2S speaker/DAC data (`G14_I2S_DDAC`) | GPIO14 | StickS3 schematic and M5Unified StickS3 speaker config |
| ES8311 BCLK | GPIO17 | M5Stack StickS3 pin map |
| ES8311 LRCK / WS | GPIO15 | M5Stack StickS3 pin map |
| ESP32-S3 I2S microphone/ADC data (`G16_I2S_DADC`) | GPIO16 | StickS3 schematic and M5Unified StickS3 microphone config |
| Shared I2C SCL | GPIO48 | M5Stack StickS3 pin map |
| Shared I2C SDA | GPIO47 | M5Stack StickS3 pin map |
| IMU | BMI270 | M5Stack StickS3 documentation |
| BMI270 I2C address | `0x68` | M5Stack StickS3 pin map |
| PMIC / helper controller | M5PM1 | M5Stack StickS3 pin map |
| M5PM1 I2C address | `0x6e` | M5Stack StickS3 pin map |
| Speaker control function | `PYG3_SPK_Pulse` through M5PM1 | M5Stack StickS3 pin map |
| Audio/L3B control function | `PYG2_L3B_EN` through M5PM1 | M5Stack StickS3 pin map |
| IMU interrupt function | `PYG4_IMU_INT` through M5PM1 | M5Stack StickS3 pin map |
| User key 1 | GPIO11 / `KEY1` | M5Stack StickS3 pin map |
| User key 2 | GPIO12 / `KEY2` | M5Stack StickS3 pin map |
| LCD MOSI | GPIO39 | M5Stack StickS3 pin map |
| LCD SCLK | GPIO40 | M5Stack StickS3 pin map |
| LCD RS/DC | GPIO45 | M5Stack StickS3 pin map |
| LCD CS | GPIO41 | M5Stack StickS3 pin map |
| LCD reset | GPIO21 | M5Stack StickS3 pin map |
| LCD backlight | GPIO38 | M5Stack StickS3 pin map |
| LCD controller/resolution | ST7789P3, 135x240 | M5Stack StickS3 documentation |
| IR TX | GPIO46 | M5Stack StickS3 pin map |
| IR RX | GPIO42 | M5Stack StickS3 pin map |

## Firmware pin mapping

`src/board/board_sticks3.h` is the authoritative firmware header for board-specific pins and hardware constants. This table keeps the removed README pin map in the hardware reference so board wiring, directions, and firmware constants stay documented in one hardware-focused place.

| Signal/function | ESP32-S3 GPIO/address | Direction | Firmware constant | Notes |
| --- | ---: | --- | --- | --- |
| SoC/module | ESP32-S3-PICO-1-N8R8 | N/A | N/A | ESP32-S3 does not support Bluetooth Classic / BR/EDR. |
| I2S MCLK | GPIO18 | ESP32-S3 -> ES8311 | `BOARD_I2S_MCLK_IO` | Current profile uses fixed MCLK at 12.288 MHz. |
| I2S BCLK | GPIO17 | ESP32-S3 -> ES8311 | `BOARD_I2S_BCK_IO` | Current documented target is 1.024 MHz for 16 kHz, 16-bit mono payload in two 32-bit standard I2S slots. |
| I2S LRCLK/WS | GPIO15 | ESP32-S3 -> ES8311 | `BOARD_I2S_WS_IO` | Current profile uses 16 kHz LRCK. |
| I2S TX / speaker DAC data (`G14_I2S_DDAC`) | GPIO14 | ESP32-S3 -> ES8311 | `BOARD_I2S_DO_IO` | Codec DAC/input data pin used by the speaker path. |
| I2S RX / microphone ADC data (`G16_I2S_DADC`) | GPIO16 | ES8311 -> ESP32-S3 | `BOARD_I2S_DI_IO` | Microphone/ADC samples read by the ESP32-S3. |
| I2C SDA | GPIO47 | Bidirectional | `BOARD_I2C_SDA_IO` | Shared ES8311/BMI270/M5PM1 control bus. |
| I2C SCL | GPIO48 | ESP32-S3 -> devices | `BOARD_I2C_SCL_IO` | Shared bus clock; M5PM1 handle remains at 100 kHz for power-up behavior. |
| ES8311 I2C address | `0x18` | N/A | `BOARD_ES8311_ADDR` | Minimal codec driver target. |
| BMI270 I2C address | `0x68` | N/A | `BOARD_BMI270_ADDR` | Used by the polling-only `bmi270.motion` hardware fact service when `CONFIG_APP_BMI270_FACTS=y`; interrupt routing remains unused. |
| M5PM1 I2C address | `0x6e` | N/A | `BOARD_M5PM1_ADDR` | Used for source-backed L3B audio rail and LCD power sequence. |
| User key 1 | GPIO11 | Input | `BOARD_BUTTON_KEY1_GPIO` | Official StickS3 `KEY1`, active-low with pull-up; cycles display pages and emits automation facts. |
| User key 2 | GPIO12 | Input | `BOARD_BUTTON_KEY2_GPIO` | Official StickS3 `KEY2`, active-low with pull-up; cycles app modes and emits automation facts. |
| LCD MOSI | GPIO39 | ESP32-S3 -> ST7789P3 | `BOARD_LCD_MOSI_GPIO` | Must not be configured as a status button or user GPIO. |
| LCD SCLK | GPIO40 | ESP32-S3 -> ST7789P3 | `BOARD_LCD_SCLK_GPIO` | SPI clock for the onboard 135x240 LCD. |
| LCD RS/DC | GPIO45 | ESP32-S3 -> ST7789P3 | `BOARD_LCD_DC_GPIO` | Display data/command select. |
| LCD CS | GPIO41 | ESP32-S3 -> ST7789P3 | `BOARD_LCD_CS_GPIO` | Display chip select. |
| LCD reset | GPIO21 | ESP32-S3 -> ST7789P3 | `BOARD_LCD_RST_GPIO` | Display reset. |
| LCD backlight | GPIO38 | ESP32-S3 -> LCD backlight | `BOARD_LCD_BL_GPIO` | Active-high backlight enable. |
| IR TX | GPIO46 | ESP32-S3 -> IR LED | `BOARD_IR_TX_GPIO` | NEC IR send action uses this RMT TX route after rule validation. |
| IR RX | GPIO42 | IR receiver -> ESP32-S3 | `BOARD_IR_RX_GPIO` | Reserved as documented IR RX; no receive action/source is wired. |

## Current firmware status

StickS3 uses an ESP32-S3 controller, and ESP32-S3 does not support Bluetooth Classic / BR/EDR. StickS3 can advertise and connect with Bluetooth LE services only; it cannot pair as a Classic Bluetooth audio endpoint, headset, speaker, serial-port, or other BR/EDR profile. No unsupported Bluetooth audio transport source or Kconfig option remains in this project.

For product behavior, user-facing firmware functions, and transport/API status, use `docs/README.md`. This hardware note set records the board-level implications of the current default firmware: the onboard ST7789P3 135x240 LCD path is optional and non-fatal, the shared ESP-IDF v6 I2C master bus owns ES8311/BMI270/M5PM1 access, and the ES8311/I2S path is initialized only in a capture-only profile unless a Kconfig-gated speaker action temporarily owns playback.

### Code-derived hardware implementation overlay

| Hardware-facing feature | Current code status | What that means |
| --- | --- | --- |
| LCD power and panel initialization | ✅ Implemented/wired when `CONFIG_APP_STATUS_UI_LCD=y` | `status_ui_init()` initializes the shared I2C bus, calls the M5PM1 LCD/L3B power helper, initializes the ST7789P3 panel, enables backlight GPIO38, and treats LCD failure as non-fatal. |
| Two StickS3 keys | ✅ Implemented/wired | Only GPIO11/KEY1 and GPIO12/KEY2 are polled as user keys; they drive menu navigation and button automation facts. |
| M5PM1 L3B helper | ✅ Wired for LCD/audio | The source-backed GPIO2-active-high sequence exists and is used by LCD power and by demand-driven ES8311 capture initialization. |
| Capture-only I2S/ES8311 profile | ✅ Implemented/wired by default | `board_audio_init_with_ops()` sequences I2C, optional M5PM1 probe, required audio power, I2S, and ES8311 ADC-only setup because `CONFIG_APP_SOUND_LEVEL_TRIGGERS=y` is set in the project defaults. |
| ES8311 microphone capture path | ✅ Implemented/wired by default | The default `CONFIG_APP_SOUND_LEVEL_TRIGGERS=y` build initializes the ES8311/I2S path only while firmware demand requires microphone metrics, reads normalized I2S microphone samples, and keeps the path capture-only. |
| I2S TX / onboard speaker output | ✅ Implemented/Kconfig-gated | StickS3 has an AW8737-backed speaker path; `speaker_tone` is Kconfig-gated and drives `G14_I2S_DDAC` with the M5PM1 PYG3 amplifier enabled only during playback. |
| IR send | ✅ Implemented/wired for actions | NEC IR actions use the RMT TX path on GPIO46 after rule validation. |
| IR receive | ⛔ Not implemented | GPIO42 is reserved as documented IR RX, but no receive action/source is wired. |
| BMI270 | ✅ Implemented/default available | `CONFIG_APP_BMI270_FACTS=y` is enabled in the project defaults, so `bmi270.motion` uses polling-only accelerometer reads unless disabled in menuconfig; interrupt routing through M5PM1 GPIO4 remains unused. |
| External GPIO digital/edge | ✅ Implemented/wired with validation | GPIO rules are allowed only after conflict checks reject LCD, I2C, I2S/audio, buttons, IR, boot/USB, and internal-risk pins. |
| GPIO pulse/frequency and HAT devices | ⛔ Not implemented / fail-closed | Capability validation reports these sources/actions disabled until source-backed drivers and routing are added. |
| Battery percent fact | 🟡 Implemented/default available; hardware calibration/bench check needed | `CONFIG_APP_BATTERY_FACTS=y` is enabled in the project defaults, so M5PM1-backed `power.battery_percent` is compiled into the default firmware and emitted by `hardware_fact_service` unless disabled in menuconfig; the firmware LiPo interpolation curve still requires hardware comparison against the official `M5.Power` battery API. |
| USB/external-power fact | 🟡 Implemented/default available; hardware calibration/bench check needed | `CONFIG_APP_USB_POWER_FACTS=y` is enabled in the project defaults, so M5PM1-backed `power.usb_present` is compiled into the default firmware and emitted by `hardware_fact_service` unless disabled in menuconfig; VIN/5V reads are independent of VBAT availability; the fact is emitted only when USB presence is proven or both VIN and 5V reads prove absence, and the firmware does not change EXT_5V mode. |
| Safe ADC voltage facts | 🟡 Implemented/default available; hardware calibration/bench check needed | `CONFIG_APP_ADC_FACTS=y` is enabled in the project defaults, so ADC1-only `adc.voltage_mv` is compiled into the default firmware and emitted by `hardware_fact_service` for the safe Grove/Hat ADC1 allowlist unless disabled in menuconfig; exposure remains limited to that allowlist pending hardware calibration/bench checks. |

## Shared I2C bus

ES8311 (`0x18`), BMI270 (`0x68`), and M5PM1 (`0x6e`) share the GPIO47/GPIO48 I2C bus. `src/board/board_i2c.*` owns bus installation. Device drivers must not install the I2C driver independently.

## Automation hardware scope

Safe GPIO digital and edge triggers are implemented only behind conflict validation. Pins used by LCD, I2C, I2S/audio, the two StickS3 buttons, IR, boot/USB, or internal-risk functions are rejected before a rule can be saved. Battery percent, USB/external-power present, BMI270 motion, and safe ADC1 facts are compiled by the default `config/sdkconfig.defaults` profile and remain behind separate Kconfig gates for menuconfig opt-out builds. HAT sensors/actions and GPIO pulse/frequency facts remain disabled until their hardware behavior and routing are verified.

## Unknowns / deferred decisions

- Hardware bench validation of the default sound-level capture path, including measured I2S clocks and ES8311 sample alignment; the firmware still does not expose PCM streaming.
- Hardware bench validation of the default-enabled, separately Kconfig-gated battery, USB/external-power, BMI270 polling, and ADC facts.
- Whether HAT features are needed by a future product feature.

## ADC voltage: `adc.voltage_mv`

* Source: ESP-IDF ADC oneshot on ESP32-S3 ADC1 only; ADC2 is not exposed.
* Safe source keys:
  * `grove.g9`
  * `grove.g10`
  * `hat.g4`
  * `hat.g5`
  * `hat.g6`
  * `hat.g7`
  * `hat.g8`
* Explicit exclusions:
  * `GPIO1`, `GPIO2`, and `GPIO3` are not exposed by default because they overlap boot/power-sensitive StickS3 Hat2-Bus roles.
  * `GPIO11` and `GPIO12` are not exposed because they are user buttons.
  * `GPIO19` and `GPIO20` are not exposed because ESP-IDF uses them for USB-JTAG by default on ESP32-S3.
  * `GPIO43` and `GPIO44` are not exposed as ADC because they are not ESP32-S3 ADC-capable GPIOs.
  * LCD, I2C, audio, IR, and other internal-risk pins are not exposed.
* Configuration: `CONFIG_APP_ADC_FACTS=y` is set in `config/sdkconfig.defaults`; emitted only while that Kconfig option remains enabled.

### Implementation references

* StickS3 product documentation and Arduino programming docs provide the shared I2C pins, BMI270/M5PM1 addresses, Grove/Hat2-Bus pin map, buttons, LCD, audio, and IR exclusions.
* M5PM1 Arduino docs and source provide the source-verified PMIC voltage register layout used for VBAT, VIN, VREF, and 5V in/out reads; the firmware follows the upstream little-endian 16-bit voltage read helper so USB/VIN values above 4095 mV remain representable.
* The BMI270 datasheet provides chip ID and accelerometer data register semantics for the polling driver.
* The ESP-IDF Programming Guide provides the ADC oneshot and calibration API assumptions and ESP32-S3 ADC channel mapping.

## Sources

- Hardware manifest checked by validation tools: sticks3.board.json
- Firmware board constants: ../../../src/board/board_sticks3.h
- Firmware hardware fact service implementation: ../../../src/board/hardware_fact_service.c
- Firmware GPIO/action/source capability validation: ../../../src/rules/rule_types.c
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

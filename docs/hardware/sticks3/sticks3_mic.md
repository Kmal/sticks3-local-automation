# StickS3 microphone and capture hardware notes

## Audio clock profile

The default capture-only software profile is intentionally explicit when the sound-level audio path is called:

- sample rate / LRCK: 16 kHz
- fixed MCLK: 12.288 MHz on GPIO18
- BCLK target: 1.024 MHz on GPIO17 for 16 kHz, 16-bit mono payload in two 32-bit slots
- LRCK/WS: GPIO15
- ES8311 12.288 MHz / 16 kHz clock-manager register-2 value used by the current project sequence: `0x40`

For the ESP-IDF v6 standard I2S channel API path, the driver uses a 768 × Fs MCLK multiple to generate the 12.288 MHz ES8311 master clock for 16 kHz audio and sets a 32-bit slot width in a two-slot standard I2S frame; mono selects one active slot but both slots contribute to BCLK while reading DMA data as 16-bit samples according to `I2S_DATA_BIT_WIDTH_16BIT`. Hardware acceptance must measure GPIO18, GPIO17, and GPIO15 before claiming physical audio success. The default `CONFIG_APP_SOUND_LEVEL_TRIGGERS=y` build calls this initializer for capture-only microphone metrics.

## Sound-level trigger reference review

Shared sound-capture work requires an upstream/vendor reference review before changing audio code. For this implementation, the ESP-IDF programming guide was used for component/Kconfig conventions, FreeRTOS task/semaphore use, logging/error style, and the channel-based I2S API assumptions. M5Stack StickS3 product and Arduino/M5PM1 documentation, the StickS3 schematic, M5PM1, M5Unified, and M5GFX sources were cross-checked for the ESP32-S3-PICO-1-N8R8 board identity, ES8311 mono codec, MEMS microphone, AW8737 amplifier, MCLK/BCLK/LRCK/DADC/DDAC pins, shared I2C addresses, M5PM1 L3B power behavior, PYG3 speaker-amplifier behavior, and LCD/L3B safety. The ES8311 datasheet was used for ADC/I2S-format expectations, MCLK/LRCK clocking, and capture-only codec setup. The BMI270 datasheet was reviewed only to avoid altering the shared I2C/interrupt behavior; sound-capture demand code does not initialize or change BMI270 state.

Shared sound capture uses `BOARD_AUDIO_PROFILE_CAPTURE_ONLY` for both enabled `sound.*` rules and Web UI telemetry demand. The speaker amplifier and I2S TX/DAC path remain disabled for that capture feature; the `speaker_tone` action follows the official M5Unified single-owner audio pattern by stopping microphone capture before starting `BOARD_AUDIO_PROFILE_PLAYBACK_ONLY`, then allowing demand-driven capture to restart afterwards. Experimental USB Audio Class support is Kconfig-gated outside the default automation image; its microphone-only path reuses this capture-only hardware path, while `BOARD_AUDIO_PROFILE_SIMULTANEOUS_MIC_SPEAKER` is reserved for explicit simultaneous mic+speaker UAC experiments that still require real StickS3 hardware validation before product claims.

## Audio init failure policy

`board_audio_init_with_ops()` initializes in this order in the default `CONFIG_APP_SOUND_LEVEL_TRIGGERS=y` build when shared sound demand requires monitoring (enabled `sound.*` rules or Web UI telemetry): shared I2C, optional M5PM1 probe, required source-backed audio power enable, I2S profile, ES8311 profile. On failure, later steps are skipped and the cleanup hook is called. The production cleanup policy logs the failure and does not guess a power-disable sequence because no source-backed L3B disable sequence exists yet. Maintainers can still turn the Kconfig option off for audio-free builds.

## Sources

- M5Stack StickS3 product page for ES8311 codec, MEMS microphone, audio pins, and mic/speaker hardware: https://docs.m5stack.com/en/core/StickS3
- M5Stack StickS3 microphone Arduino API page for the official microphone feature and single-owner audio warning: https://docs.m5stack.com/en/arduino/m5sticks3/mic
- ES8311 datasheet used for ADC/I2S-format and clocking assumptions: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/products/atom/Atomic%20Echo%20Base/ES8311.pdf
- ESP-IDF Programming Guide for I2S driver assumptions: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2s.html
- Firmware audio clock and capture implementation: ../../../src/audio/board_audio_clock.c
- Firmware board audio initialization policy: ../../../src/audio/board_audio.c
- Firmware USB Audio Class helper implementation: ../../../src/usb_audio/uac_config.c

The BCLK expectation follows the [ESP-IDF v6.1 standard driver](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_driver_i2s/i2s_std.c): `Fs × total_slot × slot_bits = 16000 × 2 × 32 = 1024000 Hz`. This is a source-derived expectation, not a physical measurement.

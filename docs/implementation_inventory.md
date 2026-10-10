# Current C implementation inventory

This inventory is generated from direct source inspection of `src/**/*.c`, `src/CMakeLists.txt`, and `tests/host/run_host_tests.sh`. It is intentionally one row per C implementation file so default firmware wiring, conditional transports, helper-only code, and host coverage cannot be inferred from stale comments. Generated C assets outside `src/**/*.c`, such as `generated/webui_assets.c`, are validated by their own budget/build checks instead of being inventoried here.

| Source | Default app link | Host test | Code-derived status |
| --- | --- | --- | --- |
| `action_dispatcher.c` | default | yes | Queue-backed rule action dispatcher used by `rule_runtime.c`; BLE, HTTP, IR, local UI, and speaker tone actions are dispatched or fail closed as unsupported. |
| `action_hat.c` | default | yes | HAT actions are deliberately unsupported; capability/action dispatch remains fail-closed. |
| `action_http.c` | default | yes | HTTP POST action helper is used by the app HTTP sender when network readiness is true; host tests cover JSON/config readiness behavior. |
| `action_ir.c` | default | yes | NEC IR send action helper is wired through the app IR sender and validates carrier/repeat/timeout bounds. |
| `action_speaker.c` | conditional | yes | Speaker tone action helper validates bounded tone parameters, initializes the playback-only StickS3 ES8311/I2S path, enables the M5PM1 PYG3 amplifier only during playback, writes PCM tone frames, and fails closed off target. |
| `app_mode.c` | default | yes | Initializes and names the local control app mode used by boot/status state. |
| `app_sound_level_demand.c` | default | yes | Shared sound-capture demand helper that combines enabled sound-rule trigger demand with Web UI telemetry demand before `main.c` starts the single capture service. |
| `app_time.c` | default | yes | Provides timezone storage/formatting and `/api/time` JSON support used by boot and Web UI. |
| `app_wifi.c` | default | yes | Starts station/setup-AP support, Wi-Fi scan/connect/AP/mode APIs, network status JSON, and SNTP sync task. |
| `audio_metrics.c` | default via sound config | yes | Audio level metric/calibration helper source linked by the default `CONFIG_APP_SOUND_LEVEL_TRIGGERS=y` build; consumed by `sound_level_service.c` for live sound rules. |
| `audio_pipeline.c` | helper-only | yes | PCM capture/playback buffer helper source; not linked by the default app component. |
| `audio_resample.c` | helper-only | yes | Audio resampling helper source; not linked by the default app component. |
| `bmi270.c` | default | yes | BMI270 polling-only accelerometer driver and deterministic software motion thresholding used by hardware automation facts. |
| `board_adc.c` | default | yes | ESP-IDF ADC1 oneshot allowlist for safe Grove/Hat voltage rule facts with calibrated millivolt conversion and fail-closed behavior when calibration is unavailable. |
| `board_power.c` | default | yes | Board-level M5PM1 power policy, battery percent interpolation, USB/external-power-present thresholding, independent degraded VBAT/VIN/5V voltage reads with explicit USB-valid status, and status UI battery helper. |
| `board_audio.c` | default via sound/speaker config | yes | Capture-only and playback-only audio initializer linked by sound-level trigger or speaker-action builds; `app_main()` uses capture-only for sound rules and `action_speaker.c` uses playback-only for tones. |
| `board_audio_clock.c` | default via sound/speaker config | yes | 16 kHz/12.288 MHz/1.024 MHz audio clock profile helper linked by sound-level trigger or speaker-action builds. |
| `board_audio_power.c` | default via sound/speaker config | yes | M5PM1 L3B audio rail enable wrapper and PYG3 speaker-amplifier control linked by audio trigger or speaker-action builds while preserving LCD M5PM1 behavior. |
| `board_i2c.c` | default | no | ESP-IDF shared I2C bus initializer used by LCD/status UI paths and available to board helpers. |
| `board_i2s.c` | default via sound/speaker config | yes | Capture-only/playback-only I2S driver source linked by sound-level trigger or speaker-action builds; includes 32-bit-slot mono `int16_t` decode helper and pin-configuration coverage. |
| `button_state.c` | default | yes | Active-low KEY1/KEY2 debouncing and event classification for status UI and automation facts. |
| `capability_registry.c` | default | yes | Central capability gate for supported/disabled sources/actions and safe GPIO profile validation. |
| `display_text.c` | default | yes | LCD text measuring, sanitizing, wrapping/marquee, collision, and glyph rendering support. |
| `es8311.c` | default via sound/speaker config | yes | ES8311 codec profile driver source linked by sound-level trigger or speaker-action builds; capture-only setup keeps the DAC muted/down while playback-only setup enables DAC output for bounded tones. |
| `hardware_fact_service.c` | default | yes | Unified hardware fact polling service that emits independently gated battery-percent, USB/external-power-present, BMI270 motion, and safe ADC voltage facts through the trigger adapter. |
| `m5pm1.c` | default | yes | M5PM1 register helper and LCD/L3B GPIO sequence used by LCD power, with bit-preserving host tests. |
| `main.c` | default | no | Firmware entry point wiring NVS, time, network, status UI, Wi-Fi, rule runtime, BLE transport, and default error/idle policy. |
| `register_bus.c` | default | no | ESP-IDF I2C register-bus cache/read/write helper used by M5PM1/codec-style drivers. |
| `rule_config_store.c` | default | yes | NVS-backed automation config load/save/default fallback used by boot, Web UI, and status UI updates. |
| `rule_engine.c` | default | yes | Rule condition evaluation, false-to-true firing, sustain, cooldown, action fan-out, and sequence assignment. |
| `rule_runtime.c` | default | yes | Runtime bridge from button/GPIO/BLE/Wi-Fi/sound facts to rule engine and action dispatcher; the default sound service feeds live metrics through this existing path. |
| `rule_types.c` | default | yes | Rule defaults, validation, source/action names, safe GPIO/capability checks, and binary config serialization. |
| `rule_web.c` | default | yes | On-demand HTTP Web UI/API implementation for config, status, time, Wi-Fi, capabilities, test actions, GPIO, and HAT probe; runtime reads, mutations and bounded browser-approval sessions use the application's rule mutex; host tests cover approval, denial, expiry, isolation, cancellation and revocation. |
| `sound_level_service.c` | default via sound config | yes | Demand-driven capture task/service source linked by default sound-level trigger builds; it reads microphone samples, computes metrics, and feeds sound facts while enabled sound rules or Web UI telemetry demand exist. |
| `status_lcd.c` | default | no | Optional LCD bring-up/render task path behind `CONFIG_APP_STATUS_UI_LCD`; failures are non-fatal. |
| `status_ui.c` | default | no | Status UI task, active-low KEY1/KEY2 polling, global input queueing, focused approval/keyboard/scan/menu/idle dispatch (approval dispatcher covered through the extracted-function host test), launcher/menu integration, toasts, service enablement, and automation config callbacks. |
| `status_ui_input_map.c` | default | yes | Pure helper mapping physical KEY1/KEY2 gestures to global `STATUS_UI_INPUT_*` values; KEY1 long remains outside this map to preserve the idle menu-open exception; also maps approval-prompt inputs to approve/reject decisions. |
| `transport_ble_gatt.c` | conditional | no | Custom BLE GATT status/rule-event transport compiled when `CONFIG_APP_TRANSPORT_BLE_GATT_RULE_EVENTS=y`. |
| `trigger_gpio.c` | default | yes | Safe GPIO digital/edge trigger initialization and polling with debounce and source-key generation. |
| `trigger_hat.c` | default | yes | HAT source probe path deliberately returns unsupported. |
| `trigger_sources.c` | default | yes | Emits normalized sound, button, and direct facts into a runtime sink. |
| `uac_audio_buffer.c` | conditional | yes | USB Audio Class ring-buffer helper linked only by `CONFIG_APP_USB_UAC_DEVICE`; host tests cover wraparound, underrun-to-silence, overrun counters, byte accounting, and concurrent producer/consumer integrity across position overflow. |
| `uac_config.c` | conditional | yes | USB Audio Class mode/config resolver linked only by explicit UAC builds; validates mic-only, speaker-only, combined descriptor, simultaneous mic+speaker, sample-rate, ring-buffer, and safe-volume policy. |
| `uac_device_adapter.c` | conditional | yes | Callback-safe UAC adapter linked only by explicit UAC builds; maps descriptor direction plans to ring-buffer input/output callbacks and clamps volume without touching codec/I2S in callbacks. |
| `uac_esp_device.c` | conditional | no | Thin ESP-IDF adapter linked only by explicit UAC builds; maps the project adapter to Espressif `usb_device_uac` fields (`input_cb`, `output_cb`, mute, volume, context, and TinyUSB init flag). |
| `uac_mic_source.c` | conditional | yes | USB microphone PCM source linked only by explicit UAC builds; reuses the existing capture-only ES8311/I2S path and exposes bounded host-tested reads/stats. |
| `uac_speaker_sink.c` | conditional | yes | USB speaker PCM sink linked only by explicit UAC builds; reuses the existing playback-only ES8311/I2S path, requires audio power, and clamps volume below 75%. |
| `uac_service.c` | conditional | yes | Opt-in USB Audio Class service linked only by `CONFIG_APP_USB_UAC_DEVICE`; resolves Kconfig, allocates direction ring buffers, starts the selected board-audio owner, creates gated bridge tasks before registering Espressif UAC callbacks; enabled-path tests exercise task failures and retained callbacks after USB initialization failure. |
| `ui_keyboard.c` | default | yes | 9-key overlay input model for SSID/password/AP/time fields, including explicit cancel result support and menu-edit cancel metadata coverage. |
| `ui_model.c` | default | yes | Menu/application model for Wi-Fi, AP, Bluetooth, field-specific automation edits through the application-owned transaction backend, Web UI service state, and time settings. |
| `ui_nav.c` | default | yes | Menu graph/navigation state machine for the status UI. |
| `ui_render.c` | default | no | LCD rendering of status bar, menus, Wi-Fi/AP/BLE/automation/settings screens, device browser-approval prompts, toasts, and keyboard overlay; the focused extracted pop-up renderer has host layout coverage, while the full LCD module remains untested on host. |


## Hardware automation facts inventory

Compiled by the default `config/sdkconfig.defaults` profile and emitted when the matching Kconfig option remains enabled:

* `power.battery_percent` — default-enabled by `CONFIG_APP_BATTERY_FACTS=y`.
* `power.usb_present` — default-enabled by `CONFIG_APP_USB_POWER_FACTS=y`.
* `bmi270.motion` — default-enabled by `CONFIG_APP_BMI270_FACTS=y`.
* `adc.voltage_mv` — default-enabled by `CONFIG_APP_ADC_FACTS=y`.

## Correctness review follow-up

The local review dated 2026-10-06 corrected LCD configuration stack allocations, pulse versus state trigger semantics, timer-driven sustain and queue retries, complete/masked configuration round-trips, disabled-feature defaults, hardware sampling demand and ADC/GPIO ownership, startup failure handling, audio lifetime synchronization, HTTPS trust attachment, and isolated UAC build configurations. `ui_model.c` now has integration coverage for retaining existing action parameters and extra actions during LCD saves.

The HTTP configuration parser also links the vendored upstream `vendor/cjson/cJSON.c` (v1.7.19, MIT license); this is external code outside `src/**/*.c`, so it is documented here rather than added to the project-source inventory table. Its source, header, license, and hashes are recorded in `vendor/cjson/README.md`.

UAC remains opt-in. The mic/speaker profile defaults match actual Espressif USB descriptor sample rate and channel counts to the 16 kHz mono bridge. Compile-time assertions reject mismatches. Local microphone monitoring and speaker actions are unavailable while USB owns the codec. `usb_device_uac` 1.2.3 has no stop/deinit API: after partial USB initialization failure, bridge tasks and board audio stop, while callback buffers remain allocated until reboot to prevent dangling driver callbacks. Startup cannot be retried in that boot.

Build a UAC profile using a separate build directory and sdkconfig, for example:

```sh
idf.py -B build-uac-mic -D SDKCONFIG="$PWD/build-uac-mic/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=config/sdkconfig.defaults;config/sdkconfig.uac-mic.defaults' \
  -D IDF_TARGET=esp32s3 build
```

Use `speaker` in both profile paths for the speaker-only build. Compilation and host tests are software evidence. Physical qualification remains governed by `docs/hardware_qualification.md`.

The 2026-10-07 follow-up adds production LCD transaction callback coverage (including lock/commit failure and immediate Web edits), discarded rejected pulses, freshness-gated sampled sustain, periodic USB/motion observations, and source-derived two-slot I2S clock expectations.

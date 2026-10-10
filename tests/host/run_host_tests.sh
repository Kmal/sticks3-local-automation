#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${ROOT}/build/host-tests"
FAKE_INC="${ROOT}/tests/host/fakes/esp_idf_stubs"
CC_BIN="${CC:-cc}"
EXTRA_CFLAGS=()
EXTRA_LDFLAGS=()
if [[ -n "${CFLAGS:-}" ]]; then
  # shellcheck disable=SC2206 # Intentionally honor conventional space-separated CFLAGS.
  EXTRA_CFLAGS=(${CFLAGS})
fi
if [[ -n "${LDFLAGS:-}" ]]; then
  # shellcheck disable=SC2206 # Intentionally honor conventional space-separated LDFLAGS.
  EXTRA_LDFLAGS=(${LDFLAGS})
fi
mkdir -p "${BUILD_DIR}"
python3 "${ROOT}/webui/build_webui.py" --check
python3 "${ROOT}/tools/check_web_ui_budget.py"
python3 "${ROOT}/tests/tools/test_build_webui.py"
node "${ROOT}/tests/web/test_webui.js"
python3 "${ROOT}/tests/tools/test_web_auth_input.py"
python3 "${ROOT}/tests/tools/test_app_runtime_startup.py"
python3 "${ROOT}/tests/tools/test_app_config_transaction.py"
compile() {
  local output="$1"
  shift
  "${CC_BIN}" -std=c11 -Wall -Wextra -Werror "${EXTRA_CFLAGS[@]}" "$@" "${EXTRA_LDFLAGS[@]}" -o "${BUILD_DIR}/${output}"
}
compile test_button_state -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/app/button_state.c" "${ROOT}/tests/host/test_button_state.c"
compile test_audio_resample -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/audio_resample.c" "${ROOT}/tests/host/test_audio_resample.c"
compile test_audio_pipeline -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/audio_pipeline.c" "${ROOT}/tests/host/test_audio_pipeline.c"
compile test_audio_metrics -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/audio_metrics.c" "${ROOT}/tests/host/test_audio_metrics.c" -lm
compile test_app_mode -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/app/app_mode.c" "${ROOT}/tests/host/test_app_mode.c"
compile test_ui_nav -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/ui/ui_nav.c" "${ROOT}/tests/host/test_ui_nav.c"
compile test_ui_keyboard -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/ui/ui_keyboard.c" "${ROOT}/tests/host/test_ui_keyboard.c"
compile test_status_ui_input_map -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/ui/status_ui_input_map.c" "${ROOT}/tests/host/test_status_ui_input_map.c"
compile test_display_text -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/ui/display_text.c" "${ROOT}/tests/host/test_display_text.c"
compile test_ble_rule_protocol -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/tests/host/test_ble_rule_protocol.c"
compile test_es8311_sequence -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/es8311.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_es8311_sequence.c"
compile test_m5pm1_gpio -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/board/board_power.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_m5pm1_gpio.c"

compile test_m5pm1_power -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/m5pm1.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_m5pm1_power.c"
compile test_board_power -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/board/board_power.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_board_power.c"
compile test_bmi270_motion -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/bmi270.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_bmi270_motion.c"
compile test_board_adc -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/board_adc.c" "${ROOT}/tests/host/test_board_adc.c"
compile test_hardware_fact_service -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/triggers/trigger_sources.c" "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/board/board_power.c" "${ROOT}/src/board/bmi270.c" "${ROOT}/src/board/board_adc.c" "${ROOT}/src/board/hardware_fact_service.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_hardware_fact_service.c"
compile test_board_audio -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/board_audio.c" "${ROOT}/tests/host/fakes/fake_board_deps.c" "${ROOT}/tests/host/test_board_audio.c"
compile test_board_audio_power -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/audio/board_audio_power.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/tests/host/test_board_audio_power.c"
compile test_board_audio_clock -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/board_audio_clock.c" "${ROOT}/tests/host/test_board_audio_clock.c"
compile test_board_i2s_decode -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/board_i2s.c" "${ROOT}/tests/host/test_board_i2s_decode.c"
compile test_sound_level_service -DSOUND_LEVEL_SERVICE_ENABLE_TEST_HOOKS -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/audio/audio_metrics.c" "${ROOT}/src/audio/sound_level_service.c" "${ROOT}/tests/host/test_sound_level_service.c" -lm
compile test_app_sound_level_demand -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/app/app_sound_level_demand.c" "${ROOT}/tests/host/test_app_sound_level_demand.c"
compile test_rule_types -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_rule_types.c"
compile test_capability_sound_enabled -DCONFIG_APP_SOUND_LEVEL_TRIGGERS=1 -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_capability_sound_enabled.c"
compile test_capability_hardware_enabled -DCONFIG_APP_BATTERY_FACTS=1 -DCONFIG_APP_USB_POWER_FACTS=1 -DCONFIG_APP_BMI270_FACTS=1 -DCONFIG_APP_ADC_FACTS=1 -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_capability_hardware.c"
compile test_capability_hardware_disabled -DCONFIG_APP_BATTERY_FACTS=0 -DCONFIG_APP_USB_POWER_FACTS=0 -DCONFIG_APP_BMI270_FACTS=0 -DCONFIG_APP_ADC_FACTS=0 -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_capability_hardware.c"
compile test_capability_sound_disabled -DCONFIG_APP_SOUND_LEVEL_TRIGGERS=0 -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_capability_sound_disabled.c"
compile test_capability_omitted -I"${ROOT}/tests/host/fakes/disabled_sdkconfig" -I"${FAKE_INC}" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/tests/host/test_capability_omitted.c"
compile test_rule_engine -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/rules/rule_engine.c" "${ROOT}/tests/host/test_rule_engine.c"
compile test_trigger_sources -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/triggers/trigger_sources.c" "${ROOT}/tests/host/test_trigger_sources.c"
compile test_action_dispatcher -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/actions/action_dispatcher.c" "${ROOT}/tests/host/test_action_dispatcher.c"
compile test_rule_runtime -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/rules/rule_engine.c" "${ROOT}/src/triggers/trigger_sources.c" "${ROOT}/src/triggers/trigger_gpio.c" "${ROOT}/src/actions/action_dispatcher.c" "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/board/board_power.c" "${ROOT}/src/board/bmi270.c" "${ROOT}/src/board/board_adc.c" "${ROOT}/src/board/hardware_fact_service.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/src/rules/rule_runtime.c" "${ROOT}/tests/host/test_rule_runtime.c"
compile test_rule_config_store -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/rules/rule_config_store.c" "${ROOT}/tests/host/test_rule_config_store.c"
compile test_action_modules -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/actions/action_http.c" "${ROOT}/src/actions/action_ir.c" "${ROOT}/src/actions/action_speaker.c" "${ROOT}/src/actions/action_hat.c" "${ROOT}/tests/host/test_action_modules.c"
compile test_external_triggers_and_web -DCJSON_NESTING_LIMIT=16 -I"${ROOT}/vendor/cjson" -I"${FAKE_INC}" -I"${ROOT}/generated" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/rules" -I"${ROOT}/src/actions" -I"${ROOT}/src/triggers" -I"${ROOT}/src/transport" -I"${ROOT}/src/ui" \
  "${ROOT}/src/rules/rule_types.c" "${ROOT}/src/rules/capability_registry.c" "${ROOT}/src/rules/rule_engine.c" "${ROOT}/src/triggers/trigger_sources.c" "${ROOT}/src/actions/action_dispatcher.c" "${ROOT}/src/board/m5pm1.c" "${ROOT}/src/board/board_power.c" "${ROOT}/src/board/bmi270.c" "${ROOT}/src/board/board_adc.c" "${ROOT}/src/board/hardware_fact_service.c" "${ROOT}/tests/host/fakes/fake_register_bus.c" "${ROOT}/src/rules/rule_runtime.c" "${ROOT}/src/rules/rule_config_store.c" "${ROOT}/src/actions/action_http.c" "${ROOT}/src/app/app_wifi.c" "${ROOT}/src/app/app_time.c" "${ROOT}/src/rules/rule_web.c" "${ROOT}/src/ui/ui_model.c" "${ROOT}/src/ui/ui_nav.c" "${ROOT}/generated/webui_assets.c" "${ROOT}/vendor/cjson/cJSON.c" "${ROOT}/src/triggers/trigger_gpio.c" "${ROOT}/src/triggers/trigger_hat.c" "${ROOT}/tests/host/test_external_triggers_and_web.c"

compile test_uac_audio_buffer -pthread -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_audio_buffer.c" "${ROOT}/tests/host/test_uac_audio_buffer.c"
compile test_uac_config -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_config.c" "${ROOT}/tests/host/test_uac_config.c"
compile test_uac_device_adapter -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_audio_buffer.c" "${ROOT}/src/usb_audio/uac_config.c" "${ROOT}/src/usb_audio/uac_device_adapter.c" "${ROOT}/tests/host/test_uac_device_adapter.c"
compile test_uac_sources -I"${FAKE_INC}" -I"${ROOT}/tests/host/fakes" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_config.c" "${ROOT}/src/usb_audio/uac_mic_source.c" "${ROOT}/src/usb_audio/uac_speaker_sink.c" "${ROOT}/tests/host/fakes/fake_uac_board_deps.c" "${ROOT}/tests/host/test_uac_sources.c"
compile test_uac_service -I"${FAKE_INC}" -I"${ROOT}/src/app" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_service.c" "${ROOT}/tests/host/test_uac_service.c"
compile test_uac_service_enabled -DFREERTOS_TEST_HOOKS -DCONFIG_APP_USB_UAC_DEVICE=1 -DCONFIG_APP_USB_UAC_MIC=1 -DCONFIG_APP_USB_UAC_SPEAKER=1 -DCONFIG_APP_USB_UAC_COMBINED_EXPERIMENTAL=1 -DCONFIG_APP_USB_UAC_SIMULTANEOUS_MIC_SPEAKER=1 \
  -I"${FAKE_INC}" -I"${ROOT}/src/audio" -I"${ROOT}/src/board" -I"${ROOT}/src/usb_audio" \
  "${ROOT}/src/usb_audio/uac_service.c" "${ROOT}/src/usb_audio/uac_config.c" "${ROOT}/src/usb_audio/uac_audio_buffer.c" "${ROOT}/src/usb_audio/uac_device_adapter.c" \
  "${ROOT}/src/usb_audio/uac_mic_source.c" "${ROOT}/src/usb_audio/uac_speaker_sink.c" "${ROOT}/tests/host/fakes/fake_uac_board_deps.c" "${ROOT}/tests/host/test_uac_service_enabled.c"
for scenario in 0 1 2 3; do "${BUILD_DIR}/test_uac_service_enabled" "$scenario"; done
"${BUILD_DIR}/test_uac_audio_buffer"
"${BUILD_DIR}/test_uac_config"
"${BUILD_DIR}/test_uac_device_adapter"
"${BUILD_DIR}/test_uac_sources"
"${BUILD_DIR}/test_uac_service"
"${BUILD_DIR}/test_button_state"
"${BUILD_DIR}/test_audio_resample"
"${BUILD_DIR}/test_audio_pipeline"
"${BUILD_DIR}/test_audio_metrics"
"${BUILD_DIR}/test_app_mode"
"${BUILD_DIR}/test_ui_nav"
"${BUILD_DIR}/test_ui_keyboard"
"${BUILD_DIR}/test_status_ui_input_map"
"${BUILD_DIR}/test_display_text"
"${BUILD_DIR}/test_ble_rule_protocol"
"${BUILD_DIR}/test_es8311_sequence"
"${BUILD_DIR}/test_m5pm1_gpio"
"${BUILD_DIR}/test_m5pm1_power"
"${BUILD_DIR}/test_board_power"
"${BUILD_DIR}/test_bmi270_motion"
"${BUILD_DIR}/test_board_adc"
"${BUILD_DIR}/test_hardware_fact_service"
"${BUILD_DIR}/test_board_audio"
"${BUILD_DIR}/test_board_audio_power"
"${BUILD_DIR}/test_board_audio_clock"
"${BUILD_DIR}/test_board_i2s_decode"
"${BUILD_DIR}/test_sound_level_service"
"${BUILD_DIR}/test_app_sound_level_demand"
"${BUILD_DIR}/test_rule_types"
"${BUILD_DIR}/test_capability_sound_enabled"
"${BUILD_DIR}/test_capability_hardware_enabled"
"${BUILD_DIR}/test_capability_hardware_disabled"
"${BUILD_DIR}/test_capability_sound_disabled"
"${BUILD_DIR}/test_capability_omitted"
"${BUILD_DIR}/test_rule_engine"
"${BUILD_DIR}/test_trigger_sources"
"${BUILD_DIR}/test_action_dispatcher"
"${BUILD_DIR}/test_rule_runtime"
"${BUILD_DIR}/test_rule_config_store"
"${BUILD_DIR}/test_action_modules"
"${BUILD_DIR}/test_external_triggers_and_web"

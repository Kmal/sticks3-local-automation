#!/usr/bin/env python3
"""Validate that default StickS3 audio capture stays demand-driven and fail-closed."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "src" / "app" / "main.c"
BOARD_I2S = ROOT / "src" / "audio" / "board_i2s.c"
ES8311 = ROOT / "src" / "audio" / "es8311.c"
ES8311_HEADER = ROOT / "src" / "audio" / "es8311.h"
ACTION_SPEAKER = ROOT / "src" / "actions" / "action_speaker.h"
BOARD_AUDIO_POWER = ROOT / "src" / "audio" / "board_audio_power.c"
CMAKE = ROOT / "src" / "CMakeLists.txt"
COMMON_AUDIO_APP_SRCS = (
    "board_audio.c",
    "board_audio_clock.c",
    "board_audio_power.c",
    "board_i2s.c",
    "es8311.c",
)
SOUND_LEVEL_ONLY_APP_SRCS = (
    "audio_metrics.c",
    "sound_level_service.c",
)
SOUND_LEVEL_APP_SRCS = COMMON_AUDIO_APP_SRCS + SOUND_LEVEL_ONLY_APP_SRCS
FORBIDDEN_DEFAULT_AUDIO_APP_SRCS = (
    "audio_pipeline.c",
    "audio_resample.c",
    "sound_meter.c",
)


def strip_c_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.S)
    return re.sub(r"//.*", "", source)



def cmake_block_has_source(block: str, source: str) -> bool:
    return any(Path(match).name == source for match in re.findall(r'"([^"\n]+\.c)"', block))

def main() -> int:
    errors: list[str] = []
    main_text = strip_c_comments(MAIN.read_text(encoding="utf-8"))
    sound_gate = "if (!needed)"
    audio_init = "board_audio_init(&audio_config)"
    if audio_init not in main_text:
        errors.append("src/app/main.c must wire board_audio_init for demand-driven sound triggers")
    elif sound_gate not in main_text or main_text.index(sound_gate) > main_text.index(audio_init):
        errors.append("src/app/main.c must gate board_audio_init behind shared sound-capture demand")
    if "app_sound_level_demand_capture_needed(&s_sound_level_demand)" not in main_text or "app_sound_level_sync(needed)" not in main_text:
        errors.append("audio owner must consume a snapshot of shared capture demand")
    if "s_audio_mutex" not in main_text or "sound_level_service_deinit(s_sound_level_service)" not in main_text:
        errors.append("audio transitions and service teardown must be serialized")
    if "BOARD_AUDIO_PROFILE_CAPTURE_ONLY" not in main_text:
        errors.append("src/app/main.c must request the capture-only profile for sound triggers")
    if "board_audio_deinit" not in main_text:
        errors.append("src/app/main.c must release audio resources after sound trigger service stops")
    if "ESP_ERROR_CHECK(board_audio_init" in main_text:
        errors.append("audio init failures must not reboot-loop the board unless explicitly configured")
    if "rule_runtime_process_metrics" in main_text:
        errors.append("src/app/main.c must leave sound metric production inside sound_level_service")
    if "app_sound_level_demand_set_telemetry" not in main_text or "s_sound_level_demand" not in main_text:
        errors.append("src/app/main.c must track Web UI telemetry sound-capture demand separately from trigger demand")
    if "no_enabled_sound_rule" in main_text and "if (!app_sound_level_capture_needed(&s_rule_config))" not in main_text:
        errors.append("sound status must report no_enabled_sound_rule only when shared capture demand is inactive")
    if "sound_level_service_start(s_sound_level_service)" not in main_text:
        errors.append("src/app/main.c must centralize sound_level_service_start in app_sound_level_sync")
    if main_text.count("sound_level_service_start(") != 1:
        errors.append("src/app/main.c must have exactly one sound_level_service_start call for the shared capture service")
    ready_guard = "if (s_sound_level_ready) {\n        return;\n    }"
    if ready_guard not in main_text:
        errors.append("src/app/main.c must return early when the shared sound capture service is already ready")
    elif main_text.index(ready_guard) > main_text.index("sound_level_service_start(s_sound_level_service)"):
        errors.append("src/app/main.c must check s_sound_level_ready before starting the shared sound capture service")

    cmake_text = CMAKE.read_text(encoding="utf-8")
    common_audio_block_match = re.search(r"if\(CONFIG_APP_SOUND_LEVEL_TRIGGERS OR CONFIG_APP_SPEAKER_ACTION OR CONFIG_APP_USB_UAC_DEVICE\)(.*?)endif\(\)", cmake_text, flags=re.S)
    if not common_audio_block_match:
        errors.append("app component must guard shared audio sources with CONFIG_APP_SOUND_LEVEL_TRIGGERS OR CONFIG_APP_SPEAKER_ACTION OR CONFIG_APP_USB_UAC_DEVICE")
        common_audio_block = ""
    else:
        common_audio_block = common_audio_block_match.group(1)
    for source in COMMON_AUDIO_APP_SRCS:
        if not cmake_block_has_source(common_audio_block, source):
            errors.append(f"shared audio config must link required source {source}")

    sound_block_match = re.search(r"if\(CONFIG_APP_SOUND_LEVEL_TRIGGERS\)(.*?)endif\(\)", cmake_text, flags=re.S)
    if not sound_block_match:
        errors.append("default app component must guard sound-level-only sources with CONFIG_APP_SOUND_LEVEL_TRIGGERS")
        sound_block = ""
    else:
        sound_block = sound_block_match.group(1)
    for source in SOUND_LEVEL_ONLY_APP_SRCS:
        if not cmake_block_has_source(sound_block, source):
            errors.append(f"sound-level config must link required source {source}")

    cmake_without_audio_blocks = cmake_text
    for match in (common_audio_block_match, sound_block_match):
        if match:
            cmake_without_audio_blocks = cmake_without_audio_blocks.replace(match.group(0), "")
    for source in SOUND_LEVEL_APP_SRCS + FORBIDDEN_DEFAULT_AUDIO_APP_SRCS:
        if cmake_block_has_source(cmake_without_audio_blocks, source):
            errors.append(f"app component must not link audio source outside audio config {source}")
    for source in FORBIDDEN_DEFAULT_AUDIO_APP_SRCS:
        if cmake_block_has_source(common_audio_block, source) or cmake_block_has_source(sound_block, source):
            errors.append(f"audio config must not link unsupported audio source {source}")

    i2s_text = BOARD_I2S.read_text(encoding="utf-8")
    if "profile == BOARD_AUDIO_PROFILE_CAPTURE_ONLY) ? NULL : &s_tx_handle" not in i2s_text:
        errors.append("board_i2s.c must avoid allocating TX for capture-only sound input")
    if "BOARD_AUDIO_PROFILE_PLAYBACK_ONLY" not in i2s_text or "BOARD_I2S_DO_IO" not in i2s_text:
        errors.append("board_i2s.c must make TX explicit for playback-only speaker profile")
    if "profile == BOARD_AUDIO_PROFILE_PLAYBACK_ONLY) ? NULL : &s_rx_handle" not in i2s_text:
        errors.append("board_i2s.c must avoid allocating RX for playback-only speaker output")

    es_text = ES8311.read_text(encoding="utf-8")
    es_header_text = ES8311_HEADER.read_text(encoding="utf-8")
    if "supported: 8000, 16000, 48000" in es_header_text:
        errors.append("es8311.h must not advertise unsupported sample rates")
    if "supported by this firmware: 16000" not in es_header_text:
        errors.append("es8311.h must document that only 16 kHz is currently supported")
    if "ES8311_PROFILE_ADC_ONLY" not in es_text:
        errors.append("es8311.c must implement ADC-only profile")
    if "ES8311_PROFILE_DAC_ONLY" not in es_text:
        errors.append("es8311.c must implement DAC-only profile for speaker actions")
    if "ES8311_SYSTEM12_DAC_DOWN" not in es_text or "es8311_mute(i2c_num, i2c_addr, true)" not in es_text:
        errors.append("ADC-only codec profile must keep DAC powered down/muted")

    speaker_text = ACTION_SPEAKER.read_text(encoding="utf-8")
    rule_types_text = (ROOT / "src" / "rules" / "rule_types.h").read_text(encoding="utf-8")
    if "RULE_SPEAKER_MAX_VOLUME_PERCENT" not in speaker_text:
        errors.append("action_speaker.h must share the rule-level below-75% speaker volume cap")
    if "RULE_SPEAKER_MAX_VOLUME_PERCENT 74u" not in rule_types_text:
        errors.append("rule_types.h must keep speaker volume below the official 75% battery notice")

    power_text = BOARD_AUDIO_POWER.read_text(encoding="utf-8")
    if "BOARD_M5PM1_SPK_AMP_GPIO 3u" not in power_text or "m5pm1_gpio_set_output" not in power_text:
        errors.append("board_audio_power.c must control source-backed M5PM1 PYG3 for speaker enable")
    if "ESP_ERR_NOT_SUPPORTED" not in power_text or "speaker amplifier pulse/gain control is not implemented" not in power_text:
        errors.append("unsupported AW8737 pulse/gain helper must remain fail-closed")

    if errors:
        print("Audio-safety validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Audio-safety validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

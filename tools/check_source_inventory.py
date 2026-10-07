#!/usr/bin/env python3
"""Validate that every src C implementation file has an explicit build status."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "src"
CMAKE = SRC_DIR / "CMakeLists.txt"
HOST_RUNNER = ROOT / "tests" / "host" / "run_host_tests.sh"
SDKCONFIG_DEFAULTS = ROOT / "config" / "sdkconfig.defaults"
DOCS = ROOT / "docs" / "README.md"
INVENTORY_DOC = ROOT / "docs" / "implementation_inventory.md"

# Source files that intentionally remain outside unconditional APP_SRCS.
# Shared audio files are conditionally linked by the sound-level trigger or
# speaker-action Kconfig gates, both of which are enabled in the checked-in
# StickS3 defaults. Sound-level-only files remain behind
# CONFIG_APP_SOUND_LEVEL_TRIGGERS; helper audio sources stay out of the default
# app component.
SHARED_AUDIO_SRCS = {
    "board_audio.c",
    "board_audio_clock.c",
    "board_audio_power.c",
    "board_i2s.c",
    "es8311.c",
}
SOUND_LEVEL_ONLY_SRCS = {
    "audio_metrics.c",
    "sound_level_service.c",
}
SOUND_LEVEL_SRCS = SHARED_AUDIO_SRCS | SOUND_LEVEL_ONLY_SRCS
HELPER_AUDIO_SRCS = {
    "audio_pipeline.c",
    "audio_resample.c",
}
CONDITIONAL_TRANSPORT_SRCS = {
    "transport_ble_gatt.c",
}
UAC_SRCS = {
    "uac_audio_buffer.c",
    "uac_config.c",
    "uac_device_adapter.c",
    "uac_esp_device.c",
    "uac_mic_source.c",
    "uac_speaker_sink.c",
    "uac_service.c",
}
EXPECTED_NON_DEFAULT_SRCS = SOUND_LEVEL_SRCS | HELPER_AUDIO_SRCS | CONDITIONAL_TRANSPORT_SRCS | UAC_SRCS

# Generated C assets may be linked by the app component but intentionally stay
# outside the one-row-per-src/**/*.c implementation inventory. Keep this list
# explicit so generated assets do not skew source inventory counts.
GENERATED_CMAKE_SRCS = {
    "webui_assets.c",
}


def quoted_sources(text: str) -> set[str]:
    return {Path(match).name for match in re.findall(r'"([^"\n]+\.c)"', text)}


def cmake_default_sources(text: str) -> set[str]:
    match = re.search(r"set\(APP_SRCS(?P<body>.*?)\)\s*set\(", text, flags=re.S)
    if not match:
        return set()
    return quoted_sources(match.group("body"))


def inventory_rows(text: str) -> dict[str, tuple[str, str]]:
    rows: dict[str, tuple[str, str]] = {}
    for line in text.splitlines():
        if not line.startswith("| `"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 4:
            continue
        source = cells[0].strip("`")
        rows[source] = (cells[1], cells[2])
    return rows


def main() -> int:
    errors: list[str] = []
    all_src = {path.name for path in SRC_DIR.rglob("*.c")}
    cmake_text = CMAKE.read_text(encoding="utf-8")
    default_sources = cmake_default_sources(cmake_text)
    generated_default_sources = default_sources & GENERATED_CMAKE_SRCS
    src_default_sources = default_sources & all_src
    conditional_sources = (quoted_sources(cmake_text) - default_sources) & all_src
    host_sources = quoted_sources(HOST_RUNNER.read_text(encoding="utf-8")) & all_src
    sdkconfig_defaults = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")
    sound_enabled_by_default = "CONFIG_APP_SOUND_LEVEL_TRIGGERS=y" in sdkconfig_defaults
    speaker_enabled_by_default = "CONFIG_APP_SPEAKER_ACTION=y" in sdkconfig_defaults
    docs_text = DOCS.read_text(encoding="utf-8")
    inventory = inventory_rows(INVENTORY_DOC.read_text(encoding="utf-8"))

    missing_doc_rows = all_src - set(inventory)
    if missing_doc_rows:
        errors.append(f"docs/implementation_inventory.md missing source rows: {sorted(missing_doc_rows)}")
    extra_doc_rows = set(inventory) - all_src
    if extra_doc_rows:
        errors.append(f"docs/implementation_inventory.md references unknown sources: {sorted(extra_doc_rows)}")

    for source in sorted(all_src & set(inventory)):
        if source in src_default_sources:
            expected_status = "default"
        elif source in SHARED_AUDIO_SRCS and source in conditional_sources and (sound_enabled_by_default or speaker_enabled_by_default):
            expected_status = "default via sound/speaker config"
        elif source in SOUND_LEVEL_ONLY_SRCS and source in conditional_sources and sound_enabled_by_default:
            expected_status = "default via sound config"
        elif source in conditional_sources:
            expected_status = "conditional"
        else:
            expected_status = "helper-only"
        expected_host = "yes" if source in host_sources else "no"
        actual_status, actual_host = inventory[source]
        if actual_status != expected_status:
            errors.append(
                f"docs/implementation_inventory.md status mismatch for {source}: expected {expected_status}, found {actual_status}"
            )
        if actual_host != expected_host:
            errors.append(
                f"docs/implementation_inventory.md host coverage mismatch for {source}: expected {expected_host}, found {actual_host}"
            )

    unknown_default = default_sources - all_src - GENERATED_CMAKE_SRCS - {"cJSON.c"}
    if unknown_default:
        errors.append(f"src/CMakeLists.txt references missing sources: {sorted(unknown_default)}")

    missing_from_inventory = all_src - src_default_sources - conditional_sources - EXPECTED_NON_DEFAULT_SRCS
    if missing_from_inventory:
        errors.append(f"src sources lack explicit build status: {sorted(missing_from_inventory)}")

    unexpected_non_default = (all_src - src_default_sources - conditional_sources) - EXPECTED_NON_DEFAULT_SRCS
    if unexpected_non_default:
        errors.append(f"unexpected non-default sources: {sorted(unexpected_non_default)}")

    helper_audio_linked_by_default = HELPER_AUDIO_SRCS & src_default_sources
    if helper_audio_linked_by_default:
        errors.append(f"helper-only audio sources linked by default: {sorted(helper_audio_linked_by_default)}")

    missing_sound_conditionals = SOUND_LEVEL_SRCS - conditional_sources
    if missing_sound_conditionals:
        errors.append(f"sound-level/shared audio sources not represented in CMake conditionals: {sorted(missing_sound_conditionals)}")

    missing_conditionals = CONDITIONAL_TRANSPORT_SRCS - conditional_sources
    if missing_conditionals:
        errors.append(f"conditional transport sources not represented in CMake conditionals: {sorted(missing_conditionals)}")

    missing_uac_conditionals = UAC_SRCS - conditional_sources
    if missing_uac_conditionals:
        errors.append(f"UAC sources not represented in CMake conditionals: {sorted(missing_uac_conditionals)}")

    # Host checks are the executable evidence for helper-only C modules that can
    # run off-device. Keep this list explicit so a helper source cannot be added
    # without either host coverage or a conscious inventory update.
    host_expected = ((SOUND_LEVEL_SRCS | HELPER_AUDIO_SRCS | UAC_SRCS) - {"board_audio_power.c", "uac_esp_device.c", "uac_service.c"})
    missing_host = host_expected - host_sources
    if missing_host:
        errors.append(f"optional helper sources missing host-test compilation: {sorted(missing_host)}")

    inventory_text = INVENTORY_DOC.read_text(encoding="utf-8")
    for phrase in ["Runtime capture starts only while shared demand is active", "Maintainers can still disable the feature through Kconfig"]:
        if phrase not in docs_text:
            errors.append(f"docs/README.md missing implementation-status phrase: {phrase!r}")
    if "one row per C implementation file" not in inventory_text:
        errors.append("docs/implementation_inventory.md must state that every C implementation file is inventoried")

    if errors:
        print("Source-inventory validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print(
        "Source-inventory validation passed: "
        f"{len(src_default_sources)} src defaults, {len(conditional_sources)} src conditionals, "
        f"{len(EXPECTED_NON_DEFAULT_SRCS - conditional_sources)} helper-only sources classified, "
        f"{len(generated_default_sources)} generated app asset(s) excluded from inventory"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

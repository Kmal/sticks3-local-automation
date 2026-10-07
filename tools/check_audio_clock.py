#!/usr/bin/env python3
"""Validate that the documented StickS3 audio clock profile stays synchronized."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "board" / "board_sticks3.h"
CLOCK = ROOT / "src" / "audio" / "board_audio_clock.c"
DOCS = [ROOT / "docs" / "README.md", ROOT / "docs" / "hardware" / "sticks3" / "sticks3_mic.md"]

EXPECTED = {
    "BOARD_I2S_SAMPLE_RATE": "16000",
    "BOARD_I2S_MCLK_HZ": "12288000",
    "BOARD_I2S_BCLK_HZ": "1024000",
}


def main() -> int:
    errors: list[str] = []
    header = HEADER.read_text(encoding="utf-8")
    for name, value in EXPECTED.items():
        if not re.search(rf"#define\s+{name}\s+{value}\b", header):
            errors.append(f"{HEADER.relative_to(ROOT)} missing {name}={value}")

    numeric = {name: int(value) for name, value in re.findall(r"#define\s+(BOARD_I2S_\w+)\s+(\d+)\b", header)}
    if numeric.get("BOARD_I2S_BCLK_HZ") != numeric.get("BOARD_I2S_SAMPLE_RATE", 0) * numeric.get("BOARD_I2S_FRAME_SLOTS", 0) * numeric.get("BOARD_I2S_SLOT_BITS", 0):
        errors.append("BCLK must equal sample rate * standard frame slots * slot bits")
    clock_text = CLOCK.read_text(encoding="utf-8")
    for needle in ["fixed_mclk_authoritative = true", "mclk_multiple_for_driver = 768", "0x40"]:
        if needle not in clock_text:
            errors.append(f"{CLOCK.relative_to(ROOT)} missing {needle!r}")

    for doc in DOCS:
        text = doc.read_text(encoding="utf-8")
        for needle in ["12.288 MHz", "16 kHz", "fixed MCLK"]:
            if needle not in text:
                errors.append(f"{doc.relative_to(ROOT)} missing audio-clock fact {needle!r}")

    if errors:
        print("Audio-clock validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Audio-clock validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

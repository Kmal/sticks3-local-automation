#!/usr/bin/env python3
"""Reject stale firmware/product names after the local-automation rename."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRS = {".git", "build", "managed_components", "vendor", "__pycache__", ".pytest_cache"}
TEXT_SUFFIXES = {
    "",
    ".c",
    ".h",
    ".json",
    ".md",
    ".py",
    ".sh",
    ".txt",
    ".yml",
    ".yaml",
    ".csv",
}

FORBIDDEN_NAMES = [
    "m5sticks3_" + "bluetooth" + "_mic",
    "bluetooth" + "_mic",
    "sticks3-ble-gatt-" + "pcm-build",
    "sticks3-ble-gatt-" + "automation-build",
    "Build StickS3 BLE GATT " + "automation firmware",
    "OS-native " + "microphone",
    "Classic Bluetooth " + "HF" + "P " + "microphone",
    "Classic Bluetooth " + "HF" + "P " + "audio endpoint",
    "leg" + "acy " + "meter " + "UI path",
    "APP_TRANSPORT_" + "HF" + "P" + "_LEGACY",
    "transport_" + "hf" + "p_" + "leg" + "acy",
    "HF" + "P " + "com" + "pat" + "ibility",
    "com" + "pat" + "ibility " + "source",
    "com" + "pat" + "ibility " + "Kconfig",
    "esp_" + "com" + "pat",
    "FULL_" + "DUPLEX",
    "full-" + "duplex",
]

REQUIRED_TEXT = {
    ROOT / "CMakeLists.txt": ["project(m5sticks3_local_automation_app)"],
    ROOT / "tools" / "make_factory_image.py": ["m5sticks3_local_automation.bin"],
    ROOT / ".github" / "workflows" / "build.yml": [
        "Build StickS3 local automation firmware",
        "sticks3-local-automation-build",
        "m5sticks3_local_automation.bin",
        "m5sticks3_local_automation_app.bin",
    ],
    ROOT / "docs" / "README.md": [
        "m5sticks3_local_automation.bin",
        "m5sticks3_local_automation_app.bin",
        "OS-native Bluetooth audio endpoint",
    ],
}


def iter_repo_text_files() -> list[Path]:
    paths: list[Path] = []
    for path in ROOT.rglob("*"):
        if path.relative_to(ROOT).parts[0].startswith("build-"):
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue
        if not path.is_file() or path == Path(__file__).resolve():
            continue
        if path.suffix not in TEXT_SUFFIXES:
            continue
        paths.append(path)
    return sorted(paths)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    errors: list[str] = []
    for path in iter_repo_text_files():
        text = read_text(path)
        for forbidden in FORBIDDEN_NAMES:
            if forbidden in text:
                errors.append(f"{path.relative_to(ROOT)} contains stale name {forbidden!r}")

    for path, required_items in REQUIRED_TEXT.items():
        if not path.exists():
            errors.append(f"required naming file is missing: {path.relative_to(ROOT)}")
            continue
        text = read_text(path)
        for required in required_items:
            if required not in text:
                errors.append(f"{path.relative_to(ROOT)} missing required local-automation name {required!r}")

    if errors:
        print("Naming consistency validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    print("Naming consistency validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

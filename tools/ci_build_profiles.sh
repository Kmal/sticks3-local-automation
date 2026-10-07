#!/usr/bin/env bash
# Keep quoting inside a file: esp-idf-ci-action single-quotes its command input.
set -euo pipefail

case "${1:-}" in
  disabled)
    idf.py -B build-features-disabled \
      -D "SDKCONFIG=${PWD}/build-features-disabled/sdkconfig" \
      -D 'SDKCONFIG_DEFAULTS=config/sdkconfig.defaults;config/sdkconfig.features-disabled.defaults' \
      -D IDF_TARGET=esp32s3 build
    python3 - <<'PYCONFIG'
from pathlib import Path
config = Path("build-features-disabled/config/sdkconfig.h").read_text()
for feature in ["BATTERY_FACTS", "USB_POWER_FACTS", "BMI270_FACTS", "ADC_FACTS", "SPEAKER_ACTION", "SOUND_LEVEL_TRIGGERS", "WIFI_ENABLE", "TRANSPORT_BLE_GATT_RULE_EVENTS"]:
    assert f"#define CONFIG_APP_{feature} " not in config, feature
PYCONFIG
    ;;
  uac)
    for variant in mic speaker; do
      idf.py -B "build-uac-${variant}" \
        -D "SDKCONFIG=${PWD}/build-uac-${variant}/sdkconfig" \
        -D "SDKCONFIG_DEFAULTS=config/sdkconfig.defaults;config/sdkconfig.uac-${variant}.defaults" \
        -D IDF_TARGET=esp32s3 build
      python3 - "${variant}" <<'PYCONFIG'
from pathlib import Path
import sys
variant = sys.argv[1]
config = Path(f"build-uac-{variant}/config/sdkconfig.h").read_text()
expected = {"CONFIG_APP_USB_UAC_DEVICE": 1, "CONFIG_UAC_SAMPLE_RATE": 16000,
            "CONFIG_UAC_MIC_CHANNEL_NUM": int(variant == "mic"),
            "CONFIG_UAC_SPEAKER_CHANNEL_NUM": int(variant == "speaker")}
for key, value in expected.items():
    assert f"#define {key} {value}\n" in config, (variant, key, value)
assert "#define CONFIG_APP_SOUND_LEVEL_TRIGGERS " not in config
assert "#define CONFIG_APP_SPEAKER_ACTION " not in config
PYCONFIG
    done
    ;;
  *)
    printf 'Usage: bash tools/ci_build_profiles.sh {disabled|uac}\n' >&2
    exit 2
    ;;
esac

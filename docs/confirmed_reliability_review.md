# Confirmed reliability fixes — 2026-10-07

Reviewed upstream commit `b1b48c384ec77dd04e7463ed1f76a08372d4ab57` against the sources below. These are implementation findings; host tests and builds do not establish physical hardware qualification.

| Previous finding | Confirmation | Implemented correction |
| --- | --- | --- |
| BMI270 initialization incomplete | Confirmed. Chip ID and accelerometer-register writes omitted the mandatory configuration load. | Soft reset, disable advanced power saving, wait, upload Bosch's 8,192-byte configuration in addressed 32-byte chunks, enable initialization, wait, and require successful INTERNAL_STATUS before enabling acceleration. Every bus failure propagates. |
| LCD/web save race | Confirmed. LCD read/edit/persist and subsequent runtime reload were outside the web/runtime transaction lock. | LCD saves hold the same rule mutex through reading, editing, preparation, persistence, and runtime publication. A failed commit leaves the persisted and running configurations unchanged. |
| Excess configuration allocations | Confirmed allocation overlap; device OOM remains unproven. POST reserved a full 32 KiB response alongside the request and parsed candidate. | POST returns a small acknowledgement using a 2 KiB response buffer; the browser reloads the snapshot with a separate GET. Status exposes internal free/minimum/largest-block heap, PSRAM free heap, HTTP/local/network-task stack margins, both queue depths, and rejected-action count. Request logs record allocation sizes and heap. |
| HTTP stalls local rules | Confirmed. esp_http_client_perform blocks the only action worker. | Separate bounded local and network workers, eight whole-rule jobs per lane. A job containing HTTP stays together in the network lane to retain its action ordering; local-only jobs proceed independently. Non-HTTP callbacks share a mutex across lanes to protect hardware ownership. HTTP never holds that mutex. |
| NEC repeats/supply handling incomplete | Confirmed. Repeat frames followed immediately; no EXT_5V supply check. | Encode each frame/repeat with a 110 ms period and RMT-compatible low-level padding. Legacy short RMT wait timeouts are raised to 120 ms so the period is not truncated. Refuse transmission if the measured EXT_5V rail is absent/unreadable; do not change the shared rail's direction automatically. |
| Unauthenticated/open-default setup service | Confirmed. API routes lacked authentication and blank AP passwords selected WIFI_AUTH_OPEN. | All real HTTP API routes require a rotating 16-character session code shown on the LCD, passed as X-Device-Token. Five failed requests lock the session until physically reopening Web UI. Stop clears the code and waits for HTTP handlers without holding their rule mutex. No CORS permission is granted. AP startup requires an 8–63-character WPA2 password; blank defaults cannot start an AP. |

Full JSON imports and presets intentionally replace a snapshot. The shared lock prevents overlapping persistence/runtime transactions; it does not merge independently edited browser snapshots or introduce versioned edits. Existing queued jobs retain their original immutable action configurations.

HTTP station-mode traffic is unencrypted: use a trusted network. The code is an access control, not TLS or a substitute for link security. Lockout is session-wide, so another client can cause a denial of access until Web UI is reopened. Missing/malformed codes are rejected in the browser before requests are sent; direct invalid requests still count. The public HTML shell and favicon contain no session code; every registered API request is checked before body/response allocation or side effects. An internal in-process request helper remains callable without HTTP credentials for trusted application code and host tests.

The IR supply check deliberately leaves power mode unchanged because EXT_5V is shared with Grove/HAT peripherals and defaults to input. Battery operation without a verified powered rail fails closed. This is not a complete battery IR power-management feature.

## Authoritative sources

- [Bosch BMI270 datasheet, rev. 1.6, section 4.4](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmi270-ds000.pdf): mandatory configuration upload and initialization status. The configuration bytes are vendored from [Bosch BMI270 SensorAPI](https://github.com/boschsensortec/BMI270_SensorAPI/blob/master/bmi270.c), source blob `20f27bb0aad4b9c41160759fff9dde989f4d90e9`, with the original BSD-3-Clause notice in `src/board/bmi270_config.h` and `third_party_notices.md`.
- [M5Stack StickS3 hardware documentation](https://docs.m5stack.com/en/core/StickS3): IR supply dependence on EXT_5V and shared external power direction. [M5Unified BMI270 implementation](https://github.com/m5stack/M5Unified/blob/master/src/utility/imu/BMI270_Class.inl) independently confirms the configuration-load sequence.
- [Renesas AN-1184, Remote Control IR Receiver Decoder](https://www.renesas.com/us/en/document/apn/1184-remote-control-ir-receiver-decoder): NEC leader, repeat waveform, and 110 ms repeat interval.
- [ESP-IDF v6.1 HTTP client](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_http_client.html): blocking perform operation. [Heap allocation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/mem_alloc.html): capability-specific free/minimum/largest-block measurements. [Random-number generation](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/random.html): Wi-Fi/Bluetooth entropy for the session code. [Wi-Fi API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/network/esp_wifi.html): AP authentication configuration.
- [OWASP CSRF prevention guidance](https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html): custom-header requests and same-origin/CORS restrictions. The implementation also requires the secret session code; a header name alone is not authentication.

## Validation and next move

Focused host regressions cover exact configuration upload length/addressing, failed BMI initialization, LCD transactional persistence/runtime agreement and failed-save rollback, bounded independent queue admission and mixed-job ordering, NEC payload and period, session rotation/lockout, open-AP rejection, and browser authentication headers plus POST acknowledgement/GET reload. Existing configuration, ownership, transport, Web UI, and disabled-feature tests remain in the full suite.

Next, flash a physical StickS3 and execute `hardware_qualification.md`, concentrating on cold BMI initialization/status and motion, concurrent LCD/browser edits, a slow HTTP endpoint while local rules fire, maximum configuration imports with Wi-Fi/BLE/audio active, protected AP/session lifecycle, and scoped NEC repeats with verified EXT_5V supply. Record the firmware SHA, heap minima/largest blocks, task stack margins, local latency, and rail/waveform captures. Hardware rows remain **Not run** until measurements exist.

### Completed local checks

- Full host/Web UI suite: passed, including all focused regressions.
- AddressSanitizer + UndefinedBehaviorSanitizer host suite: passed. LeakSanitizer is unavailable under the sandbox debugger, so only leak detection was disabled (`ASAN_OPTIONS=detect_leaks=0`). This does not establish leak-free operation.
- All 10 static check scripts and four tool regression scripts: passed.
- ESP-IDF v6.1 / ESP32-S3: default, features-disabled, UAC microphone, and UAC speaker builds passed, including profile assertions. The disabled profile retains an existing unused BLE-status-function warning.
- Generated Web UI: 15,781 bytes, within the 32 KiB target; generated asset freshness and diff whitespace checks passed.
- Vendored Bosch configuration matches upstream byte-for-byte: 8,192 bytes; SHA-256 `2d75e68e343a13ff99be98261dfbd99d9e8c6f267da9e3c5aeb883276ad178db`.

The second worker adds one bounded queue (7,936 bytes in the host layout), a 4,096-byte task stack, and RTOS bookkeeping. The smaller POST response saves 30,720 bytes of response allocation during each save. These are allocation/layout facts, not a measured on-device heap budget or latency guarantee. Hardware qualification must include the added persistent worker cost.

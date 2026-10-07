# StickS3 default-image hardware qualification

This runbook is the release gate for the default StickS3 local-automation image. Feature expansion is frozen until one physical StickS3 completes every required section with captured evidence. Experimental USB Audio Class modes are compile-checked separately and are not part of this default-image qualification.

## Qualification record

Complete this table for each run. Do not replace a measured value with `pass`.

| Field | Recorded value |
| --- | --- |
| Date/time (UTC) | |
| Operator | |
| Device model/revision | |
| Device serial or asset ID | |
| Git commit | |
| ESP-IDF version | |
| Factory image SHA-256 | |
| Application image SHA-256 | |
| Power source and measured voltage | |
| Logic analyzer/oscilloscope model | |
| IR receiver/test fixture | |
| GPIO/ADC fixture | |

Attach the serial log, `sdkconfig`, linker map, image hashes, instrument captures, HTTP/BLE client logs, and completed results table to the qualification record or release artifact.

## Build and flash gate

1. Start from a clean ESP-IDF v6.1 environment.
2. Run `idf.py set-target esp32s3`, `idf.py build`, and `python3 tools/make_factory_image.py` using the checked-in defaults.
3. Record the application/factory image sizes and SHA-256 hashes.
4. Erase the device and flash the merged factory image at offset `0x0`.
5. Confirm the partition table and running application identify the expected build.

**Acceptance:** the build has no errors, both images are non-empty, the factory image is larger than the application image, and a fully erased device reaches the ready UI without a reset loop.

## Required test matrix

Record pass/fail, measured values, evidence filenames, and defect links for every row.

| Area | Procedure and required measurement | Acceptance |
| --- | --- | --- |
| Cold boot | Boot after full flash erase and capture reset reason, time to ready screen, minimum free heap, and serial log. | No crash/reset loop; UI becomes responsive; no unexplained initialization error. |
| NVS recovery | Boot with empty NVS, save a rule/timezone/Wi-Fi configuration, power-cycle, then test an invalid or incompatible config blob. | Valid state survives power loss; invalid state falls back safely without a boot loop. |
| Buttons/LCD | Exercise KEY1/KEY2 short, double, and long gestures through idle, menu, scan, and keyboard screens. | Input matches the documented mapping; no stuck screen or unintended action. |
| Wi-Fi station | Connect with valid, missing, and bad credentials; record connection time and minimum free heap. | Valid credentials persist; failures remain recoverable; no leak across three reconnect cycles. |
| Setup AP | Force station failure, start the AP, connect a client, and record URL/reachability and minimum free heap. | Blank/short passwords cannot start an AP; WPA2 AP is usable with the configured password and reports the expected SSID/address. |
| Web UI lifecycle | Enable/disable the Web UI five times; exercise every route; record heap before enable, peak use, after disable, and largest free block if available. | API requests without the LCD code return 401 without side effects; the valid code works, five invalid requests lock access, and reopening rotates the code. Record resources.internal_min/internal_largest/http_stack_margin and both queue depths while importing the maximum config with audio/Wi-Fi/BLE active. Routes behave correctly; no faster than one-second persistent polling; released heap returns within an explained tolerance. |
| BLE GATT | Connect, read status, subscribe to notifications, fire a BLE-message rule, disconnect, and reconnect. | Status and rule-event packets are valid; automation observes connection transitions. |
| Rule persistence | Configure representative sustain/cooldown and multi-action rules, reboot, and retrigger them. | Saved rules reload and preserve transition, sustain, cooldown, ordering, and fan-out behavior. Repeat both button triggers after cooldown; exercise eight matching rules with three actions each and confirm 24 ordered actions, then overload each worker and check whole-rule rejection and enqueue_errors. Keep HTTP blocked at a slow endpoint while firing local-only rules and measure local latency. Alternate LCD and browser saves, verifying persistent/runtime agreement and failure rollback. |
| GPIO | Test every allowed digital/edge fixture route and attempt at least one prohibited route; capture voltage levels and debounce observations. | Allowed facts fire once as configured; prohibited routes are rejected. |
| IR | Transmit a known NEC frame and capture carrier frequency, address/command, repeat behavior, and timing. | Capture matches configured NEC values, carrier and 110 ms repeat spacing. Absent/unreadable EXT_5V prevents transmission; a powered rail permits it without changing rail direction. |
| Microphone | Enable a sound rule and Web UI telemetry separately; capture MCLK/BCLK/WS frequencies, sample activity, noise floor, known-tone response, and clipped behavior. | Clocks match the 16 kHz profile; metrics respond monotonically; capture stops when demand is removed. |
| Speaker | Fire minimum/typical/maximum allowed tones; capture I2S clocks/output, amplifier enable duration, and post-action state. | Volume stays below 75%; amplifier disables after playback; demanded microphone capture restarts. |
| Battery | Compare firmware voltage/percentage with a calibrated meter at multiple charge levels. | Values are monotonic and error is documented; no unsupported charge-state claim is emitted. |
| External power | Exercise USB absent/present and degraded VBAT/VIN/5V read cases where fixtures permit. | Presence is asserted only with evidence; absence requires both monitored rails below threshold. |
| BMI270 | Record stationary noise and repeated known-motion trials with the default threshold. | Confirm INTERNAL_STATUS reports successful initialization after cold boot, reset, and repeated reconfiguration. No unacceptable stationary chatter; motion detection rate and latency are recorded. |
| ADC | Apply at least three known voltages to each allowed ADC fixture route and record firmware versus meter readings. | Error and divider behavior are documented; disallowed pins remain rejected. |
| Soak/coexistence | Run Wi-Fi, BLE, LCD, hardware facts, sound demand, and repeated actions together for at least one hour. | No reset, deadlock, persistent task failure, or unbounded heap decline. |

## Result table

| Test area | Result | Measurements/evidence | Defect or waiver |
| --- | --- | --- | --- |
| Build and flash | Not run | | |
| Cold boot | Not run | | |
| NVS recovery | Not run | | |
| Buttons/LCD | Not run | | |
| Wi-Fi station | Not run | | |
| Setup AP | Not run | | |
| Web UI lifecycle | Not run | | |
| BLE GATT | Not run | | |
| Rule persistence | Not run | | |
| GPIO | Not run | | |
| IR | Not run | | |
| Microphone | Not run | | |
| Speaker | Not run | | |
| Battery | Not run | | |
| External power | Not run | | |
| BMI270 | Not run | | |
| ADC | Not run | | |
| Soak/coexistence | Not run | | |

## Release decision

Qualification passes only when all required rows pass, evidence is attached, and there are no open release-blocking defects. A waiver must name its owner, rationale, scope, and expiration. Failed or unmeasured hardware behavior remains experimental, disabled, or explicitly non-productized; it must not be converted into a release claim.

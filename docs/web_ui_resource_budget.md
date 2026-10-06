# Web UI resource budget

The Web UI is an embedded-device control surface, not a general web-app host. These budgets are non-negotiable review gates for each UI redesign phase.

## Current firmware values

These values are intentionally documented before feature work so later changes are explicit:

| Resource | Current value | Source |
| --- | ---: | --- |
| `RULE_WEB_MAX_BODY` | 2,048 bytes | `src/rules/rule_web.c` |
| `RULE_WEB_MAX_RESPONSE` | 16,384 bytes | `src/rules/rule_web.c` |
| `httpd_config_t.max_uri_handlers` | 17 | `rule_web_start()` |
| `httpd_config_t.stack_size` | 8,192 bytes | `rule_web_start()` |

The current route table already consumes the 17 registered URI-handler slots, so Phase 1 ships as a single generated HTML document instead of adding CSS/JS asset routes. The HTTP server is not a boot-time resident service: the LCD Web UI Wi-Fi/AP entry flows start it only after network connectivity is available, and backing out of the Web UI result/URL screens stops it so the HTTP server task, stack, handler table, and heap allocations are released.

## Accepted budgets

| Budget | Target | Hard ceiling | Enforcement |
| --- | ---: | ---: | --- |
| Added firmware ROM for generated Web UI assets | < 32 KiB | 64 KiB | `tools/check_web_ui_budget.py` |
| Generated single-document asset | Fit in existing response cap for host tests | 16,384 bytes | `tools/check_web_ui_budget.py` |
| Initial HTML shell source after minification, excluding inlined CSS/JS | < 8 KiB | 8 KiB | `tools/check_web_ui_budget.py` |
| CSS source before minification | < 12 KiB | 12 KiB | `tools/check_web_ui_budget.py` |
| JavaScript source before minification | < 25 KiB | 25 KiB | `tools/check_web_ui_budget.py` |
| Response buffer size | keep `RULE_WEB_MAX_RESPONSE` at 16 KiB | 16 KiB until reviewed | `tools/check_web_ui_budget.py` |
| Request body size | bounded first-rule settings, up to 2,047 payload bytes | 2,048 bytes including NUL | `tools/check_web_ui_budget.py` |
| Peak additional heap during normal page load | < 4 KiB while service is enabled; 0 KiB web-server heap after Web UI exit | 8 KiB | code review + hardware measurement |
| Peak additional heap during save/import | < 8 KiB | 16 KiB | code review + hardware measurement |
| Persistent background polling | no faster than 1 second | 1 second minimum interval | code review |


## Current Phase 1 measurements

Measured by `webui/build_webui.py --check` and `tools/check_web_ui_budget.py` for the extracted, behavior-equivalent single-document UI:

| Measurement | Current value | Budget status |
| --- | ---: | --- |
| Generated `webui_index_html` asset | 15,089 bytes | Under 32 KiB target |
| Minified HTML shell source, excluding injected CSS/JS | 6,149 bytes | Under 8 KiB hard ceiling |
| CSS source before minification | 1,921 bytes | Under 12 KiB hard ceiling |
| JavaScript source before minification | 7,254 bytes | Under 25 KiB hard ceiling |
| URI handlers added for UI assets | 0 | Preserves existing 17-handler table |

On ESP-IDF, `GET /` sends the generated const asset directly with `httpd_resp_send()` and does not allocate the `RULE_WEB_MAX_RESPONSE` heap buffer used by JSON/API routes. Host tests still exercise `rule_web_handle_request()` by copying the generated asset into the caller-provided test buffer.

## Correctness patch resource changes

The request cap increased from 512 to 2,048 bytes so an exported first-rule snapshot, including escaped names and URLs, can be imported through the real HTTP handler. Redundant nested rule summaries were removed. This remains a bounded first-rule editor; full multi-rule import/export and `/api/config/op` remain future work. HTTP requests allocate only their actual body length plus a terminator, GET requests allocate no body, and oversized POSTs are rejected before allocation. The response cap, route count, and HTTP task stack remain unchanged.

The action queue still has eight slots, but each slot now stores one whole-rule batch of up to three events. This admits a maximum-size eight-rule/24-action burst and rejects a full rule batch atomically. Queue storage grows from `8 * sizeof(rule_event_t)` to `8 * sizeof(rule_event_batch_t)`; the verified ESP32-S3 build measures 328 bytes per event and 988 bytes per batch, so queue payload grows from 2,624 to 7,904 bytes (+5,280 bytes). Firmware heap and stack measurements must include that increase. Engine scratch space stays bounded to one three-action batch, rather than a 24-event stack array.

The heap targets above remain qualification targets, not measured claims. The existing 16 KiB API response allocation already exceeds the normal-page-load heap ceiling. Save/import also allocates an `automation_config_t`. These costs and the larger action queue require real-device measurements before a release claim; the static budget checker validates constants/assets, not peak heap.

## Design rules

- No frontend framework or runtime CSS framework.
- Prefer generated `const` assets stored in flash/ROM.
- Do not duplicate `automation_config_t` as a second long-lived Web UI model.
- Keep temporary parse/import buffers short-lived.
- Prefer existing APIs; add aggregate or operation APIs only when bounded payloads require them.
- Prefer manual refresh for expensive diagnostics, Wi-Fi scans, and hardware probes.
- Keep the HTTP server scoped to the explicit Web UI session: default disabled after boot, enable only from Web UI Wi-Fi/AP mode once connected, and disable on exit to release RAM.
- Keep short browser form controls center-aligned for phone readability; keep textarea/status/log surfaces left-aligned.
- Any change that raises `RULE_WEB_MAX_BODY`, `RULE_WEB_MAX_RESPONSE`, route count, or generated asset size must update this document and justify the impact.

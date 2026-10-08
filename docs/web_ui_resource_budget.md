# Web UI resource budget

The Web UI is an embedded-device control surface, not a general web-app host. These budgets are non-negotiable review gates for each UI redesign phase.

## Current firmware values

These values are intentionally documented before feature work so later changes are explicit:

| Resource | Current value | Source |
| --- | ---: | --- |
| `RULE_WEB_MAX_BODY` | 32,768-byte allocation cap; payload must be smaller | `src/rules/rule_web.c` |
| `RULE_WEB_MAX_RESPONSE` | 32,768 bytes for configuration routes | `src/rules/rule_web.c` |
| `httpd_config_t.max_uri_handlers` | 17 | `rule_web_start()` |
| `httpd_config_t.stack_size` | 8,192 bytes | `rule_web_start()` |

The current route table consumes the 17 registered URI-handler slots. The redesigned UI remains a single generated HTML document, with CSS and JavaScript inlined. At build time Python gzip compresses the document. ESP-IDF compiles only the compressed const array; host tests compile the plain fixture. The root handler sends the gzip bytes directly from flash with `Content-Encoding: gzip`; decompression happens in the browser. The HTTP server is not a boot-time resident service: the LCD Web UI Wi-Fi/AP entry flows start it only after network connectivity is available, and backing out of the Web UI result/URL screens stops it so the HTTP server task, stack, handler table, and heap allocations are released.

### Internal RAM for deferred HTTP startup

`ESP_ERR_HTTPD_TASK` means ESP-IDF failed to create the HTTP task; no HTTP listener remains after that failure. Its 8,192-byte stack stays in internal RAM because configuration handlers write NVS and flash operations can disable the PSRAM cache. The 8 MiB PSRAM total in the boot log does not establish that a sufficiently large internal block is available.

The StickS3 defaults prefer PSRAM for ordinary allocations above 1,024 bytes, reserve 98,304 bytes of internal memory for internal/DMA allocations, and place NimBLE host dynamic allocations in PSRAM through ESP-IDF's supported allocation mode. This preserves headroom alongside the LCD's 64,800-byte internal DMA framebuffer and the Wi-Fi/BLE/runtime tasks; it does not allocate a resident HTTP stack while the Web UI is disabled. The reserve is shared by internal/DMA users, not a dedicated HTTP pool, so physical measurements remain necessary.

HTTP startup and failures log internal free bytes, the largest free internal block, the minimum free internal heap, and PSRAM free bytes. A task needs a contiguous stack allocation plus internal task metadata; free PSRAM cannot satisfy this requirement. On hardware, verify boot, BLE operation, Web UI open/close/reopen, configuration save/import, and startup while audio capture is active.

Existing `sdkconfig` files retain their previous values when defaults change. For an existing build, set `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=98304`, and select `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y` in menuconfig, or use a fresh build configuration loaded from `config/sdkconfig.defaults`. Rebuild and flash the firmware before retesting.

## Accepted budgets

| Budget | Target | Hard ceiling | Enforcement |
| --- | ---: | ---: | --- |
| Added firmware ROM for generated Web UI assets | < 32 KiB | 64 KiB | `tools/check_web_ui_budget.py` |
| Compressed single-document flash asset | < 8 KiB | 16,384 bytes | `tools/check_web_ui_budget.py` |
| Decoded HTML / plain host fixture | Fit in existing 32 KiB response cap | 32,767 bytes | `tools/check_web_ui_budget.py` |
| Initial HTML shell source after minification, excluding inlined CSS/JS | < 8 KiB | 8 KiB | `tools/check_web_ui_budget.py` |
| CSS source before minification | < 12 KiB | 12 KiB | `tools/check_web_ui_budget.py` |
| JavaScript source before minification | < 25 KiB | 25 KiB | `tools/check_web_ui_budget.py` |
| Response buffer size | 2 KiB for ordinary JSON routes; 16 KiB for capabilities | 32 KiB for complete configuration export | `tools/check_web_ui_budget.py` |
| Request body size | 511-byte payload for ordinary routes | 32,767-byte payload for `/api/config` | `tools/check_web_ui_budget.py` |
| Peak additional heap during normal page load | < 4 KiB while service is enabled; 0 KiB web-server heap after Web UI exit | 8 KiB | code review + hardware measurement |
| Peak additional heap during save/import | Release all temporary buffers before returning | Bounded by body, response, one candidate configuration, and JSON node/string limits; physical measurement pending | code review + hardware measurement |
| Persistent background polling | no faster than 1 second | 1 second minimum interval | code review |


## Current redesign measurements

Measured by `webui/build_webui.py --check`, `tools/check_web_ui_budget.py`, and compiled asset round-trip tests:

| Measurement | Current value | Budget status |
| --- | ---: | --- |
| Firmware gzip `webui_index_html` payload | 9,917 bytes | Above 8 KiB target, within 16 KiB ceiling; 37.4% smaller than the prior 15,855-byte page |
| Decoded HTML / host fixture | 32,754 bytes | Under existing 32 KiB host response cap |
| Minified HTML shell source, excluding injected CSS/JS | 8,144 bytes | Under 8 KiB hard ceiling |
| CSS source before minification | 8,520 bytes | Under 12 KiB hard ceiling |
| JavaScript source before minification | 16,409 bytes | Under 25 KiB hard ceiling |
| URI handlers added for UI assets | 0 | Preserves existing 17-handler table |

The richer layout increases decoded document size, so the plain host fixture ceiling moves from 16 KiB to the existing 32 KiB cap. Only the host HTML test uses a larger caller-owned buffer; ordinary JSON test buffers and every device API allocation limit stay unchanged. Firmware contains no plain duplicate, gzip library, or decompression buffer. The ESP32-S3 ELF confirms the compressed array resides in `.rodata`. The compressed payload excludes its one-byte terminator and four-byte length constant.

The two-section revision adds listing, creation, and editing of any saved rule, plus a separate native dialog. That functionality raises the compressed page above the aspirational 8 KiB target without changing its 16 KiB ceiling. The main list reads `/api/config?view=list` through the existing configuration handler with a 2 KiB response allocation. It returns only rule identity, name, enabled state, trigger source, first action type, and action count, never credentials or action payloads. Display-only control characters in names become spaces to bound JSON expansion; the full configuration retains original names. Host tests verify eight maximum-length names fit, including quotes, backslashes, and control characters. There is no second long-lived firmware configuration model. On phones, the existing navigation becomes a native browser popover drawer with manual open/close, Escape and outside-tap dismissal. CSS transitions slide the drawer in and out over 240 ms and fade its backdrop; reduced-motion preferences disable these transitions. It adds no dependency, firmware allocation, HTTP handler, or polling.

On ESP-IDF, `GET /` sends the generated compressed const asset directly with `httpd_resp_send()` and does not allocate the `RULE_WEB_MAX_RESPONSE` heap buffer used by JSON/API routes. Host tests still exercise `rule_web_handle_request()` by copying the generated asset into the caller-provided test buffer.

## Browser request policy

No API requests occur before pairing. Unlock on the default Automations page reads only `/api/config?view=list`. New automation or selecting a saved entry loads capabilities and the full configuration into the browser before opening the editor. Saving patches the selected rule and first action in that snapshot, preserving other rules, additional actions, and masked credentials. Settings reads Wi-Fi status; time and diagnostics load when their sections are opened. Full export is also fetched on explicit backup export/reload. Scans and probes remain manual. There are no background fetch intervals. Requests time out after 20 seconds, restore buttons on failure, and clear the in-memory code on HTTP 401. Browser Lock clears the code, list, cached configuration, and editor; it does not stop the device Web UI service.

The generated asset tests compile both preprocessor branches, validate gzip decompression against the document, and enforce the compressed page ceiling. Browser validation uses API fixtures, not a physical device. Physical minimum-heap and largest-block measurements remain required.

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

## Configuration correctness review (2026-10-06)

The old 511-byte payload limit could not accept even the firmware's own default export. The configuration API now exports all eight possible rules and all three actions per rule; importing that export preserves HTTP bearer tokens through an explicit `masked` marker tied to the existing rule ID and action slot. A new action cannot import a masked secret that does not exist locally. `empty` or an empty string clears a token. Legacy flat form submissions update the first rule/action while retaining the remaining rules/actions.

Only complete `/api/config` GET exports allocate the 32 KiB response. The list query and ordinary JSON responses use 2 KiB; capabilities retain 16 KiB. Request allocation uses the actual body length plus its terminator. The JSON parser is upstream cJSON 1.7.19, with a nesting limit of 16, a node limit of 1,024, duplicate-key rejection, and checked strings/integer ranges. Parser storage is released before applying a configuration. Applying and saving temporarily holds one candidate configuration. GPIO preparation precedes the persistence callback; failed persistence restores hardware ownership and leaves the engine, counters, and event sequence unchanged. Heap allocation failure returns an error.

A full import may therefore exceed the former 16 KiB temporary-heap ceiling. These explicit bounds replace that inaccurate ceiling; measure minimum free heap and the largest free block on the physical board before release. The static page still streams directly from flash, and generated asset, handler-count, HTTP-stack, and polling budgets remain unchanged.

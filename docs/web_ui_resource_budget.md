# Web UI resource budget

The Web UI is an embedded-device control surface, not a general web-app host. These budgets are non-negotiable review gates for each UI redesign phase.

## Current firmware values

These values are intentionally documented before feature work so later changes are explicit:

| Resource | Current value | Source |
| --- | ---: | --- |
| `RULE_WEB_MAX_BODY` | 32,768-byte allocation cap; payload must be smaller | `src/rules/rule_web.c` |
| `RULE_WEB_MAX_RESPONSE` | 32,768 bytes for configuration routes | `src/rules/rule_web.c` |
| `httpd_config_t.max_uri_handlers` | 19 | `rule_web_start()` |
| `httpd_config_t.stack_size` | 8,192 bytes | `rule_web_start()` |

The current route table consumes the 19 registered URI-handler slots. The redesigned UI remains a single generated HTML document, with CSS and JavaScript inlined. At build time Python gzip compresses the document. ESP-IDF compiles only the compressed const array; host tests compile the plain fixture. The root handler sends the gzip bytes directly from flash with `Content-Encoding: gzip`; decompression happens in the browser. The HTTP server is not a boot-time resident service: the LCD Web UI Wi-Fi/AP entry flows start it only after network connectivity is available, and backing out of the Web UI result/URL screens stops it so the HTTP server task, stack, handler table, and heap allocations are released.

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
| Decoded HTML / plain host fixture | < 40 KiB; host-only buffer | 40,959 bytes | `tools/check_web_ui_budget.py` |
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
| Firmware gzip `webui_index_html` payload | 10,677 bytes | Above 8 KiB target, within 16 KiB ceiling; 32.7% smaller than the prior 15,855-byte page |
| Decoded HTML / host fixture | 35,431 bytes | Under 40 KiB host-only fixture ceiling |
| Minified HTML shell source, excluding injected CSS/JS | 8,131 bytes | Under 8 KiB hard ceiling |
| CSS source before minification | 8,673 bytes | Under 12 KiB hard ceiling |
| JavaScript source before minification | 19,030 bytes | Under 25 KiB hard ceiling |
| URI handlers added for UI assets | 0 | 19 handlers total; two added for authentication |

The authentication page and session flow increase decoded document size, so the plain host fixture ceiling is 40 KiB, independently of the unchanged 32 KiB configuration API cap. Only the host HTML test uses a larger caller-owned buffer; ordinary JSON test buffers and every device API allocation limit stay unchanged. Firmware contains no plain duplicate, gzip library, or decompression buffer. The ESP32-S3 ELF confirms the compressed array resides in `.rodata`. The compressed payload excludes its one-byte terminator and four-byte length constant.

The two-section revision adds listing, creation, and editing of any saved rule, plus a separate native dialog. That functionality raises the compressed page above the aspirational 8 KiB target without changing its 16 KiB ceiling. The main list reads `/api/config?view=list` through the existing configuration handler with a 2 KiB response allocation. It returns only rule identity, name, enabled state, trigger source, first action type, and action count, never credentials or action payloads. Display-only control characters in names become spaces to bound JSON expansion; the full configuration retains original names. Host tests verify eight maximum-length names fit, including quotes, backslashes, and control characters. There is no second long-lived firmware configuration model. On phones, the existing navigation becomes a native browser popover drawer with manual open/close, Escape and outside-tap dismissal. CSS transitions slide the drawer in and out over 240 ms and fade its backdrop; reduced-motion preferences disable these transitions. It adds no dependency, firmware allocation, HTTP handler, or polling.

On ESP-IDF, `GET /` sends the generated compressed const asset directly with `httpd_resp_send()` and does not allocate the `RULE_WEB_MAX_RESPONSE` heap buffer used by JSON/API routes. Host tests still exercise `rule_web_handle_request()` by copying the generated asset into the caller-provided test buffer.

The authentication flow adds two URI handlers, increasing the reviewed table from 17 to 19 without changing the 8,192-byte server stack or any device API body/response limit. The larger decoded document affects only the browser and a 40 KiB caller-owned host-test fixture: firmware continues streaming gzip directly from flash. The four session records occupy 192 bytes, plus a 4-byte request counter; exact device RAM and handler-table heap measurements remain hardware qualification work. The existing LCD input task checks pending requests every 250 ms, outside the UI spinlock and GPIO polling task; approvals use the runtime mutex. Device stop marks the service unavailable before joining HTTP handlers, then clears sessions under that mutex.

## Browser request policy

Opening the URL shows only the authentication page. Clicking Request access sends `POST /api/auth` with body `request` and a browser-generated 32-character hex secret in `X-Device-Token`. The browser checks `GET /api/auth` once per second while pending, stopping after approval, rejection, expiry or cancellation. Device KEY1 approves only the displayed request ID; KEY2 rejects it. A second browser receives a busy response without replacing that prompt. Approved sessions occupy at most four static slots; failed authorization does not invalidate other browsers. Authentication uses an 8-byte request and 128-byte response stack buffer, with no per-request heap allocation. Each static session holds a secret, state, start time and request ID; no dynamic session list or background task is added. New requests use empty slots before recycling rejected/expired records, preserving decisions for polling browsers while space remains.

Approval opens Automations and reads only `/api/config?view=list`. The private secret is retained in session storage for tab reloads; restoring it first checks approval through `/api/auth`. No protected API is called before approval. `POST /api/auth` with body `cancel` cancels a pending request or revokes the caller's approved session. Browser Lock also clears cached configuration, password inputs and the configuration JSON textarea (which can contain raw imported credentials); service exit clears every approval. Authentication or network failures stay on the authentication page, with retry/cancel controls. HTTP 401 clears the browser session. Requests time out after 20 seconds.

Authenticated views have no periodic API polling. New automation or selecting a saved entry loads capabilities and the full configuration. Saving patches the selected rule and first action, preserving other rules, additional actions and masked credentials. Settings reads Wi-Fi status; time and diagnostics load when opened. Export/reload, scans and probes remain manual.

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

The old 511-byte payload limit could not accept even the firmware's own default export. The configuration API now exports all eight possible rules and all three actions per rule; importing that export preserves HTTP bearer tokens through an explicit `masked` marker tied to the existing rule ID and action slot. A new action cannot import a masked secret that does not exist locally. `empty` or an empty string clears a token. Only nested rule/action snapshots are accepted; flat form input, preset commands, and duplicate first-rule export fields are removed.

Only complete `/api/config` GET exports allocate the 32 KiB response. The list query and ordinary JSON responses use 2 KiB; capabilities retain 16 KiB. Request allocation uses the actual body length plus its terminator. The JSON parser is upstream cJSON 1.7.19, with a nesting limit of 16, a node limit of 1,024, duplicate-key rejection, and checked strings/integer ranges. Parser storage is released before applying a configuration. Applying and saving temporarily holds one candidate configuration. GPIO preparation precedes the persistence callback; failed persistence restores hardware ownership and leaves the engine, counters, and event sequence unchanged. Heap allocation failure returns an error.

A full import may therefore exceed the former 16 KiB temporary-heap ceiling. These explicit bounds replace that inaccurate ceiling; measure minimum free heap and the largest free block on the physical board before release. The static page still streams directly from flash, and generated asset, handler-count, HTTP-stack, and polling budgets remain unchanged.

# Web UI redesign status

This file records the code-review status of the phased Web UI redesign so reviewers can tell which parts are complete and which are still future work.

## Current shipped scope

The current implementation is a **Phase 0 + Phase 1 foundation only**:

- Resource budgets are documented and statically checked.
- The previous monolithic `s_rule_setup_page` C string has been extracted into `webui/` source files.
- `webui/build_webui.py` generates checked-in `generated/webui_assets.c` and `generated/webui_assets.h`.
- The firmware serves `GET /` from the generated const asset without allocating the large API response buffer used by JSON routes.
- The correctness patch defines `/api/config` as a first-rule settings editor that preserves other rules/actions and omitted fields; presets remain explicit replacements.
- The HTTP server lifecycle is on-demand: disabled after boot, enabled only by Web UI Wi-Fi/AP entry flows once connected, and stopped when the user exits the Web UI result/URL screens.
- Short browser input/select fields are center-aligned for phone readability; larger textarea/status surfaces stay left-aligned.
- Host validation checks generated-asset freshness and Web UI size budgets before running host tests.

## Phase checklist

| Phase | Status | Review notes |
| --- | --- | --- |
| Phase 0 — hard resource budgets | Complete for initial budgets | `docs/web_ui_resource_budget.md` documents current HTTP/body/response/route/stack values, accepted budgets, and measured Phase 1 asset sizes. |
| Phase 1 — extract current Web UI | Complete for single-document mode | The UI remains behavior-equivalent and is emitted as one generated HTML asset because the route table already uses the 17 configured URI-handler slots. |
| Phase 2 — modern visual system | Not started | No redesigned dark console layout or component vocabulary beyond the existing compact page. |
| Phase 3 — hash router | Not started | No `/#dashboard`, `/#network`, or section router exists yet. |
| Phase 4 — Dashboard command center | Not started | Current page still shows the legacy compact setup/status layout. |
| Phase 5 — guided Network provisioning | Not started | Existing Wi-Fi controls remain; no stepper-style guided redesign yet. |
| Phase 6 — Automation workspace | Not started | Current UI remains single-rule oriented and does not yet implement a multi-rule workspace. |
| Phase 7 — first-class Capabilities page | Not started | Capabilities remain exposed in existing controls/raw panels only. |
| Phase 8 — Diagnostics redesign | Not started | Raw developer tools have not been moved into a dedicated diagnostics page. |
| Phase 9 — Settings/security posture | Not started | Future auth/pairing hooks and security copy are not implemented yet. |
| Phase 10 — resource-safe full config backend | Not started | `/api/config/op` and bounded mutation operations are not implemented yet. |
| Phase 11 — performance/failure testing | Partial static only | Static asset and host checks exist; real StickS3 heap/hardware validation is still required. |

## Review outcome

The extraction preserved API behavior. The subsequent correctness patch fixes config preservation, raises the bounded request cap to 2,048 bytes for snapshot round trips, removes redundant nested summaries, and adds safe SSID rendering. The remaining risk areas before later phases are:

1. Hardware measurement is still required for true peak heap impact.
2. The UI source files are extracted but still compact because Phase 1 preserved behavior rather than redesigning.
3. Future phases must keep `tools/check_web_ui_budget.py` and this status document updated when adding routes, assets, polling, or backend APIs.

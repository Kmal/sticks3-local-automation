# Web UI redesign status

This file records the code-review status of the phased Web UI redesign so reviewers can tell which parts are complete and which are still future work.

## Current shipped scope

The current implementation is a **resource-bounded automation workspace with two section routes**:

- Resource budgets are documented and statically checked.
- The previous monolithic `s_rule_setup_page` C string has been extracted into `webui/` source files.
- `webui/build_webui.py` generates checked-in `generated/webui_assets.c` and `generated/webui_assets.h`.
- The firmware serves `GET /` from the generated const asset without allocating the large API response buffer used by JSON routes.
- The configuration API imports/exports all rules and actions with masked credentials; only nested rule/action snapshots are accepted, without flat-form or preset compatibility paths.
- The HTTP server lifecycle is on-demand: disabled after boot, enabled only by Web UI Wi-Fi/AP entry flows once connected, and stopped when the user exits the Web UI result/URL screens.
- Responsive dark surfaces, mint accents, visible keyboard focus, and mobile navigation replace the long stacked form. Short inputs remain centered; JSON and logs remain left-aligned.
- Automations is the default workspace: saved rules appear as entries, New automation opens a separate dialog, and selecting an entry edits that rule. The editor updates its first action while preserving additional actions and other rules.
- Automations and Settings use client-side hash routes without adding HTTP handlers. On mobile, a Menu button opens an off-canvas navigation drawer with a 240 ms slide-in/slide-out transition and fading backdrop (disabled for reduced motion); choosing a section closes it. Close, Escape, and outside taps also dismiss it. Mobile uses a compact icon header (hamburger and refresh), with an automation lightning icon, the same hamburger icon to dismiss the drawer, and a single footer row containing plain connection status and a lock icon. The logo and both sidebar toggles use 24 px SVG artwork and 44 px touch targets; their top edges align in the open and closed mobile layouts. Text labels remain available to assistive technology. Desktop retains the sidebar and the main workspace expands to fill the remaining viewport width with equal side padding. Settings groups network, time, backup, and diagnostics in expandable sections.
- The flash asset is reproducible gzip, served directly with no firmware decompression or page-sized RAM buffer.
- The URL opens an authentication page before the workspace. Request access displays an authorization pop-up on the StickS3: KEY1 approves the matching request and KEY2 rejects. Approval opens Automations automatically. Four browser sessions are bounded in static storage; only one approval can be pending, with a 60-second timeout. Browser Lock revokes its secret; service exit revokes all browsers. HTTP 401 restores the authentication page. Pending status checks run once per second and stop after a decision or cancellation; tab reload checks the saved session before entering the workspace.
- Authenticated views load on demand, with no background polling. Advanced GPIO settings, capabilities, raw status, and last-operation logs use progressive disclosure.
- The main list uses a credential-free metadata response within the ordinary 2 KiB buffer. Full configuration is fetched only for editing, creation, or explicit backup operations.
- Host validation checks generated-asset freshness and Web UI size budgets before running host tests.

## Phase checklist

| Phase | Status | Review notes |
| --- | --- | --- |
| Phase 0 — hard resource budgets | Complete for initial budgets | `docs/web_ui_resource_budget.md` documents current HTTP/body/response/route/stack values, accepted budgets, and current measured asset sizes. |
| Phase 1 — extract current Web UI | Complete for single-document mode | One generated HTML document. The current server registers 19 URI handlers, including the two authentication methods; code-entry authentication and legacy configuration formats are removed. |
| Phase 2 — modern visual system | Complete | Responsive dark console, mint accents, clear forms, focus states, and mobile navigation. |
| Phase 3 — hash router | Complete for current scope | `/#automations` and `/#settings`; no additional server asset routes. |
| Phase 4 — Dashboard command center | Superseded by automation workspace | Saved automations are the default working surface, per the product direction. |
| Phase 5 — guided Network provisioning | Partial | Scan/select/connect, saved credentials, and hotspot controls are reorganized. No stepper or new provisioning API. |
| Phase 6 — Automation workspace | Complete for list/create/edit scope | All saved rules listed; separate WHEN/DO dialog creates or edits a selected rule. Conditional HTTP/GPIO controls; additional actions and other settings preserved. Editing multiple actions remains future work. |
| Phase 7 — first-class Capabilities page | Partial | HAT probing and expandable capability JSON live in Settings diagnostics. |
| Phase 8 — Diagnostics redesign | Complete for existing controls | Expandable Settings diagnostics; raw JSON and operation details collapsed by default. |
| Phase 9 — Settings/security posture | Partial | Network, time, backup, and diagnostics in Settings; physical browser approval, server-side browser Lock, 401 handling, masked tokens, and destructive-action confirmations. HTTP still requires a trusted network. |
| Phase 10 — resource-safe full config backend | Partial | `/api/config?view=list` adds a bounded metadata read using the existing handler and 2 KiB response buffer. Saves still use complete snapshots; `/api/config/op` and bounded mutation operations are not implemented. |
| Phase 11 — performance/failure testing | Partial, hardware pending | Asset round trips, JS/host regressions, browser API-fixture interactions, compiled-C handler interactions and responsive checks, and default ESP32-S3 build. Real-device peak heap and hardware checks remain required. |

## Review outcome

The redesign preserves server lifecycle, stack size, and existing controls. Configuration import/export now uses only nested rule/action snapshots; duplicate first-rule export fields, flat forms, and preset commands are removed. The browser uses only Automations/Settings hash routes and named GPIO profile objects; it does not supply a fallback configuration schema version. Physical browser approval replaces manual code entry and adds two public authentication handlers (19 total). Configuration saves preserve unedited settings, the request cap is 32,768 bytes for snapshot round trips, and SSIDs render as text. The remaining risk areas before later phases are:

1. Hardware measurement is still required for true peak heap impact.
2. Browser validation includes JavaScript API fixtures and Chromium requests routed through the compiled C authentication/configuration handlers, covering approval, rejection, expiry, cancellation, Lock, reload, creation/editing, and widths from 320 to 3800 pixels. Physical approval is simulated; real Wi-Fi transitions, button/display behavior, and hardware actions require a StickS3.
3. Future phases must keep `tools/check_web_ui_budget.py` and this status document updated when adding routes, assets, polling, or backend APIs.

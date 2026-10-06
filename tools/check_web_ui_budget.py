#!/usr/bin/env python3
"""Static budget checks for generated Web UI assets and HTTP limits."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULE_WEB = ROOT / "src" / "rules" / "rule_web.c"
WEBUI = ROOT / "webui"
GENERATED = ROOT / "generated" / "webui_assets.c"

KI_B = 1024
MAX_BODY = 2048
MAX_RESPONSE = 16 * KI_B
MAX_URI_HANDLERS = 17
STACK_SIZE = 8192
ROM_TARGET = 32 * KI_B
ROM_CEILING = 64 * KI_B
HTML_SHELL_CEILING = 8 * KI_B
CSS_SOURCE_CEILING = 12 * KI_B
JS_SOURCE_CEILING = 25 * KI_B


def require_match(pattern: str, text: str, label: str) -> re.Match[str]:
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"missing {label}")
    return match


def minify_shell(text: str) -> str:
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s{2,}", " ", text)
    return text.strip()


def main() -> int:
    errors: list[str] = []
    rule_web = RULE_WEB.read_text(encoding="utf-8")

    try:
        body = int(require_match(r"#define\s+RULE_WEB_MAX_BODY\s+(\d+)u", rule_web, "RULE_WEB_MAX_BODY").group(1))
        response = int(require_match(r"#define\s+RULE_WEB_MAX_RESPONSE\s+(\d+)u", rule_web, "RULE_WEB_MAX_RESPONSE").group(1))
        handlers = int(require_match(r"config\.max_uri_handlers\s*=\s*(\d+)", rule_web, "max_uri_handlers").group(1))
        stack = int(require_match(r"config\.stack_size\s*=\s*(\d+)", rule_web, "stack_size").group(1))
    except ValueError as exc:
        errors.append(str(exc))
        body = response = handlers = stack = 0

    if body != MAX_BODY:
        errors.append(f"RULE_WEB_MAX_BODY is {body}, expected reviewed budget {MAX_BODY}")
    if response != MAX_RESPONSE:
        errors.append(f"RULE_WEB_MAX_RESPONSE is {response}, expected reviewed budget {MAX_RESPONSE}")
    if handlers != MAX_URI_HANDLERS:
        errors.append(f"max_uri_handlers is {handlers}, expected reviewed budget {MAX_URI_HANDLERS}")
    if stack != STACK_SIZE:
        errors.append(f"HTTP stack_size is {stack}, expected reviewed budget {STACK_SIZE}")

    index = WEBUI / "index.html"
    css = WEBUI / "styles.css"
    js = WEBUI / "app.js"
    for path in [index, css, js, GENERATED]:
        if not path.exists():
            errors.append(f"missing {path.relative_to(ROOT)}")

    html_shell_len = len(minify_shell(index.read_text(encoding="utf-8")).encode("utf-8")) if index.exists() else 0
    css_source_len = css.stat().st_size if css.exists() else 0
    js_source_len = js.stat().st_size if js.exists() else 0
    if html_shell_len > HTML_SHELL_CEILING:
        errors.append(f"webui/index.html shell exceeds {HTML_SHELL_CEILING} bytes after minification")
    if css_source_len > CSS_SOURCE_CEILING:
        errors.append(f"webui/styles.css source exceeds {CSS_SOURCE_CEILING} bytes")
    if js_source_len > JS_SOURCE_CEILING:
        errors.append(f"webui/app.js source exceeds {JS_SOURCE_CEILING} bytes")

    asset_len = 0
    if GENERATED.exists():
        generated = GENERATED.read_text(encoding="utf-8")
        match = re.search(r"webui_index_html_len\s*=\s*(\d+)u", generated)
        if not match:
            errors.append("generated/webui_assets.c missing webui_index_html_len")
        else:
            asset_len = int(match.group(1))
            if asset_len > ROM_CEILING:
                errors.append(f"generated Web UI asset {asset_len} bytes exceeds hard ROM ceiling {ROM_CEILING}")
            if asset_len > MAX_RESPONSE:
                errors.append(f"generated Web UI asset {asset_len} bytes exceeds host response buffer {MAX_RESPONSE}")

    if errors:
        print("Web UI budget validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    target_note = "within target" if asset_len <= ROM_TARGET else "over target but within hard ceiling"
    print(
        "Web UI budget validation passed: "
        f"asset={asset_len} bytes ({target_note}), "
        f"html_shell={html_shell_len}, css_source={css_source_len}, js_source={js_source_len}, "
        f"body={body}, response={response}, handlers={handlers}, stack={stack}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

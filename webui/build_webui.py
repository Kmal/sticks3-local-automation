#!/usr/bin/env python3
"""Build the embedded Web UI as a single static firmware asset."""

from __future__ import annotations

import argparse
import gzip
import io
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEBUI = ROOT / "webui"
GENERATED = ROOT / "generated"


def minify_html(text: str) -> str:
    text = re.sub(r"<!--(?! WEBUI_).*?-->", "", text, flags=re.S)
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s{2,}", " ", text)
    return text.strip()


def minify_css(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{}:;,>])\s*", r"\1", text)
    text = text.replace(";}", "}")
    return text.strip()


def minify_js(text: str) -> str:
    # Avoid token-aware rewrites in this dependency-light builder: line-trim only.
    # Joining with newlines preserves // comments and automatic-semicolon-insertion
    # boundaries if future edits make app.js less compact.
    lines = [line.strip() for line in text.splitlines()]
    return "\n".join(line for line in lines if line)


def c_bytes(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 12):
        chunk = data[i : i + 12]
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    body = "\n".join(rows)
    return f"const unsigned char {name}[] = {{\n{body}\n    0x00,\n}};\nconst unsigned int {name}_len = {len(data)}u;\n"


def build() -> bytes:
    html = minify_html((WEBUI / "index.html").read_text(encoding="utf-8"))
    css = minify_css((WEBUI / "styles.css").read_text(encoding="utf-8"))
    js = minify_js((WEBUI / "app.js").read_text(encoding="utf-8"))
    missing = [marker for marker in ("<!-- WEBUI_STYLE -->", "<!-- WEBUI_SCRIPT -->") if marker not in html]
    if missing:
        raise ValueError(f"webui/index.html missing injection marker(s): {', '.join(missing)}")
    html = html.replace("<!-- WEBUI_STYLE -->", f"<style>{css}</style>")
    html = html.replace("<!-- WEBUI_SCRIPT -->", f"<script>{js}</script>")
    return html.encode("utf-8")


def render_outputs(data: bytes) -> tuple[str, str]:
    header = (
        "#pragma once\n\n#include <stddef.h>\n\n"
        "extern const unsigned char webui_index_html[];\n"
        "extern const unsigned int webui_index_html_len;\n"
    )
    # Compress at build time; the browser decompresses, never the ESP32.
    # GzipFile fixes the timestamp, filename, and OS byte for reproducible assets.
    packed = io.BytesIO()
    with gzip.GzipFile(fileobj=packed, mode="wb", filename="", mtime=0) as stream:
        stream.write(data)
    source = (
        "#include \"webui_assets.h\"\n\n#ifdef ESP_PLATFORM\n"
        + c_bytes("webui_index_html", packed.getvalue())
        + "#else\n" + c_bytes("webui_index_html", data) + "#endif\n"
    )
    return header, source


def write_outputs(data: bytes) -> None:
    GENERATED.mkdir(exist_ok=True)
    header, source = render_outputs(data)
    (GENERATED / "webui_assets.h").write_text(header, encoding="utf-8")
    (GENERATED / "webui_assets.c").write_text(source, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail if generated assets are stale")
    args = parser.parse_args()
    data = build()
    if args.check:
        expected_h = (GENERATED / "webui_assets.h").read_text(encoding="utf-8") if (GENERATED / "webui_assets.h").exists() else ""
        expected_c = (GENERATED / "webui_assets.c").read_text(encoding="utf-8") if (GENERATED / "webui_assets.c").exists() else ""
        actual_h, actual_c = render_outputs(data)
        if expected_h != actual_h or expected_c != actual_c:
            raise SystemExit("generated Web UI assets are stale; rerun webui/build_webui.py")
        print(f"Web UI asset generation check passed: html={len(data)} bytes")
        return 0
    write_outputs(data)
    print(f"Generated Web UI assets: html={len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

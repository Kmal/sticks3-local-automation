#!/usr/bin/env python3
"""Verify the actual compiled flash asset and the plain host fixture."""
import gzip
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("webui_builder", ROOT / "webui/build_webui.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
expected = builder.build()
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    main = directory / "asset.c"
    main.write_text('#include <stdio.h>\n#include "webui_assets.h"\n'
                    'int main(void) { return fwrite(webui_index_html, 1, webui_index_html_len, stdout) != webui_index_html_len; }\n')
    for firmware in (False, True):
        exe = directory / ("firmware" if firmware else "host")
        flags = ["-DESP_PLATFORM"] if firmware else []
        subprocess.run([os.environ.get("CC", "cc"), *flags, "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "generated"), str(main),
                        str(ROOT / "generated/webui_assets.c"), "-o", str(exe)], check=True)
        actual = subprocess.check_output([str(exe)])
        if firmware:
            assert actual[:3] == b"\x1f\x8b\x08"
            assert gzip.decompress(actual) == expected
            assert len(actual) < 16384
            print(f"Firmware gzip round trip passed: {len(actual)} flash bytes, {len(expected)} decoded bytes")
        else:
            assert actual == expected
print("Compiled Web UI asset tests passed")

#!/usr/bin/env python3
"""Compile the actual monitor with host fakes; no Zephyr installation required."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = root / "src/disconnected_sleep.c"
with tempfile.TemporaryDirectory(prefix="disconnected-sleep-test-") as tmp:
    tmp = pathlib.Path(tmp)
    for header in re.findall(r"#include <([^>]+)>", source.read_text()):
        path = tmp / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* supplied by the host harness */\n")
    for timeout in (60000, 1000, 0):
        exe = tmp / f"test-{timeout}"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-unused-variable",
            f"-DCONFIG_ZMK_NON_LIPO_ADV_SLEEP_TIMEOUT={timeout}", f"-I{tmp}",
            str(root / "tests/power_management/test_disconnected_sleep.c"), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)

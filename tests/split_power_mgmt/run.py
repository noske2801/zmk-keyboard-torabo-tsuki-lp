#!/usr/bin/env python3
"""Compile the actual board.c against deterministic host mocks; no Zephyr SDK needed."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="split-power-test-") as tmp:
    stubs = Path(tmp)
    for header in re.findall(r"#include <([^>]+)>", (root / "src/board.c").read_text()):
        path = stubs / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()
    binary = stubs / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-I", str(stubs),
                    str(Path(__file__).with_name("test.c")), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

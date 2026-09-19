#!/usr/bin/env python3
"""Build and run bounded WebSocket binary protocol parser host tests."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "scripts/websocket_binary_protocol_parser_host_test.cc"
idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    raise SystemExit("Set IDF_PATH by sourcing ESP-IDF 5.5.2 export.sh before running this test.")
IDF_JSON = Path(idf_path) / "components/json/cJSON"
if not (IDF_JSON / "cJSON.h").is_file() or not (IDF_JSON / "cJSON.c").is_file():
    raise SystemExit("IDF_PATH must point to ESP-IDF with components/json/cJSON sources.")
BUILD_DIR = ROOT / "build/host_tests"
BIN = BUILD_DIR / "websocket_binary_protocol_parser_host_test"


def main() -> int:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    compile_cmd = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I",
        str(ROOT / "main"),
        "-I",
        str(IDF_JSON),
        str(SRC),
        "-o",
        str(BIN),
    ]
    subprocess.run(compile_cmd, cwd=ROOT, check=True)
    subprocess.run([str(BIN)], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

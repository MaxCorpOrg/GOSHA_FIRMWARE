#!/usr/bin/env python3
"""Build and run the host-only Motion Studio package manager test."""

from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "scripts/motion_package_manager_host_test.cc"
MANAGER_IMPL = ROOT / "main/boards/gosha-v1/motion_package_manager.cc"
JSON_IMPL = ROOT / "main/boards/gosha-v1/motion_package_json.cc"
PACKAGE_IMPL = ROOT / "main/boards/gosha-v1/motion_package.cc"
STORE_IMPL = ROOT / "main/boards/gosha-v1/motion_package_store.cc"
UPLOAD_IMPL = ROOT / "main/boards/gosha-v1/motion_package_upload.cc"
IDF_JSON = Path("/home/max/esp/esp-idf-v5.5.2/components/json/cJSON")
CJSON_IMPL = IDF_JSON / "cJSON.c"
BUILD_DIR = ROOT / "build/host_tests"
BIN = BUILD_DIR / "motion_package_manager_host_test"


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
        str(ROOT / "main/boards/gosha-v1"),
        "-I",
        str(IDF_JSON),
        str(MANAGER_IMPL),
        str(JSON_IMPL),
        str(PACKAGE_IMPL),
        str(STORE_IMPL),
        str(UPLOAD_IMPL),
        str(CJSON_IMPL),
        str(SRC),
        "-o",
        str(BIN),
    ]
    subprocess.run(compile_cmd, cwd=ROOT, check=True)
    subprocess.run([str(BIN)], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Verify robot movement playback and editor separation on a simulated device."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "scripts/robot_motion_runtime_host_test.cc"
HARDWARE_RUNNER_IMPL = ROOT / "main/boards/gosha-v1/motion_package_hardware_runner.cc"
RUNNER_IMPL = ROOT / "main/boards/gosha-v1/motion_package_runner.cc"
PLAYER_IMPL = ROOT / "main/boards/gosha-v1/motion_package_player.cc"
JSON_IMPL = ROOT / "main/boards/gosha-v1/motion_package_json.cc"
PACKAGE_IMPL = ROOT / "main/boards/gosha-v1/motion_package.cc"
UPLOAD_IMPL = ROOT / "main/boards/gosha-v1/motion_package_upload.cc"
CORE_IMPL = ROOT / "main/boards/gosha-v1/motion_live_core.cc"
idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    raise SystemExit("Set IDF_PATH by sourcing ESP-IDF 5.5.2 export.sh before running this test.")
IDF_JSON = Path(idf_path) / "components/json/cJSON"
if not (IDF_JSON / "cJSON.h").is_file() or not (IDF_JSON / "cJSON.c").is_file():
    raise SystemExit("IDF_PATH must point to ESP-IDF with components/json/cJSON sources.")
CJSON_IMPL = IDF_JSON / "cJSON.c"
BUILD_DIR = ROOT / "build/host_tests"
BIN = BUILD_DIR / "robot_motion_runtime_host_test"


def main() -> int:
    subprocess.run(["python3", str(ROOT / "scripts/import_legacy_motion_routines.py"), "--check"], check=True)
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
        str(ROOT / "main/boards/gosha-v1/robot_motion_runtime.cc"),
        str(ROOT / "main/boards/gosha-v1/legacy_motion_plan.cc"),
        str(ROOT / "main/boards/gosha-v1/motion_package_manager.cc"),
        str(ROOT / "main/boards/gosha-v1/motion_package_store.cc"),
        str(HARDWARE_RUNNER_IMPL),
        str(RUNNER_IMPL),
        str(PLAYER_IMPL),
        str(JSON_IMPL),
        str(PACKAGE_IMPL),
        str(UPLOAD_IMPL),
        str(CORE_IMPL),
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

#!/usr/bin/env python3
"""Build and run the right-arm MotionLive -> Otto -> Oscillator host test."""

from __future__ import annotations

import os
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_mem_available_mb() -> int | None:
    try:
        for line in Path("/proc/meminfo").read_text(encoding="utf-8").splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) // 1024
    except OSError:
        return None
    return None


def read_memory_pressure() -> str:
    try:
        return Path("/proc/pressure/memory").read_text(encoding="utf-8").strip()
    except OSError:
        return "unavailable"


def run_step(command: list[str], *, cwd: Path, timeout: int, env: dict[str, str] | None = None) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd, env=env, timeout=timeout, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build and run the right-arm MotionLive/Otto/Oscillator host test."
    )
    parser.add_argument(
        "--no-sanitize",
        action="store_true",
        help="build without ASan/UBSan; default is sanitizer-enabled and fail-closed",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    firmware = root / "firmware"
    gxx = shutil.which("g++")
    if gxx is None:
        print("g++ not found; cannot run host integration test", file=sys.stderr)
        return 1

    mem_available_mb = read_mem_available_mb()
    print(f"resource preflight: MemAvailable={mem_available_mb} MB", flush=True)
    print("resource preflight: memory pressure", flush=True)
    print(read_memory_pressure(), flush=True)

    with tempfile.TemporaryDirectory(prefix="gosha-right-arm-pwm-") as temporary_directory:
        output = Path(temporary_directory) / "right_arm_pwm_route_host_test"
        common_flags = [
            gxx,
            "-std=gnu++20",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wno-unused-variable",
            "-fpermissive",
            "-I",
            str(firmware / "scripts" / "host_stubs_motion_pwm"),
            "-I",
            str(firmware / "main" / "boards" / "gosha-v1"),
            str(firmware / "main" / "boards" / "gosha-v1" / "motion_live_core.cc"),
            str(firmware / "main" / "boards" / "gosha-v1" / "oscillator.cc"),
            str(firmware / "main" / "boards" / "gosha-v1" / "otto_movements.cc"),
            str(firmware / "scripts" / "host_stubs_motion_pwm" / "ledc_recorder.cc"),
            str(firmware / "scripts" / "right_arm_pwm_route_host_test.cc"),
            "-o",
            str(output),
        ]

        run_env = os.environ.copy()
        if args.no_sanitize:
            run_step(common_flags, cwd=firmware, timeout=40)
            print("sanitizers: disabled by explicit --no-sanitize", flush=True)
        else:
            sanitizer_flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            run_step(common_flags + sanitizer_flags, cwd=firmware, timeout=40)
            run_env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
            run_env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
            print("sanitizers: address,undefined enabled", flush=True)

        run_step([str(output)], cwd=firmware, timeout=20, env=run_env)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

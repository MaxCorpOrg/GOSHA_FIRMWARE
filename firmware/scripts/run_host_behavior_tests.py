#!/usr/bin/env python3
"""Build and run host-side behavioral regression tests."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def find_cxx() -> str | None:
    configured = os.environ.get("CXX")
    if configured:
        return configured
    for candidate in ("g++", "c++", "clang++"):
        path = shutil.which(candidate)
        if path:
            return path
    return None


def run(command: list[str]) -> None:
    print("+ " + " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    compiler = find_cxx()
    if compiler is None:
        print("[ERROR] C++ compiler was not found; host behavioral tests cannot run", file=sys.stderr)
        return 2

    out_dir = Path(tempfile.mkdtemp(prefix="gosha-host-tests-"))
    common_flags = [
        compiler,
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pthread",
        "-Imain",
    ]

    tests = [
        (
            "audio_playback_drain_host_test",
            ["scripts/audio_playback_drain_host_test.cc"],
        ),
        (
            "diagnostic_redaction_host_test",
            ["scripts/diagnostic_redaction_host_test.cc", "main/diagnostic_redaction.cc"],
        ),
    ]

    for name, sources in tests:
        output = out_dir / name
        run([*common_flags, *sources, "-o", str(output)])
        run([str(output)])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

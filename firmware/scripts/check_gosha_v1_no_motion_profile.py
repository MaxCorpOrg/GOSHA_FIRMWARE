#!/usr/bin/env python3
"""Static guard for the gosha-v1 no-motion safe profile."""

import argparse
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CONFIG_JSON = ROOT / "main/boards/gosha-v1/config.json"
KCONFIG = ROOT / "main/Kconfig.projbuild"
OTTO_CONTROLLER = ROOT / "main/boards/gosha-v1/otto_controller.cc"
OTTO_MOVEMENTS_CC = ROOT / "main/boards/gosha-v1/otto_movements.cc"
OTTO_MOVEMENTS_H = ROOT / "main/boards/gosha-v1/otto_movements.h"
RELEASE = ROOT / "scripts/release.py"

NO_MOTION_FLAG = "CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y"

DANGEROUS_TOOLS = (
    '"self.otto.action"',
    '"self.otto.servo_sequences"',
    '"self.otto.stop"',
    '"self.otto.set_trim"',
    '"self.otto.get_trims"',
)

SAFE_TOOLS = (
    '"self.otto.get_status"',
    '"self.battery.get_level"',
    '"self.otto.get_ip"',
)


class GuardError(Exception):
    """No-motion static guard failure."""


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GuardError(message)


def extract_body(source: str, pattern: str, name: str) -> str:
    match = re.search(pattern, source)
    require(match is not None, f"{name} was not found")
    brace = source.find("{", match.end() - 1)
    require(brace >= 0, f"{name} opening brace was not found")
    depth = 1
    pos = brace + 1
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    require(depth == 0, f"{name} body was not closed")
    return source[brace + 1 : pos - 1]


def protected_intervals(source: str) -> list[tuple[int, int]]:
    intervals: list[tuple[int, int]] = []
    for match in re.finditer(r"if\s*\(\s*!kNoMotionSafeProfile\s*\)\s*\{", source):
        brace = source.find("{", match.end() - 1)
        depth = 1
        pos = brace + 1
        while pos < len(source) and depth:
            if source[pos] == "{":
                depth += 1
            elif source[pos] == "}":
                depth -= 1
            pos += 1
        require(depth == 0, "if (!kNoMotionSafeProfile) block was not closed")
        intervals.append((brace, pos))
    return intervals


def inside_any(position: int, intervals: list[tuple[int, int]]) -> bool:
    return any(start <= position <= end for start, end in intervals)


def validate_config(config_json: str, kconfig: str) -> None:
    data = json.loads(config_json)
    gosha_build = next(
        (
            build
            for build in data.get("builds", [])
            if build.get("name") == "gosha-v1"
        ),
        None,
    )
    require(gosha_build is not None, "gosha-v1 build was not found in config.json")
    require(
        NO_MOTION_FLAG in gosha_build.get("sdkconfig_append", []),
        "gosha-v1 release config must enable CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    require(
        re.search(
            r"config\s+GOSHA_NO_MOTION_SAFE_PROFILE(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
            kconfig,
            flags=re.DOTALL,
        )
        is not None,
        "GOSHA_NO_MOTION_SAFE_PROFILE Kconfig symbol was not found",
    )
    body = re.search(
        r"config\s+GOSHA_NO_MOTION_SAFE_PROFILE(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
        kconfig,
        flags=re.DOTALL,
    ).group("body")
    require("depends on BOARD_TYPE_GOSHA_V1" in body, "no-motion Kconfig must depend on gosha-v1")
    require("default n" in body, "no-motion Kconfig must default to off outside explicit release config")


def validate_motion_init(movements_h: str, movements_cc: str, controller: str) -> None:
    require(
        "int right_hand = -1, bool attach_servos = true" in movements_h,
        "Otto::Init declaration must expose an explicit attach_servos flag",
    )
    init_body = extract_body(
        movements_cc,
        r"void\s+Otto::Init\s*\([^)]*attach_servos[^)]*\)",
        "Otto::Init",
    )
    require(
        "if (attach_servos)" in init_body and "AttachServos();" in init_body,
        "Otto::Init must attach servos only when attach_servos is true",
    )
    require(
        "else" in init_body and "is_otto_resting_ = true;" in init_body,
        "Otto::Init must keep the robot resting when servos are not attached",
    )

    constructor_body = extract_body(
        controller,
        r"OttoController\s*\(\s*const\s+HardwareConfig&\s+hw_config\s*\)",
        "OttoController constructor",
    )
    require(
        "!kNoMotionSafeProfile" in constructor_body,
        "OttoController must pass !kNoMotionSafeProfile to Otto::Init",
    )
    intervals = protected_intervals(constructor_body)
    for match in re.finditer(r"QueueAction\s*\(\s*ACTION_HOME\b", constructor_body):
        require(
            inside_any(match.start(), intervals),
            "boot ACTION_HOME must be guarded by if (!kNoMotionSafeProfile)",
        )


def validate_motion_entrypoints(controller: str) -> None:
    require(
        "#ifdef CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in controller
        and "constexpr bool kNoMotionSafeProfile = true;" in controller,
        "controller must derive kNoMotionSafeProfile from CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    queue_body = extract_body(
        controller,
        r"void\s+QueueAction\s*\([^)]*\)",
        "QueueAction",
    )
    require(
        "if (kNoMotionSafeProfile)" in queue_body,
        "QueueAction must fail closed when no-motion profile is active",
    )
    require(
        queue_body.find("if (kNoMotionSafeProfile)") < queue_body.find("xQueueSend"),
        "QueueAction no-motion guard must run before xQueueSend",
    )

    sequence_body = extract_body(
        controller,
        r"void\s+QueueServoSequence\s*\([^)]*\)",
        "QueueServoSequence",
    )
    require(
        "if (kNoMotionSafeProfile)" in sequence_body,
        "QueueServoSequence must fail closed when no-motion profile is active",
    )
    require(
        sequence_body.find("if (kNoMotionSafeProfile)") < sequence_body.find("xQueueSend"),
        "QueueServoSequence no-motion guard must run before xQueueSend",
    )

    intervals = protected_intervals(controller)
    for token in DANGEROUS_TOOLS:
        pos = controller.find(token)
        require(pos >= 0, f"{token} was not found")
        require(
            inside_any(pos, intervals),
            f"{token} must be registered only inside if (!kNoMotionSafeProfile)",
        )
    for token in SAFE_TOOLS:
        pos = controller.find(token)
        require(pos >= 0, f"{token} was not found")
        require(
            not inside_any(pos, intervals),
            f"{token} must remain registered in the no-motion profile",
        )


def validate_release_hook(release_py: str) -> None:
    require(
        '"scripts/check_gosha_v1_no_motion_profile.py", "--self-test"' in release_py,
        "release.py must run the gosha-v1 no-motion static guard",
    )


def validate_tree(
    config_json: str,
    kconfig: str,
    controller: str,
    movements_cc: str,
    movements_h: str,
    release_py: str,
) -> None:
    validate_config(config_json, kconfig)
    validate_motion_init(movements_h, movements_cc, controller)
    validate_motion_entrypoints(controller)
    validate_release_hook(release_py)


def validate_current_tree() -> None:
    validate_tree(
        read(CONFIG_JSON),
        read(KCONFIG),
        read(OTTO_CONTROLLER),
        read(OTTO_MOVEMENTS_CC),
        read(OTTO_MOVEMENTS_H),
        read(RELEASE),
    )


def run_self_test() -> None:
    config_json = read(CONFIG_JSON)
    kconfig = read(KCONFIG)
    controller = read(OTTO_CONTROLLER)
    movements_cc = read(OTTO_MOVEMENTS_CC)
    movements_h = read(OTTO_MOVEMENTS_H)
    release_py = read(RELEASE)

    validate_tree(config_json, kconfig, controller, movements_cc, movements_h, release_py)

    try:
        validate_tree(
            config_json.replace(f'"{NO_MOTION_FLAG}",', ""),
            kconfig,
            controller,
            movements_cc,
            movements_h,
            release_py,
        )
    except GuardError as exc:
        require("CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in str(exc), "flag negative test did not name the missing flag")
    else:
        raise GuardError("flag negative test failed: disabled release profile was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller.replace("if (!kNoMotionSafeProfile) {", "if (kNoMotionSafeProfile) {", 1),
            movements_cc,
            movements_h,
            release_py,
        )
    except GuardError as exc:
        require("ACTION_HOME" in str(exc), "boot Home negative test did not name ACTION_HOME")
    else:
        raise GuardError("boot Home negative test failed: unguarded boot Home was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc.replace("if (attach_servos) {", "if (true) {"),
            movements_h,
            release_py,
        )
    except GuardError as exc:
        require("attach_servos" in str(exc), "servo attach negative test did not name attach_servos")
    else:
        raise GuardError("servo attach negative test failed: unconditional attach was accepted")

    print("gosha-v1 no-motion profile guard self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="prove current tree passes and representative regressions fail",
    )
    args = parser.parse_args()

    try:
        if args.self_test:
            run_self_test()
        else:
            validate_current_tree()
            print("gosha-v1 no-motion profile guard passed")
    except GuardError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()

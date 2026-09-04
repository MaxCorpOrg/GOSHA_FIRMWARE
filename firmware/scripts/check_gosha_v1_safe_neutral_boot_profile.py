#!/usr/bin/env python3
"""Static fail-closed guard for the gosha-v1 maintenance neutral boot."""

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "main/boards/gosha-v1/config.json"
KCONFIG = ROOT / "main/Kconfig.projbuild"
CONTROLLER = ROOT / "main/boards/gosha-v1/otto_controller.cc"
MOVEMENTS = ROOT / "main/boards/gosha-v1/otto_movements.cc"
ROBOT = ROOT / "main/boards/gosha-v1/otto_robot.cc"
RELEASE = ROOT / "scripts/release.py"

PROFILE_FLAG = "CONFIG_GOSHA_SAFE_NEUTRAL_BOOT_PROFILE=y"
NO_MOTION_FLAG = "CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y"
BUILD_NAME = "gosha-v1-safe-neutral-boot"


class GuardError(Exception):
    """Maintenance profile contract violation."""


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


def validate_tree(
    config_text: str,
    kconfig: str,
    controller: str,
    movements: str,
    robot: str,
    release: str,
) -> None:
    config = json.loads(config_text)
    normal = next(
        (item for item in config.get("builds", []) if item.get("name") == "gosha-v1"),
        None,
    )
    maintenance = next(
        (item for item in config.get("builds", []) if item.get("name") == BUILD_NAME),
        None,
    )
    require(normal is not None, "normal gosha-v1 build is missing")
    require(maintenance is not None, "maintenance build is missing")
    require(
        PROFILE_FLAG not in normal.get("sdkconfig_append", []),
        "maintenance flag must not be enabled in the normal gosha-v1 build",
    )
    maintenance_flags = maintenance.get("sdkconfig_append", [])
    require(NO_MOTION_FLAG in maintenance_flags, "maintenance build must keep no-motion enabled")
    require(PROFILE_FLAG in maintenance_flags, "maintenance build must enable safe-neutral boot")

    match = re.search(
        r"config\s+GOSHA_SAFE_NEUTRAL_BOOT_PROFILE(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
        kconfig,
        flags=re.DOTALL,
    )
    require(match is not None, "maintenance Kconfig symbol is missing")
    kconfig_body = match.group("body")
    require("depends on BOARD_TYPE_GOSHA_V1" in kconfig_body, "maintenance profile must depend on gosha-v1")
    require("depends on GOSHA_NO_MOTION_SAFE_PROFILE" in kconfig_body, "maintenance profile must depend on no-motion")
    require("default n" in kconfig_body, "maintenance profile must default to off")

    require(
        "CONFIG_GOSHA_SAFE_NEUTRAL_BOOT_PROFILE requires CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in controller,
        "compile-time no-motion constraint is missing",
    )
    constructor = extract_body(
        controller,
        r"OttoController\s*\(\s*const\s+HardwareConfig&\s+hw_config\s*\)",
        "OttoController constructor",
    )
    for hand in ("left_hand_pin", "right_hand_pin"):
        require(
            f"kSafeNeutralBootProfile ? GPIO_NUM_NC : hw_config.{hand}" in constructor,
            f"maintenance profile must mask {hand}",
        )
    require("const bool safe_neutral_pinset" in constructor, "complete safe-neutral pinset check is missing")
    require(
        "kSafeNeutralBootProfile && safe_neutral_pinset" in constructor,
        "maintenance attach must require the complete safe-neutral pinset",
    )

    maintenance_body = extract_body(
        controller,
        r"void\s+PerformSafeNeutralBootOnce\s*\(\s*\)",
        "PerformSafeNeutralBootOnce",
    )
    require("has_complete_legs_feet_" in maintenance_body, "four-channel admission check is missing")
    require("HoldLegsFeetAtNeutral();" in maintenance_body, "lower-body neutral hold call is missing")
    require("QueueAction" not in maintenance_body, "maintenance boot must not enter the general action queue")
    require("QueueServoSequence" not in maintenance_body, "maintenance boot must not enter the sequence queue")

    hold_body = extract_body(
        movements,
        r"void\s+Otto::HoldLegsFeetAtNeutral\s*\(\s*\)",
        "Otto::HoldLegsFeetAtNeutral",
    )
    require(
        "i < kLegAndFootServoCount" in hold_body and "i < SERVO_COUNT" not in hold_body,
        "neutral hold must iterate only over legs and feet",
    )
    require("SetPosition(90);" in hold_body, "neutral hold must command 90 degrees")
    require("LEFT_HAND" not in hold_body and "RIGHT_HAND" not in hold_body, "neutral hold must not mention hand channels")
    require(
        "constexpr int kLegAndFootServoCount = RIGHT_FOOT + 1;" in movements,
        "lower-body boundary must end at RIGHT_FOOT",
    )

    robot_init = extract_body(
        robot,
        r"void\s+InitializeOttoController\s*\(\s*\)",
        "OttoRobot::InitializeOttoController",
    )
    for field in (
        "left_hand_pin",
        "right_hand_pin",
        "left_leg_pin",
        "right_leg_pin",
        "left_foot_pin",
        "right_foot_pin",
    ):
        require(
            f"control_config.{field} = GPIO_NUM_NC;" in robot_init,
            f"camera fail-closed mask is missing for {field}",
        )
    require("if (has_camera_)" in robot_init, "camera fail-closed branch is missing")

    require(
        '"scripts/check_gosha_v1_safe_neutral_boot_profile.py", "--self-test"' in release,
        "release.py must run the maintenance guard",
    )


def validate_current_tree() -> None:
    validate_tree(
        read(CONFIG),
        read(KCONFIG),
        read(CONTROLLER),
        read(MOVEMENTS),
        read(ROBOT),
        read(RELEASE),
    )


def expect_rejection(mutator, needle: str) -> None:
    values = [read(CONFIG), read(KCONFIG), read(CONTROLLER), read(MOVEMENTS), read(ROBOT), read(RELEASE)]
    mutated = mutator(values)
    try:
        validate_tree(*mutated)
    except GuardError as exc:
        require(needle in str(exc), f"negative test did not report {needle}: {exc}")
    else:
        raise GuardError(f"negative test was accepted: {needle}")


def run_self_test() -> None:
    validate_current_tree()

    def remove_no_motion(values):
        values[0] = values[0].replace(f'                "{NO_MOTION_FLAG}",\n                "{PROFILE_FLAG}"', f'                "{PROFILE_FLAG}"', 1)
        return values

    expect_rejection(remove_no_motion, "no-motion")

    def expose_right_hand(values):
        values[2] = values[2].replace(
            "kSafeNeutralBootProfile ? GPIO_NUM_NC : hw_config.right_hand_pin",
            "hw_config.right_hand_pin",
            1,
        )
        return values

    expect_rejection(expose_right_hand, "right_hand_pin")

    def widen_hold(values):
        values[3] = values[3].replace("i < kLegAndFootServoCount", "i < SERVO_COUNT", 1)
        return values

    expect_rejection(widen_hold, "legs and feet")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        run_self_test() if args.self_test else validate_current_tree()
    except (GuardError, json.JSONDecodeError, OSError) as exc:
        print(f"safe-neutral guard failed: {exc}")
        return 1
    print("safe-neutral guard passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

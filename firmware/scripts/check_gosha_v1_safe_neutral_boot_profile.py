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
LIVE_FLAG = "CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN=y"
RIGHT_ARM_FLAG = "CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN=y"
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


def kconfig_body(kconfig: str, symbol: str) -> str:
    match = re.search(
        rf"config\s+{re.escape(symbol)}(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
        kconfig,
        flags=re.DOTALL,
    )
    require(match is not None, f"{symbol} Kconfig symbol is missing")
    return match.group("body")


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
    for flag in (LIVE_FLAG, RIGHT_ARM_FLAG):
        require(flag not in normal.get("sdkconfig_append", []),
                f"normal build must not enable {flag}")
        require(flag not in maintenance.get("sdkconfig_append", []),
                f"checked-in maintenance build must not enable {flag}")
    maintenance_flags = maintenance.get("sdkconfig_append", [])
    require(NO_MOTION_FLAG in maintenance_flags, "maintenance build must keep no-motion enabled")
    require(PROFILE_FLAG in maintenance_flags, "maintenance build must enable safe-neutral boot")

    safe_body = kconfig_body(kconfig, "GOSHA_SAFE_NEUTRAL_BOOT_PROFILE")
    require("depends on BOARD_TYPE_GOSHA_V1" in safe_body, "maintenance profile must depend on gosha-v1")
    require("depends on GOSHA_NO_MOTION_SAFE_PROFILE" in safe_body, "maintenance profile must depend on no-motion")
    require("default n" in safe_body, "maintenance profile must default to off")
    right_body = kconfig_body(kconfig, "GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN")
    require("depends on GOSHA_MOTION_LIVE_LOCAL_OPT_IN" in right_body,
            "right-arm commissioning must require Live opt-in")
    require("depends on GOSHA_SAFE_NEUTRAL_BOOT_PROFILE" in right_body,
            "right-arm commissioning must require safe-neutral boot")
    require("default n" in right_body, "right-arm commissioning must default to off")

    require(
        "CONFIG_GOSHA_SAFE_NEUTRAL_BOOT_PROFILE requires CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in controller,
        "compile-time no-motion constraint is missing",
    )
    require(
        "CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN requires CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN" in controller,
        "compile-time right-arm Live constraint is missing",
    )
    constructor = extract_body(
        controller,
        r"OttoController\s*\(\s*const\s+HardwareConfig&\s+hw_config\s*\)",
        "OttoController constructor",
    )
    require(
        "kSafeNeutralBootProfile ? GPIO_NUM_NC : hw_config.left_hand_pin" in constructor,
        "maintenance profile must always mask left_hand_pin",
    )
    for token in (
        "const bool right_arm_live_pinset",
        "kMotionLiveRightArmLocalOptIn",
        "hw_config.left_hand_pin == GPIO_NUM_NC",
        "hw_config.right_hand_pin == GPIO_NUM_12",
        "right_arm_live_pinset ? hw_config.right_hand_pin : GPIO_NUM_NC",
        "const bool safe_neutral_lower_body_pinset",
        "const bool safe_neutral_pinset",
        "right_arm_live_pinset ? right_hand_pin == GPIO_NUM_12",
        ": right_hand_pin == GPIO_NUM_NC",
        "const bool attach_servos = !kNoMotionSafeProfile",
    ):
        require(token in constructor, f"right-arm safe-neutral admission is missing: {token}")
    require(
        "kSafeNeutralBootProfile && safe_neutral_pinset" not in constructor,
        "maintenance constructor must not attach any servo just because safe-neutral pinset is present",
    )

    maintenance_body = extract_body(
        controller,
        r"void\s+PerformSafeNeutralBootOnce\s*\(\s*\)",
        "PerformSafeNeutralBootOnce",
    )
    require("has_complete_legs_feet_" in maintenance_body, "four-channel admission check is missing")
    require("AttachLegsFeetServos();" in maintenance_body, "lower-body attach call is missing")
    require("HoldLegsFeetAtNeutral();" in maintenance_body, "lower-body neutral hold call is missing")
    require("AttachRightHandAtHome" not in maintenance_body, "maintenance boot must not initialize the right arm")
    require("QueueAction" not in maintenance_body, "maintenance boot must not enter the general action queue")
    require("QueueServoSequence" not in maintenance_body, "maintenance boot must not enter the sequence queue")

    attach_body = extract_body(
        movements,
        r"void\s+Otto::AttachLegsFeetServos\s*\(\s*\)",
        "Otto::AttachLegsFeetServos",
    )
    require(
        "i < kLegAndFootServoCount" in attach_body and "i < SERVO_COUNT" not in attach_body,
        "maintenance attach must iterate only over legs and feet",
    )
    require("LEFT_HAND" not in attach_body and "RIGHT_HAND" not in attach_body,
            "maintenance attach must not mention hand channels")

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
    require("LEFT_HAND" not in hold_body and "RIGHT_HAND" not in hold_body,
            "neutral hold must not mention hand channels")
    require(
        "constexpr int kLegAndFootServoCount = RIGHT_FOOT + 1;" in movements,
        "lower-body boundary must end at RIGHT_FOOT",
    )
    right_init_body = extract_body(
        movements,
        r"bool\s+Otto::AttachRightHandAtHome\s*\([^)]*\)",
        "Otto::AttachRightHandAtHome",
    )
    require("servo_pins_[LEFT_HAND] != -1" in right_init_body,
            "right-arm initializer must fail closed if the left hand is available")
    require("servo_[RIGHT_HAND].Attach" in right_init_body and
            "servo_[RIGHT_HAND].SetPosition(home_degrees)" in right_init_body,
            "right-arm initializer must attach and hold only the right hand")
    require("servo_[LEFT_HAND]" not in right_init_body,
            "right-arm initializer must not touch the left-hand servo object")

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
    require("kMotionLiveRightArmLocalOptIn" in robot_init,
            "non-camera right-arm branch must require the right-arm opt-in flag")
    require("control_config.right_hand_pin = hw_config_.right_hand_pin;" in robot_init,
            "non-camera right-arm branch must pass the owner-confirmed right-hand pin")

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
            "right_arm_live_pinset ? hw_config.right_hand_pin : GPIO_NUM_NC",
            "hw_config.right_hand_pin",
            1,
        )
        return values

    expect_rejection(expose_right_hand, "right-arm safe-neutral admission")

    def widen_attach(values):
        values[3] = values[3].replace("i < kLegAndFootServoCount", "i < SERVO_COUNT", 1)
        return values

    expect_rejection(widen_attach, "legs and feet")

    def boot_right_arm(values):
        values[2] = values[2].replace(
            "otto_.HoldLegsFeetAtNeutral();",
            "otto_.AttachRightHandAtHome(135);\n        otto_.HoldLegsFeetAtNeutral();",
            1,
        )
        return values

    expect_rejection(boot_right_arm, "right arm")


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

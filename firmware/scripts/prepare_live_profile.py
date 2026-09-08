#!/usr/bin/env python3
"""Generate an ignored local gosha-v1 Live profile header from owner JSON."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import string
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "local_only/gosha_motion_live_profile.h"
PROFILE_ID = "gosha-preview-v1"
PROFILE_MODE_VERIFIED = "verified"
PROFILE_MODE_COMMISSIONING = "commissioning"
PROFILE_MODE_COMMISSIONING_RIGHT_ARM = "commissioning_right_arm"
WATCHDOG_MS = 300
MIN_RATE_HZ = 5
MAX_RATE_HZ = 20
MAX_SPEED_DPS = 30
COMMISSIONING_LIMIT_DEGREES = 1.0
COMMISSIONING_MAX_SPEED_DPS = 1.0
RIGHT_ARM_HOME_DEGREES = 135
RIGHT_ARM_DEFAULT_LIMIT_DEGREES = 5.0
RIGHT_ARM_EXTENDED_LIMIT_DEGREES = 15.0
RIGHT_ARM_ALLOWED_LIMIT_DEGREES = (
    RIGHT_ARM_DEFAULT_LIMIT_DEGREES,
    RIGHT_ARM_EXTENDED_LIMIT_DEGREES,
)
CALIBRATION_RE = re.compile(r"^[a-f0-9]{64}$")

LOWER_BODY_JOINT_IDS = (
    "leg_negative_x",
    "leg_positive_x",
    "foot_negative_x",
    "foot_positive_x",
)
RIGHT_ARM_JOINT_IDS = LOWER_BODY_JOINT_IDS + ("arm_positive_x",)
MODE_JOINT_IDS = {
    PROFILE_MODE_VERIFIED: set(LOWER_BODY_JOINT_IDS),
    PROFILE_MODE_COMMISSIONING: set(LOWER_BODY_JOINT_IDS),
    PROFILE_MODE_COMMISSIONING_RIGHT_ARM: set(RIGHT_ARM_JOINT_IDS),
}
LOWER_BODY_SERVO_KEYS = {"left_leg", "right_leg", "left_foot", "right_foot"}
MODE_SERVO_KEYS = {
    PROFILE_MODE_VERIFIED: LOWER_BODY_SERVO_KEYS,
    PROFILE_MODE_COMMISSIONING: LOWER_BODY_SERVO_KEYS,
    PROFILE_MODE_COMMISSIONING_RIGHT_ARM: LOWER_BODY_SERVO_KEYS | {"right_hand"},
}

UI_JOINTS = {
    "leg_negative_x": {
        "servo_group": "leg",
        "ui_min": -35,
        "ui_max": 35,
    },
    "leg_positive_x": {
        "servo_group": "leg",
        "ui_min": -35,
        "ui_max": 35,
    },
    "foot_negative_x": {
        "servo_group": "foot",
        "ui_min": -30,
        "ui_max": 30,
    },
    "foot_positive_x": {
        "servo_group": "foot",
        "ui_min": -30,
        "ui_max": 30,
    },
    "arm_positive_x": {
        "servo_group": "arm",
        "ui_min": -70,
        "ui_max": 70,
    },
}

SERVO_SLOTS = {
    "left_leg": {"servo_group": "leg", "servo_index": 0, "pin": 17, "neutral": 90},
    "right_leg": {"servo_group": "leg", "servo_index": 1, "pin": 39, "neutral": 90},
    "left_foot": {"servo_group": "foot", "servo_index": 2, "pin": 18, "neutral": 90},
    "right_foot": {"servo_group": "foot", "servo_index": 3, "pin": 38, "neutral": 90},
    "left_hand": {"servo_group": "arm", "servo_index": 4, "pin": 8, "neutral": 45},
    "right_hand": {"servo_group": "arm", "servo_index": 5, "pin": 12, "neutral": RIGHT_ARM_HOME_DEGREES},
}


class ProfileError(Exception):
    """Owner profile validation failed."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ProfileError(message)


def as_int(value: Any, field: str) -> int:
    require(isinstance(value, int) and not isinstance(value, bool), f"{field} must be an integer")
    return value


def as_number(value: Any, field: str) -> float:
    require(isinstance(value, (int, float)) and not isinstance(value, bool), f"{field} must be numeric")
    return float(value)


def validate_access_key(value: Any) -> str:
    require(isinstance(value, str), "access_key must be a string")
    require(16 <= len(value) <= 128, "access_key must contain 16..128 characters")
    require(not any(ch.isspace() for ch in value), "access_key must not contain whitespace")
    require(all(ch in string.printable and ch not in "\r\n\t\x0b\x0c" for ch in value),
            "access_key must be printable local text")
    require(len(set(value)) >= 8, "access_key must have at least 8 distinct characters")
    require(value not in {"0123456789abcdef", "abcdefghijklmnop", "gosha-motion-live"},
            "access_key looks like a placeholder")
    return value


def require_commissioning_limits(joint_id: str, relative_min: float, relative_max: float,
                                 max_speed: float, limit: float) -> None:
    require(relative_min == -limit and relative_max == limit,
            f"{joint_id}: commissioning limits must be exactly [{-limit:+g},{limit:+g}] degrees")
    require(max_speed <= COMMISSIONING_MAX_SPEED_DPS,
            f"{joint_id}: commissioning max_speed_dps must be <= {COMMISSIONING_MAX_SPEED_DPS}")


def require_right_arm_commissioning_extent(joint_id: str, relative_min: float,
                                           relative_max: float) -> float:
    for extent in RIGHT_ARM_ALLOWED_LIMIT_DEGREES:
        if relative_min == -extent and relative_max == extent:
            return extent
    raise ProfileError(
        f"{joint_id}: right-arm commissioning limits must be exactly [-5,+5] or [-15,+15] degrees"
    )


def validate_joint(raw: Any, mode: str) -> dict[str, Any]:
    require(isinstance(raw, dict), "each joint entry must be an object")
    joint_id = raw.get("id")
    require(isinstance(joint_id, str) and joint_id in UI_JOINTS,
            "joint id must be one of the allowed Motion Studio ids")
    require(joint_id in MODE_JOINT_IDS[mode], f"{joint_id}: joint is not available in {mode}")
    expected_joint = UI_JOINTS[joint_id]

    servo_key = raw.get("servo_key")
    require(isinstance(servo_key, str) and servo_key in SERVO_SLOTS,
            f"{joint_id}: servo_key must be explicit and checked by the owner")
    require(servo_key in MODE_SERVO_KEYS[mode],
            f"{joint_id}: servo_key {servo_key} is not available in {mode}")
    require(servo_key != "left_hand", "left_hand is unavailable: the physical left arm is disconnected")
    expected_servo = SERVO_SLOTS[servo_key]
    require(expected_servo["servo_group"] == expected_joint["servo_group"],
            f"{joint_id}: servo_key must stay within the {expected_joint['servo_group']} pair")
    if joint_id == "arm_positive_x":
        require(mode == PROFILE_MODE_COMMISSIONING_RIGHT_ARM,
                "arm_positive_x is available only in commissioning_right_arm")
        require(servo_key == "right_hand",
                "arm_positive_x must bind to right_hand slot5 GPIO12")
    else:
        require(servo_key in LOWER_BODY_SERVO_KEYS,
                f"{joint_id}: lower-body joints must not bind to hand slots")

    if "servo_index" in raw:
        require(as_int(raw["servo_index"], f"{joint_id}.servo_index") == expected_servo["servo_index"],
                f"{joint_id}: servo_index does not match selected servo_key")
    require(as_int(raw.get("pin"), f"{joint_id}.pin") == expected_servo["pin"],
            f"{joint_id}: pin must match selected servo_key and checked non-camera pin map")

    trim = as_int(raw.get("trim"), f"{joint_id}.trim")
    require(-50 <= trim <= 50, f"{joint_id}: trim must be within -50..50 degrees")

    neutral = as_int(raw.get("neutral_degrees"), f"{joint_id}.neutral_degrees")
    require(neutral == expected_servo["neutral"],
            f"{joint_id}: neutral_degrees must stay {expected_servo['neutral']} for the installed controller")

    direction = as_int(raw.get("direction"), f"{joint_id}.direction")
    require(direction in (-1, 1), f"{joint_id}: direction must be -1 or 1")
    if joint_id == "arm_positive_x":
        require(direction == 1, "arm_positive_x must use direction +1 so relative -5 commands servo 130")

    relative_min = as_number(raw.get("min"), f"{joint_id}.min")
    relative_max = as_number(raw.get("max"), f"{joint_id}.max")
    require(relative_min < relative_max, f"{joint_id}: min must be less than max")
    require(expected_joint["ui_min"] <= relative_min <= 0 <= relative_max <= expected_joint["ui_max"],
            f"{joint_id}: relative limits must stay inside UI model limits and include 0")

    servo_min = as_int(raw.get("servo_min_degrees"), f"{joint_id}.servo_min_degrees")
    servo_max = as_int(raw.get("servo_max_degrees"), f"{joint_id}.servo_max_degrees")
    require(0 <= servo_min < servo_max <= 180, f"{joint_id}: servo range must stay inside 0..180")
    servo_at_min = neutral + direction * relative_min
    servo_at_max = neutral + direction * relative_max
    require(min(servo_at_min, servo_at_max) >= servo_min and max(servo_at_min, servo_at_max) <= servo_max,
            f"{joint_id}: relative mapping leaves the prepared servo range")

    max_speed = as_number(raw.get("max_speed_dps"), f"{joint_id}.max_speed_dps")
    require(0 < max_speed <= MAX_SPEED_DPS, f"{joint_id}: max_speed_dps must be <= {MAX_SPEED_DPS}")
    if mode == PROFILE_MODE_COMMISSIONING:
        require_commissioning_limits(joint_id, relative_min, relative_max, max_speed,
                                     COMMISSIONING_LIMIT_DEGREES)
    if mode == PROFILE_MODE_COMMISSIONING_RIGHT_ARM:
        if joint_id == "arm_positive_x":
            right_arm_extent = require_right_arm_commissioning_extent(
                joint_id, relative_min, relative_max
            )
            require(max_speed <= COMMISSIONING_MAX_SPEED_DPS,
                    f"{joint_id}: commissioning max_speed_dps must be <= {COMMISSIONING_MAX_SPEED_DPS}")
            require(servo_min == RIGHT_ARM_HOME_DEGREES - int(right_arm_extent) and
                    servo_max == RIGHT_ARM_HOME_DEGREES + int(right_arm_extent),
                    "arm_positive_x must stay within servo 130..140 or 120..150 from rightHome135")
        else:
            require_commissioning_limits(joint_id, relative_min, relative_max, max_speed,
                                         COMMISSIONING_LIMIT_DEGREES)

    return {
        "id": joint_id,
        "servo_key": servo_key,
        "servo_index": expected_servo["servo_index"],
        "pin": expected_servo["pin"],
        "trim": trim,
        "neutral_degrees": neutral,
        "direction": direction,
        "min_relative_degrees": relative_min,
        "max_relative_degrees": relative_max,
        "min_servo_degrees": servo_min,
        "max_servo_degrees": servo_max,
        "max_speed_dps": max_speed,
    }


def validate_profile(raw: Any) -> dict[str, Any]:
    require(isinstance(raw, dict), "profile JSON must be an object")
    require(raw.get("profile_id") == PROFILE_ID, f"profile_id must be {PROFILE_ID}")
    mode = raw.get("mode", PROFILE_MODE_VERIFIED)
    require(mode in MODE_JOINT_IDS,
            "mode must be verified, commissioning or commissioning_right_arm")
    access_key = validate_access_key(raw.get("access_key"))
    watchdog_ms = as_int(raw.get("watchdog_ms", WATCHDOG_MS), "watchdog_ms")
    require(watchdog_ms == WATCHDOG_MS, "watchdog_ms must be exactly 300")
    max_rate_hz = as_int(raw.get("max_rate_hz"), "max_rate_hz")
    require(MIN_RATE_HZ <= max_rate_hz <= MAX_RATE_HZ, "max_rate_hz must be within 5..20")
    require(1000 / max_rate_hz <= watchdog_ms / 2, "max_rate_hz interval must fit watchdog/2")

    expected_joint_ids = MODE_JOINT_IDS[mode]
    joints_raw = raw.get("joints")
    require(isinstance(joints_raw, list) and len(joints_raw) == len(expected_joint_ids),
            f"joints must contain exactly {len(expected_joint_ids)} entries for {mode}")
    joints = [validate_joint(item, mode) for item in joints_raw]
    require({item["id"] for item in joints} == expected_joint_ids,
            f"joints must cover each {mode} id once")
    require({item["servo_key"] for item in joints} == MODE_SERVO_KEYS[mode],
            f"joints must bind each {mode} servo_key once")
    joints.sort(key=lambda item: item["servo_index"])

    calibration_payload = {
        "profile_id": PROFILE_ID,
        "mode": mode,
        "watchdog_ms": watchdog_ms,
        "max_rate_hz": max_rate_hz,
        "joints": sorted(joints, key=lambda item: item["id"]),
    }
    calibration_text = json.dumps(
        calibration_payload,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    )
    calibration_id = hashlib.sha256(calibration_text.encode("utf-8")).hexdigest()
    expected_calibration_id = raw.get("calibration_id")
    if expected_calibration_id is not None:
        require(
            isinstance(expected_calibration_id, str) and CALIBRATION_RE.fullmatch(expected_calibration_id),
            "optional calibration_id must be 64 lowercase hex characters",
        )
        require(
            expected_calibration_id == calibration_id,
            "calibration_id is computed from the checked profile and does not match the input",
        )

    return {
        "profile_id": PROFILE_ID,
        "mode": mode,
        "calibration_id": calibration_id,
        "access_key_sha256": hashlib.sha256(access_key.encode("utf-8")).hexdigest(),
        "watchdog_ms": watchdog_ms,
        "max_rate_hz": max_rate_hz,
        "joints": joints,
    }


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def fmt_number(value: float) -> str:
    if value.is_integer():
        return f"{int(value)}.0"
    return repr(value)


def render_header(profile: dict[str, Any]) -> str:
    lines = [
        "#pragma once",
        "",
        "// Generated by firmware/scripts/prepare_live_profile.py from owner-local JSON.",
        "// Keep this file under firmware/local_only/ or another ignored local path.",
        "namespace gosha::motion_live {",
        "",
        "static const MotionLivePreparedProfile kGoshaMotionLivePreparedProfile = {",
        f"    {cpp_string(profile['profile_id'])},",
        f"    {cpp_string(profile['calibration_id'])},",
        f"    {cpp_string(profile['access_key_sha256'])},",
        f"    {profile['watchdog_ms']},",
        f"    {profile['max_rate_hz']},",
        "    {{",
    ]
    for joint in profile["joints"]:
        lines.append(
            "        {"
            f"{cpp_string(joint['id'])}, "
            f"{cpp_string(joint['servo_key'])}, "
            f"{joint['servo_index']}, "
            f"{joint['pin']}, "
            f"{joint['trim']}, "
            f"{joint['neutral_degrees']}, "
            f"{joint['direction']}, "
            f"{fmt_number(joint['min_relative_degrees'])}, "
            f"{fmt_number(joint['max_relative_degrees'])}, "
            f"{joint['min_servo_degrees']}, "
            f"{joint['max_servo_degrees']}, "
            f"{fmt_number(joint['max_speed_dps'])}"
            "},"
        )
    lines.extend([
        "    }},",
        f"    {cpp_string(profile['mode'])},",
        f"    {len(profile['joints'])},",
        "};",
        "",
        "}  // namespace gosha::motion_live",
        "",
    ])
    return "\n".join(lines)


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def lower_body_sample(mode: str,
                      right_arm_extent: float = RIGHT_ARM_DEFAULT_LIMIT_DEGREES) -> dict[str, Any]:
    sample = {
        "profile_id": PROFILE_ID,
        "mode": mode,
        "access_key": "LiveKey-20260906-Q4bz!",
        "watchdog_ms": 300,
        "max_rate_hz": 20,
        "joints": [
            {"id": "leg_negative_x", "servo_key": "left_leg", "pin": 17, "trim": 0, "neutral_degrees": 90,
             "direction": 1, "min": -10, "max": 10, "servo_min_degrees": 80,
             "servo_max_degrees": 100, "max_speed_dps": 15},
            {"id": "leg_positive_x", "servo_key": "right_leg", "pin": 39, "trim": 0, "neutral_degrees": 90,
             "direction": -1, "min": -10, "max": 10, "servo_min_degrees": 80,
             "servo_max_degrees": 100, "max_speed_dps": 15},
            {"id": "foot_negative_x", "servo_key": "left_foot", "pin": 18, "trim": 0, "neutral_degrees": 90,
             "direction": 1, "min": -8, "max": 8, "servo_min_degrees": 82,
             "servo_max_degrees": 98, "max_speed_dps": 12},
            {"id": "foot_positive_x", "servo_key": "right_foot", "pin": 38, "trim": 0, "neutral_degrees": 90,
             "direction": -1, "min": -8, "max": 8, "servo_min_degrees": 82,
             "servo_max_degrees": 98, "max_speed_dps": 12},
        ],
    }
    if mode in (PROFILE_MODE_COMMISSIONING, PROFILE_MODE_COMMISSIONING_RIGHT_ARM):
        for joint in sample["joints"]:
            joint["min"] = -1
            joint["max"] = 1
            joint["servo_min_degrees"] = 89
            joint["servo_max_degrees"] = 91
            joint["max_speed_dps"] = 1
    if mode == PROFILE_MODE_COMMISSIONING_RIGHT_ARM:
        sample["joints"].append({
            "id": "arm_positive_x",
            "servo_key": "right_hand",
            "servo_index": 5,
            "pin": 12,
            "trim": 0,
            "neutral_degrees": RIGHT_ARM_HOME_DEGREES,
            "direction": 1,
            "min": -right_arm_extent,
            "max": right_arm_extent,
            "servo_min_degrees": RIGHT_ARM_HOME_DEGREES - int(right_arm_extent),
            "servo_max_degrees": RIGHT_ARM_HOME_DEGREES + int(right_arm_extent),
            "max_speed_dps": 1,
        })
    return sample


def expect_rejection(sample: dict[str, Any], needle: str) -> None:
    try:
        validate_profile(sample)
    except ProfileError as exc:
        require(needle in str(exc), f"negative test did not report {needle}: {exc}")
    else:
        raise ProfileError(f"negative test was accepted: {needle}")


def run_self_test() -> None:
    verified = lower_body_sample(PROFILE_MODE_VERIFIED)
    verified_profile = validate_profile(verified)
    require(verified_profile["mode"] == PROFILE_MODE_VERIFIED, "verified profile mode changed")
    require(CALIBRATION_RE.fullmatch(verified_profile["calibration_id"]) is not None,
            "computed calibration_id is not 64 lowercase hex")
    header = render_header(verified_profile)
    require("LiveKey-20260906-Q4bz!" not in header, "header leaked plaintext access_key")
    require(verified_profile["access_key_sha256"] in header, "header did not contain access_key hash")
    require('"verified"' in header and "    4," in header, "header did not preserve 4-joint mode")
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "gosha_motion_live_profile.h"
        output.write_text(header, encoding="utf-8")
        require(output.read_text(encoding="utf-8") == header, "self-test header write failed")

    commissioning = lower_body_sample(PROFILE_MODE_COMMISSIONING)
    commissioning_profile = validate_profile(commissioning)
    require(commissioning_profile["mode"] == PROFILE_MODE_COMMISSIONING,
            "commissioning profile mode changed")
    require(commissioning_profile["calibration_id"] != verified_profile["calibration_id"],
            "commissioning mode must be included in computed calibration_id")
    commissioning_header = render_header(commissioning_profile)
    require('"commissioning"' in commissioning_header, "header did not mark commissioning mode")
    require("LiveKey-20260906-Q4bz!" not in commissioning_header,
            "commissioning header leaked plaintext access_key")

    right_arm = lower_body_sample(PROFILE_MODE_COMMISSIONING_RIGHT_ARM)
    right_arm_profile = validate_profile(right_arm)
    right_header = render_header(right_arm_profile)
    require(right_arm_profile["mode"] == PROFILE_MODE_COMMISSIONING_RIGHT_ARM,
            "right-arm commissioning profile mode changed")
    require(right_arm_profile["calibration_id"] not in {
        verified_profile["calibration_id"], commissioning_profile["calibration_id"]},
        "commissioning_right_arm mode and fifth joint must be included in computed calibration_id")
    require('"commissioning_right_arm"' in right_header and "    5," in right_header,
            "right-arm header did not mark 5-joint commissioning_right_arm mode")
    require('{"arm_positive_x", "right_hand", 5, 12, 0, 135, 1, -5.0, 5.0, 130, 140, 1.0}' in right_header,
            "right-arm header did not bind arm_positive_x to right_hand slot5 GPIO12")
    require("LiveKey-20260906-Q4bz!" not in right_header,
            "right-arm header leaked plaintext access_key")

    right_arm_15 = lower_body_sample(PROFILE_MODE_COMMISSIONING_RIGHT_ARM,
                                     RIGHT_ARM_EXTENDED_LIMIT_DEGREES)
    right_arm_15_profile = validate_profile(right_arm_15)
    right_15_header = render_header(right_arm_15_profile)
    require('{"arm_positive_x", "right_hand", 5, 12, 0, 135, 1, -15.0, 15.0, 120, 150, 1.0}' in right_15_header,
            "right-arm 15-degree header did not bind servo 120..150 from rightHome135")
    require(right_arm_15_profile["calibration_id"] != right_arm_profile["calibration_id"],
            "right-arm 15-degree range must change the computed calibration_id")
    require("LiveKey-20260906-Q4bz!" not in right_15_header,
            "right-arm 15-degree header leaked plaintext access_key")

    bad_commissioning_range = json.loads(json.dumps(commissioning))
    bad_commissioning_range["joints"][0]["max"] = 2
    bad_commissioning_range["joints"][0]["servo_max_degrees"] = 92
    expect_rejection(bad_commissioning_range, "commissioning limits")

    bad_commissioning_speed = json.loads(json.dumps(commissioning))
    bad_commissioning_speed["joints"][0]["max_speed_dps"] = 2
    expect_rejection(bad_commissioning_speed, "commissioning max_speed_dps")

    bad_old_fifth = json.loads(json.dumps(commissioning))
    bad_old_fifth["joints"].append(dict(right_arm["joints"][-1]))
    expect_rejection(bad_old_fifth, "exactly 4")

    bad_right_six = json.loads(json.dumps(right_arm))
    bad_right_six["joints"].append(dict(right_arm["joints"][-1], id="arm_negative_x"))
    expect_rejection(bad_right_six, "exactly 5")

    bad_right_slot = json.loads(json.dumps(right_arm))
    bad_right_slot["joints"][-1]["servo_index"] = 4
    expect_rejection(bad_right_slot, "servo_index")

    bad_right_pin = json.loads(json.dumps(right_arm))
    bad_right_pin["joints"][-1]["pin"] = 8
    expect_rejection(bad_right_pin, "pin")

    bad_left_arm = json.loads(json.dumps(right_arm))
    bad_left_arm["joints"][-1]["servo_key"] = "left_hand"
    bad_left_arm["joints"][-1]["servo_index"] = 4
    bad_left_arm["joints"][-1]["pin"] = 8
    bad_left_arm["joints"][-1]["neutral_degrees"] = 45
    expect_rejection(bad_left_arm, "left_hand")

    bad_right_direction = json.loads(json.dumps(right_arm))
    bad_right_direction["joints"][-1]["direction"] = -1
    expect_rejection(bad_right_direction, "direction +1")

    bad_right_range = json.loads(json.dumps(right_arm))
    bad_right_range["joints"][-1]["min"] = -10
    bad_right_range["joints"][-1]["max"] = 10
    bad_right_range["joints"][-1]["servo_min_degrees"] = 125
    bad_right_range["joints"][-1]["servo_max_degrees"] = 145
    expect_rejection(bad_right_range, "right-arm commissioning limits")

    bad_right_too_wide = json.loads(json.dumps(right_arm))
    bad_right_too_wide["joints"][-1]["min"] = -16
    bad_right_too_wide["joints"][-1]["max"] = 16
    bad_right_too_wide["joints"][-1]["servo_min_degrees"] = 119
    bad_right_too_wide["joints"][-1]["servo_max_degrees"] = 151
    expect_rejection(bad_right_too_wide, "right-arm commissioning limits")

    bad_right_asymmetric = json.loads(json.dumps(right_arm_15))
    bad_right_asymmetric["joints"][-1]["min"] = -5
    bad_right_asymmetric["joints"][-1]["servo_min_degrees"] = 130
    expect_rejection(bad_right_asymmetric, "right-arm commissioning limits")

    bad_right_15_bounds = json.loads(json.dumps(right_arm_15))
    bad_right_15_bounds["joints"][-1]["servo_min_degrees"] = 119
    expect_rejection(bad_right_15_bounds, "servo 130..140 or 120..150")

    bad_right_speed = json.loads(json.dumps(right_arm))
    bad_right_speed["joints"][-1]["max_speed_dps"] = 2
    expect_rejection(bad_right_speed, "commissioning max_speed_dps")

    bad_id = json.loads(json.dumps(verified))
    bad_id["calibration_id"] = "b" * 64
    expect_rejection(bad_id, "computed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_json", nargs="?", type=Path,
                        help="owner-local JSON with checked calibration and access_key")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help=f"generated ignored header path (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--self-test", action="store_true",
                        help="run validation/generation self-test without owner files")
    args = parser.parse_args()

    try:
        if args.self_test:
            run_self_test()
            print("prepare_live_profile: self-test PASS")
            return 0
        require(args.profile_json is not None, "profile_json is required unless --self-test is used")
        profile = validate_profile(load_json(args.profile_json))
        header = render_header(profile)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(header, encoding="utf-8")
        print(f"prepare_live_profile: wrote {args.output}")
        print("prepare_live_profile: plaintext access_key was not written")
        return 0
    except (OSError, json.JSONDecodeError, ProfileError) as exc:
        print(f"prepare_live_profile: ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

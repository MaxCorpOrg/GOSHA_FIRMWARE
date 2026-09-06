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
WATCHDOG_MS = 300
MIN_RATE_HZ = 5
MAX_RATE_HZ = 20
MAX_SPEED_DPS = 30
CALIBRATION_RE = re.compile(r"^[a-f0-9]{64}$")

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
}

SERVO_SLOTS = {
    "left_leg": {"servo_group": "leg", "servo_index": 0, "pin": 17},
    "right_leg": {"servo_group": "leg", "servo_index": 1, "pin": 39},
    "left_foot": {"servo_group": "foot", "servo_index": 2, "pin": 18},
    "right_foot": {"servo_group": "foot", "servo_index": 3, "pin": 38},
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


def validate_joint(raw: Any) -> dict[str, Any]:
    require(isinstance(raw, dict), "each joint entry must be an object")
    joint_id = raw.get("id")
    require(isinstance(joint_id, str) and joint_id in UI_JOINTS, "joint id must be one of the four lower-body ids")
    expected_joint = UI_JOINTS[joint_id]
    servo_key = raw.get("servo_key")
    require(isinstance(servo_key, str) and servo_key in SERVO_SLOTS,
            f"{joint_id}: servo_key must be explicit and checked by the owner")
    expected_servo = SERVO_SLOTS[servo_key]
    require(expected_servo["servo_group"] == expected_joint["servo_group"],
            f"{joint_id}: servo_key must stay within the {expected_joint['servo_group']} pair")
    if "servo_index" in raw:
        require(as_int(raw["servo_index"], f"{joint_id}.servo_index") == expected_servo["servo_index"],
                f"{joint_id}: servo_index does not match selected servo_key")
    require(as_int(raw.get("pin"), f"{joint_id}.pin") == expected_servo["pin"],
            f"{joint_id}: pin must match selected servo_key and checked non-camera pin map")

    trim = as_int(raw.get("trim"), f"{joint_id}.trim")
    require(-50 <= trim <= 50, f"{joint_id}: trim must be within -50..50 degrees")

    neutral = as_int(raw.get("neutral_degrees"), f"{joint_id}.neutral_degrees")
    require(neutral == 90, f"{joint_id}: neutral_degrees must stay 90 for the installed controller")

    direction = as_int(raw.get("direction"), f"{joint_id}.direction")
    require(direction in (-1, 1), f"{joint_id}: direction must be -1 or 1")

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
    access_key = validate_access_key(raw.get("access_key"))
    watchdog_ms = as_int(raw.get("watchdog_ms", WATCHDOG_MS), "watchdog_ms")
    require(watchdog_ms == WATCHDOG_MS, "watchdog_ms must be exactly 300")
    max_rate_hz = as_int(raw.get("max_rate_hz"), "max_rate_hz")
    require(MIN_RATE_HZ <= max_rate_hz <= MAX_RATE_HZ, "max_rate_hz must be within 5..20")
    require(1000 / max_rate_hz <= watchdog_ms / 2, "max_rate_hz interval must fit watchdog/2")

    joints_raw = raw.get("joints")
    require(isinstance(joints_raw, list) and len(joints_raw) == len(UI_JOINTS),
            "joints must contain exactly the four lower-body entries")
    joints = [validate_joint(item) for item in joints_raw]
    require({item["id"] for item in joints} == set(UI_JOINTS), "joints must cover each lower-body id once")
    require({item["servo_key"] for item in joints} == set(SERVO_SLOTS),
            "joints must bind each lower-body servo_key once")
    joints.sort(key=lambda item: item["servo_index"])

    calibration_payload = {
        "profile_id": PROFILE_ID,
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
        "};",
        "",
        "}  // namespace gosha::motion_live",
        "",
    ])
    return "\n".join(lines)


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def run_self_test() -> None:
    sample = {
        "profile_id": PROFILE_ID,
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
    profile = validate_profile(sample)
    require(CALIBRATION_RE.fullmatch(profile["calibration_id"]) is not None,
            "computed calibration_id is not 64 lowercase hex")
    header = render_header(profile)
    require("LiveKey-20260906-Q4bz!" not in header, "header leaked plaintext access_key")
    require(profile["access_key_sha256"] in header, "header did not contain access_key hash")
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / "gosha_motion_live_profile.h"
        output.write_text(header, encoding="utf-8")
        require(output.read_text(encoding="utf-8") == header, "self-test header write failed")

    bad = dict(sample)
    bad["joints"] = list(sample["joints"])
    bad["joints"][0] = dict(bad["joints"][0], pin=8)
    try:
        validate_profile(bad)
    except ProfileError as exc:
        require("pin" in str(exc), f"pin negative test named wrong error: {exc}")
    else:
        raise ProfileError("pin negative test was accepted")

    bad_id = dict(sample)
    bad_id["calibration_id"] = "b" * 64
    try:
        validate_profile(bad_id)
    except ProfileError as exc:
        require("computed" in str(exc), f"calibration id negative test named wrong error: {exc}")
    else:
        raise ProfileError("calibration id negative test was accepted")


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

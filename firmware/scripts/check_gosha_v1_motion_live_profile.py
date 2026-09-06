#!/usr/bin/env python3
"""Static guard for the gosha-v1 local Motion Live adapter."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "main/boards/gosha-v1/config.json"
KCONFIG = ROOT / "main/Kconfig.projbuild"
CMAKE = ROOT / "main/CMakeLists.txt"
WS = ROOT / "main/boards/gosha-v1/websocket_control_server.cc"
ADAPTER = ROOT / "main/boards/gosha-v1/motion_live_adapter.cc"
CORE = ROOT / "main/boards/gosha-v1/motion_live_core.cc"
CORE_H = ROOT / "main/boards/gosha-v1/motion_live_core.h"
BOARD = ROOT / "main/boards/gosha-v1/otto_robot.cc"
CONTROLLER = ROOT / "main/boards/gosha-v1/otto_controller.cc"
MOVEMENTS = ROOT / "main/boards/gosha-v1/otto_movements.cc"
PREPARE = ROOT / "scripts/prepare_live_profile.py"
RELEASE = ROOT / "scripts/release.py"

LIVE_OPT_IN = "CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN=y"


class GuardError(Exception):
    """Motion Live static contract violation."""


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


def validate_config(config_text: str) -> None:
    data = json.loads(config_text)
    for build in data.get("builds", []):
        require(
            LIVE_OPT_IN not in build.get("sdkconfig_append", []),
            "release config must not enable CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN by default",
        )


def validate_kconfig(kconfig: str) -> None:
    body = kconfig_body(kconfig, "GOSHA_MOTION_LIVE_LOCAL_OPT_IN")
    for dependency in (
        "depends on BOARD_TYPE_GOSHA_V1",
        "depends on GOSHA_NO_MOTION_SAFE_PROFILE",
        "depends on GOSHA_SAFE_NEUTRAL_BOOT_PROFILE",
    ):
        require(dependency in body, f"Live opt-in must keep {dependency}")
    require("default n" in body, "Live opt-in must default to off")


def validate_cmake(cmake: str) -> None:
    require("CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN" in cmake, "CMake must handle Live opt-in")
    require("GOSHA_MOTION_LIVE_PROFILE_HEADER" in cmake, "CMake must require explicit profile header path")
    require("NOT IS_ABSOLUTE" in cmake, "profile header path must be absolute")
    require("NOT EXISTS" in cmake, "profile header path existence must be checked")
    require("GOSHA_MOTION_LIVE_PROFILE_HEADER_ENABLED=1" in cmake, "adapter header macro must be opt-in only")
    require("mbedtls" in cmake, "access_key SHA-256 dependency must be declared")


def validate_live_wifi(board: str) -> None:
    body = extract_body(board, r"void\s+SetPowerSaveLevel\s*\([^)]*\)\s*override", "board power policy")
    require(re.search(
        r"#ifdef CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN\s+"
        r"(?:\s*//[^\n]*\n)*\s*"
        r"WifiBoard::SetPowerSaveLevel\(PowerSaveLevel::PERFORMANCE\);\s*"
        r"#else\s*WifiBoard::SetPowerSaveLevel\(level\);\s*#endif", body) is not None,
        "Live opt-in must keep WiFi awake while other builds preserve their power policy")
    start = extract_body(board, r"void\s+StartNetwork\s*\(\s*\)\s*override", "StartNetwork")
    policy = "#ifdef CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN\n        SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);\n#endif"
    require(policy in start and start.index("WifiBoard::StartNetwork()") < start.index(policy)
            < start.index("InitializeWebSocketControlServer()"),
            "Live WiFi policy must apply before local WebSocket starts")


def validate_websocket(ws: str) -> None:
    body = extract_body(ws, r"void\s+WebSocketControlServer::HandleMessage\s*\([^)]*\)", "HandleMessage")
    live_pos = body.find("MotionLiveAdapter::GetInstance().HandleWebSocketMessage")
    identity_pos = body.find("IsLocalIdentityRequest")
    parse_pos = body.find("McpServer::GetInstance().ParseMessage")
    require(live_pos >= 0, "Live handler must be called by local WebSocket server")
    require(identity_pos >= 0 and live_pos < identity_pos, "Live namespace must run before local identity fallback")
    require(parse_pos >= 0 and live_pos < parse_pos, "Live namespace must run before MCP fallback")
    require("return;" in body[live_pos:identity_pos], "Live handler must consume protocol messages")
    require("MotionLiveAdapter::GetInstance().OnSocketClosed" in ws, "socket close must disarm owned Live session")
    require("kMaxWebSocketMessageBytes = 4096" in ws, "WebSocket must keep a fixed frame size cap")
    receive_body = extract_body(ws, r"esp_err_t\s+WebSocketControlServer::ws_handler\s*\([^)]*\)", "ws_handler")
    cap_pos = receive_body.find("ws_pkt.len > kMaxWebSocketMessageBytes")
    calloc_pos = receive_body.find("calloc")
    require(cap_pos >= 0 and calloc_pos >= 0 and cap_pos < calloc_pos,
            "WebSocket must reject oversized frames before allocation")
    require(receive_body.count("RemoveClient(req)") >= 3,
            "WebSocket receive errors and CLOSE must disarm owned Live sessions")
    stop_body = extract_body(ws, r"void\s+WebSocketControlServer::Stop\s*\(\s*\)", "Stop")
    require("OnSocketClosed(client.first)" in stop_body,
            "server Stop must disarm every owned Live session before clearing clients")
    require('ESP_LOGD(TAG, "frame len is %d"' in ws and 'ESP_LOGD(TAG, "Packet type: %d"' in ws,
            "high-rate frame diagnostics must be debug-level")


def validate_adapter(adapter: str) -> None:
    require('kProtocol = "gosha.motion.live.v1"' not in adapter, "protocol constant belongs in core")
    require("PreparedProfileOrNull" in adapter, "adapter must use generated profile only through opt-in")
    require("CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN" in adapter, "adapter must check compile opt-in")
    require("#include GOSHA_MOTION_LIVE_PROFILE_HEADER" in adapter, "adapter must include generated header by macro")
    require("mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)" in adapter, "adapter must hash access_key")
    require("access_key" not in "\n".join(line for line in adapter.splitlines() if "ESP_LOG" in line),
            "adapter must not log access_key")
    require('cJSON_AddBoolToObject(feedback, "measured_position", false)' in adapter,
            "measured_position feedback must default false")
    require('cJSON_AddBoolToObject(feedback, "imu", false)' in adapter, "IMU feedback must default false")
    require('cJSON_AddStringToObject(reply, "mode", caps.mode)' in adapter,
            "capabilities must report Live profile mode")
    require('cJSON_AddBoolToObject(reply, "commissioning", caps.commissioning)' in adapter,
            "capabilities must report commissioning mode explicitly")
    require('cJSON_AddNullToObject(reply, "measured_pose")' in adapter, "ACK must not synthesize measured_pose")
    require('cJSON_AddNullToObject(reply, "tilt")' in adapter, "ACK must not synthesize tilt")
    require("esp_timer_start_periodic" in adapter and "gosha_live_watchdog" in adapter,
            "watchdog must use a nonblocking periodic timer")
    require('SendError(req, root, "bad_json"' in adapter, "bad JSON in Live namespace must not fall through to MCP")


def validate_core(core: str, core_h: str) -> None:
    for token in (
        '"gosha.motion.live.v1"',
        '"gosha-preview-v1"',
        "kWatchdogMs = 300",
        "kMaxServoRateDps = 30",
        "kProfileModeVerified",
        "kProfileModeCommissioning",
        "kCommissioningJointLimitDegrees = 1.0",
        "kCommissioningMaxServoRateDps = 1.0",
        '"leg_negative_x"',
        '"leg_positive_x"',
        '"foot_negative_x"',
        '"foot_positive_x"',
        '"arm_negative_x"',
        '"arm_positive_x"',
    ):
        require(token in core + core_h, f"core contract token is missing: {token}")
    require("motion_allowed = false" in core_h, "capabilities must default motion_allowed=false")
    require("const char* mode = kProfileModeVerified" in core_h,
            "prepared profile mode must default to verified for old generated initializers")
    require("bool commissioning = false" in core_h,
            "capabilities must default commissioning=false")
    require("ValidatePreparedProfile" in core and "ValidateRuntimeAgainstProfile" in core,
            "core must validate generated profile and runtime binding")
    require("local_opt_in_enabled_" in core_h and "profile_ = nullptr" in core_h,
            "core must fail closed without local opt-in/profile")
    require("seq != last_seq_ + 1" in core, "core must enforce strict sequence numbers")
    require("target.has_unknown_joint" in core and "present && !active" in core,
            "core must reject unknown or hand joints in pose target")
    require("speed_dps > kMaxServoRateDps" in core and "speed_dps > joint.max_speed_dps" in core,
            "core must enforce <=30 dps and per-joint speed")
    require("joint.min_relative_degrees != -kCommissioningJointLimitDegrees" in core and
            "joint.max_relative_degrees != kCommissioningJointLimitDegrees" in core and
            "joint.max_speed_dps > kCommissioningMaxServoRateDps" in core,
            "commissioning mode must force exact ±1 degree limits and <=1 dps")
    require("ValidateCommissioningTarget" in core and "session_initial_servo_degrees_" in core_h and
            "commissioning_servo_index_" in core_h and "kCommissioningSingleJoint" in core,
            "commissioning mode must track one physical joint per session")
    require("!ValidateCommissioningTarget(servo_degrees, &reason)" in core,
            "pose must call the commissioning one physical joint gate")
    require("std::abs(delta) > 1" in core and "changed_count > 1" in core,
            "commissioning mode must reject multi-joint and >1 degree baseline deltas")
    require("commissioning_servo_index_ = changed_slot" in core,
            "commissioning mode must lock the first changed physical joint")
    require("caps.commissioning = ProfileIsCommissioning()" in core and
            "caps.calibrated = !caps.commissioning" in core and
            "caps.mode = profile_->mode" in core,
            "capabilities must distinguish commissioning from verified calibration")
    require("now_ms - last_command_ms_" in core and "kWatchdogMs" in core,
            "core must enforce watchdog timeout")
    require("watchdog_tick_ready" in core_h and "kWatchdogUnavailable" in core,
            "core must reject cap/arm when the periodic watchdog is unavailable")
    require("kSessionBusy" in core and "Another Live session is already armed" in core,
            "core must reject a second arm while a session is active")
    require("StepTowardTarget" in core and "max_delta" in core and "std::lround" in core,
            "core must limit applied motion with a fractional accumulator and integer PWM rounding")
    require("ServoIndexForKey" in core and "ExpectedPinForServoIndex" in core,
            "core must validate owner-confirmed UI joint to servo-slot mapping")
    require("(*servo_degrees)[joint.servo_index]" in core,
            "core must address hardware commands by servo_index")
    require("PoseFromServoDegrees" in core and "commanded_pose_ = PoseFromServoDegrees" in core,
            "ACK commanded_pose must be derived from applied integer servo degrees")
    require("LeaseExpired(now_ms)" in core,
            "pose/keepalive must reject expired leases before renewing")
    require("hardware_applier_" in core_h and "ApplyHardware" in core,
            "core must make hardware writes an explicit callback")


def validate_controller(controller: str, movements: str) -> None:
    require("ConfigureMotionLiveAdapter" in controller, "controller must provide runtime binding to Live adapter")
    require("safe_neutral_hold_commanded_" in controller, "runtime binding must know whether neutral hold ran")
    for token in (
        "runtime.no_motion_safe_profile = kNoMotionSafeProfile",
        "runtime.safe_neutral_boot_profile = kSafeNeutralBootProfile",
        "runtime.safe_neutral_commanded = safe_neutral_hold_commanded_",
        "runtime.lower_body_attached = attach_servos && has_complete_legs_feet_",
        "ServoSlot::kLeftLeg",
        "ServoSlot::kRightLeg",
        "ServoSlot::kLeftFoot",
        "ServoSlot::kRightFoot",
        "ServoSlot::kLeftHand",
        "ServoSlot::kRightHand",
        "GPIO_NUM_NC",
    ):
        require(token in controller, f"runtime binding is missing: {token}")
    require("ApplyLegsFeetPositions" in movements, "Otto must expose only lower-body direct Live apply")
    apply_body = extract_body(movements, r"bool\s+Otto::ApplyLegsFeetPositions\s*\([^)]*\)", "ApplyLegsFeetPositions")
    require("i < kLegAndFootServoCount" in apply_body, "Live apply must iterate only legs/feet")
    require("RIGHT_HAND" not in apply_body and "LEFT_HAND" not in apply_body, "Live apply must not touch hands")


def validate_prepare(prepare: str) -> None:
    for token in (
        "DEFAULT_OUTPUT = ROOT / \"local_only/gosha_motion_live_profile.h\"",
        "PROFILE_MODE_VERIFIED = \"verified\"",
        "PROFILE_MODE_COMMISSIONING = \"commissioning\"",
        '"access_key"',
        "hashlib.sha256",
        "calibration_payload",
        "\"mode\": mode",
        "sort_keys=True",
        "calibration_id is computed",
        "plaintext access_key was not written",
        "mode must be verified or commissioning",
        "commissioning limits must be exactly [-1,+1] degrees",
        "commissioning max_speed_dps must be <=",
        "servo_key must be explicit",
        "servo_key must stay within",
        '"leg_negative_x"',
        '"leg_positive_x"',
        '"foot_negative_x"',
        '"foot_positive_x"',
        '"pin\": 17',
        '"pin\": 39',
        '"pin\": 18',
        '"pin\": 38',
    ):
        require(token in prepare, f"prepare script contract token is missing: {token}")


def validate_release(release: str) -> None:
    require(
        '"scripts/check_gosha_v1_motion_live_profile.py", "--self-test"' in release,
        "release.py must run the Motion Live guard",
    )


def validate_tree(values: dict[str, str]) -> None:
    validate_config(values["config"])
    validate_kconfig(values["kconfig"])
    validate_cmake(values["cmake"])
    validate_live_wifi(values["board"])
    validate_websocket(values["ws"])
    validate_adapter(values["adapter"])
    validate_core(values["core"], values["core_h"])
    validate_controller(values["controller"], values["movements"])
    validate_prepare(values["prepare"])
    validate_release(values["release"])


def current_values() -> dict[str, str]:
    return {
        "config": read(CONFIG),
        "kconfig": read(KCONFIG),
        "cmake": read(CMAKE),
        "ws": read(WS),
        "adapter": read(ADAPTER),
        "core": read(CORE),
        "core_h": read(CORE_H),
        "board": read(BOARD),
        "controller": read(CONTROLLER),
        "movements": read(MOVEMENTS),
        "prepare": read(PREPARE),
        "release": read(RELEASE),
    }


def expect_rejection(values: dict[str, str], key: str, needle: str, replacement: str, error: str) -> None:
    mutated = dict(values)
    mutated[key] = mutated[key].replace(needle, replacement, 1)
    try:
        validate_tree(mutated)
    except GuardError as exc:
        require(error in str(exc), f"negative test did not report {error}: {exc}")
    else:
        raise GuardError(f"negative test was accepted: {error}")


def run_self_test() -> None:
    values = current_values()
    validate_tree(values)
    expect_rejection(
        values, "board",
        "WifiBoard::SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);",
        "WifiBoard::SetPowerSaveLevel(level);",
        "keep WiFi awake",
    )
    expect_rejection(
        values, "board",
        "SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);\n#endif\n\n        InitializeWebSocketControlServer();",
        "// removed pre-server WiFi policy\n#endif\n\n        InitializeWebSocketControlServer();",
        "before local WebSocket",
    )
    expect_rejection(
        values,
        "kconfig",
        "depends on GOSHA_SAFE_NEUTRAL_BOOT_PROFILE\n    default n\n    help\n        Enables the separate local WebSocket Live namespace",
        "# removed safe-neutral dependency\n    default n\n    help\n        Enables the separate local WebSocket Live namespace",
        "SAFE_NEUTRAL",
    )
    expect_rejection(
        values,
        "ws",
        "MotionLiveAdapter::GetInstance().HandleWebSocketMessage",
        "MotionLiveAdapter::GetInstance().RemovedLiveHandler",
        "Live handler",
    )
    expect_rejection(
        values,
        "core",
        "seq != last_seq_ + 1",
        "false",
        "strict sequence",
    )
    expect_rejection(
        values,
        "core",
        "ValidateCommissioningTarget(servo_degrees, &reason)",
        "true",
        "one physical joint",
    )
    expect_rejection(
        values,
        "adapter",
        'cJSON_AddBoolToObject(reply, "commissioning", caps.commissioning);',
        "",
        "commissioning mode explicitly",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            run_self_test()
            print("check_gosha_v1_motion_live_profile: self-test PASS")
        else:
            validate_tree(current_values())
            print("check_gosha_v1_motion_live_profile: PASS")
        return 0
    except (json.JSONDecodeError, OSError, GuardError) as exc:
        print(f"check_gosha_v1_motion_live_profile: ERROR: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

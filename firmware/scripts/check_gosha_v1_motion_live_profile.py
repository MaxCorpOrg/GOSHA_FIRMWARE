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
RIGHT_ARM_OPT_IN = "CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN=y"


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
        flags = build.get("sdkconfig_append", [])
        require(
            LIVE_OPT_IN not in flags,
            "release config must not enable CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN by default",
        )
        require(
            RIGHT_ARM_OPT_IN not in flags,
            "release config must not enable CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN by default",
        )


def validate_kconfig(kconfig: str) -> None:
    live_body = kconfig_body(kconfig, "GOSHA_MOTION_LIVE_LOCAL_OPT_IN")
    for dependency in (
        "depends on BOARD_TYPE_GOSHA_V1",
        "depends on GOSHA_NO_MOTION_SAFE_PROFILE",
        "depends on GOSHA_SAFE_NEUTRAL_BOOT_PROFILE",
    ):
        require(dependency in live_body, f"Live opt-in must keep {dependency}")
    require("default n" in live_body, "Live opt-in must default to off")

    right_body = kconfig_body(kconfig, "GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN")
    for dependency in (
        "depends on BOARD_TYPE_GOSHA_V1",
        "depends on GOSHA_NO_MOTION_SAFE_PROFILE",
        "depends on GOSHA_SAFE_NEUTRAL_BOOT_PROFILE",
        "depends on GOSHA_MOTION_LIVE_LOCAL_OPT_IN",
    ):
        require(dependency in right_body, f"right-arm opt-in must keep {dependency}")
    require("default n" in right_body, "right-arm opt-in must default to off")
    require("GPIO12" in right_body and "initialize_right_arm" in right_body,
            "right-arm opt-in help must document GPIO12 and explicit initialization")


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


def validate_board_init(board: str) -> None:
    robot_init = extract_body(
        board,
        r"void\s+InitializeOttoController\s*\(\s*\)",
        "OttoRobot::InitializeOttoController",
    )
    require("kMotionLiveRightArmLocalOptIn" in board,
            "board must know the right-arm opt-in flag")
    require("control_config.left_hand_pin = GPIO_NUM_NC;" in robot_init,
            "left hand must be masked before controller handoff")
    require("control_config.right_hand_pin = GPIO_NUM_NC;" in robot_init,
            "right hand must default to NC before the non-camera right-arm opt-in branch")
    camera_pos = robot_init.find("if (has_camera_)")
    require(camera_pos >= 0, "camera fail-closed branch is missing")
    for field in ("left_leg_pin", "right_leg_pin", "left_foot_pin", "right_foot_pin"):
        require(f"control_config.{field} = GPIO_NUM_NC;" in robot_init[camera_pos:],
                f"camera fail-closed mask is missing for {field}")
    right_arm_assign = "control_config.right_hand_pin = hw_config_.right_hand_pin;"
    assign_pos = robot_init.find(right_arm_assign)
    require(assign_pos > camera_pos and "else" in robot_init[camera_pos:assign_pos],
            "right hand GPIO must be exposed only in the non-camera branch")
    require("kMotionLiveRightArmLocalOptIn" in robot_init[camera_pos:assign_pos],
            "right hand GPIO must require the right-arm opt-in flag")


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


def validate_adapter(adapter: str, adapter_h: str) -> None:
    require('kProtocol = "gosha.motion.live.v1"' not in adapter, "protocol constant belongs in core")
    require("PreparedProfileOrNull" in adapter, "adapter must use generated profile only through opt-in")
    require("CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN" in adapter, "adapter must check compile opt-in")
    require("#include GOSHA_MOTION_LIVE_PROFILE_HEADER" in adapter, "adapter must include generated header by macro")
    require("mbedtls_md_info_from_type(MBEDTLS_MD_SHA256)" in adapter, "adapter must hash access_key")
    require("access_key" not in "\n".join(line for line in adapter.splitlines() if "ESP_LOG" in line),
            "adapter must not log access_key")
    for token in (
        'cJSON_AddBoolToObject(feedback, "measured_position", false)',
        'cJSON_AddBoolToObject(feedback, "imu", false)',
        'cJSON_AddNullToObject(reply, "measured_pose")',
        'cJSON_AddNullToObject(reply, "tilt")',
    ):
        require(token in adapter, f"adapter must not synthesize feedback: {token}")
    for token in (
        'cJSON_AddStringToObject(reply, "mode", caps.mode)',
        'cJSON_AddBoolToObject(reply, "commissioning", caps.commissioning)',
        'cJSON_AddBoolToObject(reply, "initialization_required", caps.initialization_required)',
        'cJSON_AddBoolToObject(reply, "right_arm_available", caps.right_arm_available)',
        'cJSON_AddBoolToObject(reply, "right_arm_initialized", caps.right_arm_initialized)',
        'cJSON_AddStringToObject(reply, "initialization_op", caps.initialization_op)',
        'for (int i = 0; i < caps.joint_limit_count; ++i)',
    ):
        require(token in adapter, f"capabilities must report contract field: {token}")
    require("limits != nullptr && caps.motion_allowed" not in adapter,
            "capabilities must expose profile limits even while right-arm initialization is required")
    require("MotionLiveRightArmInitializer" in adapter_h,
            "adapter configure API must carry the right-arm initializer callback")
    require("esp_timer_start_periodic" in adapter and "gosha_live_watchdog" in adapter,
            "watchdog must use a nonblocking periodic timer")
    require('SendError(req, root, "bad_json"' in adapter, "bad JSON in Live namespace must not fall through to MCP")

    init_body = extract_body(adapter, r"if\s*\(std::strcmp\(op,\s*\"initialize_right_arm\"\)\s*==\s*0\)",
                             "initialize_right_arm handler")
    require('ReadString(root, "request_id", &request_id)' in init_body,
            "initialize_right_arm must require request_id")
    require('ReadString(root, "calibration_id", &calibration_id)' in init_body,
            "initialize_right_arm must require calibration_id")
    require("AccessKeyMatches" in init_body, "initialize_right_arm must authenticate the access key")
    require("core_.InitializeRightArm" in init_body,
            "initialize_right_arm must call the core initializer")
    require("core_.GetCapabilities()" in init_body and "SendCapabilities(req, root, caps)" in init_body,
            "initialize_right_arm success must respond with capabilities and copied request_id")


def validate_core(core: str, core_h: str) -> None:
    for token in (
        '"gosha.motion.live.v1"',
        '"gosha-preview-v1"',
        "kPoseJointCount = 6",
        "kActiveJointCount = 4",
        "kMaxActiveJointCount = 5",
        "kWatchdogMs = 300",
        "kMaxServoRateDps = 30",
        "kRightArmHomeDegrees = 135",
        "kProfileModeVerified",
        "kProfileModeCommissioning",
        "kProfileModeCommissioningRightArm",
        "kCommissioningJointLimitDegrees = 1.0",
        "kCommissioningMaxServoRateDps = 1.0",
        "kCommissioningRightArmJointLimitDegrees = 5.0",
        '"leg_negative_x"',
        '"leg_positive_x"',
        '"foot_negative_x"',
        '"foot_positive_x"',
        '"arm_negative_x"',
        '"arm_positive_x"',
        "ServoSlot::kLeftHand",
        "ServoSlot::kRightHand",
        "MotionLiveRightArmInitializer",
        "right_arm_initialized_",
        "right_arm_initialization_failed_",
        "right_arm_initialization_required",
        "right_arm_initialization_failed",
        "InitializeRightArm",
        "InitializeRightArmHardware",
        "ValidateBaseSafety",
        "ProfileNeedsRightArmInitialization",
        "ProfileRequiresSingleJointSession",
        "ProfileJointCount",
    ):
        require(token in core + core_h, f"core contract token is missing: {token}")
    require("motion_allowed = false" in core_h, "capabilities must default motion_allowed=false")
    require("const char* mode = kProfileModeVerified" in core_h,
            "prepared profile mode must default to verified for old generated initializers")
    require("int joint_count = kActiveJointCount" in core_h,
            "prepared profile joint_count must default to four lower-body joints")
    require("std::array<MotionLiveJointProfile, kMaxActiveJointCount> joints" in core_h,
            "prepared profile storage must have a 5-entry max without changing the six servo slots")
    require("std::array<int, kPoseJointCount> servo_degrees" in core_h,
            "hardware result must address all physical servo slots")
    require("MotionLiveHardwareApplier = std::function<bool(const std::array<int, kPoseJointCount>&)>" in core_h,
            "hardware applier must receive all six physical servo slots")
    require("bool commissioning = false" in core_h,
            "capabilities must default commissioning=false")
    require("ValidatePreparedProfile" in core and "ValidateRuntimeAgainstProfile" in core,
            "core must validate generated profile and runtime binding")
    require("local_opt_in_enabled_" in core_h and "profile_ = nullptr" in core_h,
            "core must fail closed without local opt-in/profile")
    require("seq != last_seq_ + 1" in core, "core must enforce strict sequence numbers")
    require("target.has_unknown_joint" in core and "present && !active" in core,
            "core must reject unknown or unavailable joints in pose target")
    require("speed_dps > kMaxServoRateDps" in core and "speed_dps > joint.max_speed_dps" in core,
            "core must enforce <=30 dps and per-joint speed")
    require("joint.min_relative_degrees != -limit" in core and
            "joint.max_relative_degrees != limit" in core and
            "joint.max_speed_dps > kCommissioningMaxServoRateDps" in core,
            "commissioning modes must force exact prepared limits and <=1 dps")
    require("joint.direction != 1 ||\n                 joint.servo_index" in core and
            "kRightArmHomeDegrees - 5" in core and
            "kRightArmHomeDegrees + 5" in core,
            "right arm must be exact direction +1 and servo 130..140 from home135")
    require("ProfileIsCommissioningRightArm() && joint_count != kMaxActiveJointCount" in core and
            "!ProfileIsCommissioningRightArm() && joint_count != kActiveJointCount" in core,
            "new profile must be five joints while old profiles stay four joints")
    require("seen_servos[static_cast<int>(ServoSlot::kLeftHand)]" in core and
            "seen_servos[static_cast<int>(ServoSlot::kRightHand)]" in core,
            "profile validation must distinguish left-hand slot4 from right-hand slot5")
    require("right_hand.pin != ExpectedPinForServoIndex(static_cast<int>(ServoSlot::kRightHand))" in core and
            "right_hand.attached != right_arm_initialized_" in core,
            "runtime validation must gate right-hand GPIO12 attachment state")
    require("caps.commissioning = ProfileRequiresSingleJointSession()" in core and
            "caps.calibrated = !caps.commissioning" in core and
            "caps.mode = profile_->mode" in core,
            "capabilities must distinguish commissioning modes from verified calibration")
    require("caps.initialization_required" in core and '"initialize_right_arm"' in core,
            "capabilities must expose explicit right-arm initialization")
    require("ValidateCommissioningTarget" in core and "session_initial_servo_degrees_" in core_h and
            "commissioning_servo_index_" in core_h and "kCommissioningSingleJoint" in core,
            "commissioning mode must track one physical joint per session")
    require("!ValidateCommissioningTarget(servo_degrees, &reason)" in core,
            "pose must call the commissioning one physical joint gate")
    require("std::abs(delta) > max_delta" in core and "changed_count > 1" in core,
            "commissioning modes must reject multi-joint and over-baseline deltas")
    require("slot == static_cast<int>(ServoSlot::kRightHand)" in core and
            "kCommissioningRightArmJointLimitDegrees" in core,
            "right-arm commissioning must use the 5 degree baseline limit")
    require("commissioning_servo_index_ = changed_slot" in core,
            "commissioning mode must lock the first changed physical joint")
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

    arm_body = extract_body(core, r"MotionLiveResult\s+MotionLiveCore::Arm\s*\([^)]*\)",
                            "MotionLiveCore::Arm")
    require("InitializeRightArmHardware" not in arm_body and "ApplyHardware" not in arm_body,
            "ARM must not attach or move hardware")
    init_body = extract_body(core, r"MotionLiveResult\s+MotionLiveCore::InitializeRightArm\s*\([^)]*\)",
                             "MotionLiveCore::InitializeRightArm")
    require("armed_" in init_body and "kSessionBusy" in init_body,
            "initialize_right_arm must be rejected while a session is active")
    require("ValidateBaseSafety" in init_body and "EvaluateSafety" not in init_body,
            "initialize_right_arm must run base safety without blocking itself on initialization_required")
    require("right_arm_initialized_" in init_body and "InitializeRightArmHardware(kRightArmHomeDegrees)" in init_body,
            "initialize_right_arm must exact-once attach and hold home135")
    require("right_arm_initialization_failed_ = true" in init_body,
            "initialize_right_arm failure must latch closed without auto retries")
    keepalive_body = extract_body(core, r"MotionLiveResult\s+MotionLiveCore::Keepalive\s*\([^)]*\)",
                                  "MotionLiveCore::Keepalive")
    require("!ProfileIsCommissioningRightArm()" in keepalive_body and "StepTowardTarget" in keepalive_body,
            "right-arm keepalive must not advance motion")
    tick_body = extract_body(core, r"MotionLiveResult\s+MotionLiveCore::Tick\s*\([^)]*\)",
                             "MotionLiveCore::Tick")
    require("!ProfileIsCommissioningRightArm()" in tick_body and "StepTowardTarget" in tick_body,
            "right-arm timer tick must not advance motion")


def validate_controller(controller: str, movements: str) -> None:
    require("ConfigureMotionLiveAdapter" in controller, "controller must provide runtime binding to Live adapter")
    require("safe_neutral_hold_commanded_" in controller, "runtime binding must know whether neutral hold ran")
    for token in (
        "CONFIG_GOSHA_MOTION_LIVE_RIGHT_ARM_LOCAL_OPT_IN",
        "kMotionLiveRightArmLocalOptIn",
        "right_arm_live_pinset",
        "GPIO_NUM_12",
        "const bool attach_servos = !kNoMotionSafeProfile",
        "runtime.no_motion_safe_profile = kNoMotionSafeProfile",
        "runtime.safe_neutral_boot_profile = kSafeNeutralBootProfile",
        "runtime.safe_neutral_commanded = safe_neutral_hold_commanded_",
        "runtime.lower_body_attached = safe_neutral_hold_commanded_ && has_complete_legs_feet_",
        "ServoSlot::kLeftLeg",
        "ServoSlot::kRightLeg",
        "ServoSlot::kLeftFoot",
        "ServoSlot::kRightFoot",
        "ServoSlot::kLeftHand",
        "ServoSlot::kRightHand",
        "kRightArmHomeDegrees",
        "ApplyLiveServoPositions",
        "AttachRightHandAtHome",
        "AttachLegsFeetServos",
        "MotionLiveAdapter::GetInstance().ConfigureRuntime(",
        "right_arm_initializer",
        "GPIO_NUM_NC",
    ):
        require(token in controller, f"runtime binding is missing: {token}")
    require("has_left_hand_" in controller and "has_right_hand_" in controller,
            "controller must not require both hands for right-hand trim availability")
    require("!has_left_hand_" in controller and "!has_right_hand_" in controller,
            "set_trim must distinguish left and right hand availability")
    require("Профиль no-motion включён: инструменты движения, Home, set_trim и servo sequence не зарегистрированы" in controller,
            "no-motion build must keep legacy MCP motion/Home/trim/sequence tools closed")

    attach_lower_body = extract_body(movements, r"void\s+Otto::AttachLegsFeetServos\s*\(\s*\)",
                                     "AttachLegsFeetServos")
    require("i < kLegAndFootServoCount" in attach_lower_body and "i < SERVO_COUNT" not in attach_lower_body,
            "safe-neutral boot attach must iterate only legs/feet")
    hold_body = extract_body(movements, r"void\s+Otto::HoldLegsFeetAtNeutral\s*\(\s*\)",
                             "HoldLegsFeetAtNeutral")
    require("i < kLegAndFootServoCount" in hold_body and "i < SERVO_COUNT" not in hold_body,
            "neutral hold must iterate only legs/feet")
    require("LEFT_HAND" not in hold_body and "RIGHT_HAND" not in hold_body,
            "neutral hold must not mention hand channels")
    apply_lower_body = extract_body(movements, r"bool\s+Otto::ApplyLegsFeetPositions\s*\([^)]*\)",
                                    "ApplyLegsFeetPositions")
    require("i < kLegAndFootServoCount" in apply_lower_body,
            "legacy Live lower-body apply must iterate only legs/feet")
    require("RIGHT_HAND" not in apply_lower_body and "LEFT_HAND" not in apply_lower_body,
            "legacy Live lower-body apply must not touch hands")
    right_init_body = extract_body(movements, r"bool\s+Otto::AttachRightHandAtHome\s*\([^)]*\)",
                                   "AttachRightHandAtHome")
    require("servo_pins_[LEFT_HAND] != -1" in right_init_body,
            "right-arm initializer must fail if the left hand is available")
    require("servo_[RIGHT_HAND].Attach" in right_init_body and
            "servo_[RIGHT_HAND].SetPosition(home_degrees)" in right_init_body,
            "right-arm initializer must attach and hold only the right hand")
    require("servo_[LEFT_HAND]" not in right_init_body,
            "right-arm initializer must not touch the left-hand servo object")
    live_apply_body = extract_body(movements, r"bool\s+Otto::ApplyLiveServoPositions\s*\([^)]*\)",
                                   "ApplyLiveServoPositions")
    require("servo_pins_[LEFT_HAND] != -1" in live_apply_body,
            "Live apply must fail closed if the left hand is available")
    require("i < kLegAndFootServoCount" in live_apply_body,
            "Live apply must always include only lower body plus the right hand")
    require("servo_[RIGHT_HAND].SetPosition" in live_apply_body,
            "Live apply must be able to command the initialized right hand")
    require("servo_[LEFT_HAND].SetPosition" not in live_apply_body,
            "Live apply must never command the left hand")
    set_trims_body = extract_body(movements, r"void\s+Otto::SetTrims\s*\([^)]*\)", "Otto::SetTrims")
    require("has_left_hand_" in set_trims_body and "has_right_hand_" in set_trims_body,
            "Otto trims must track a single available right hand")


def validate_prepare(prepare: str) -> None:
    for token in (
        "DEFAULT_OUTPUT = ROOT / \"local_only/gosha_motion_live_profile.h\"",
        "PROFILE_MODE_VERIFIED = \"verified\"",
        "PROFILE_MODE_COMMISSIONING = \"commissioning\"",
        "PROFILE_MODE_COMMISSIONING_RIGHT_ARM = \"commissioning_right_arm\"",
        "RIGHT_ARM_HOME_DEGREES = 135",
        "RIGHT_ARM_LIMIT_DEGREES = 5.0",
        '"right_hand": {"servo_group": "arm", "servo_index": 5, "pin": 12',
        '"left_hand": {"servo_group": "arm", "servo_index": 4, "pin": 8',
        '"arm_positive_x"',
        '"access_key"',
        "hashlib.sha256",
        "calibration_payload",
        "\"mode\": mode",
        "sort_keys=True",
        "calibration_id is computed",
        "plaintext access_key was not written",
        "mode must be verified, commissioning or commissioning_right_arm",
        "commissioning limits must be exactly",
        "commissioning max_speed_dps must be <=",
        "arm_positive_x must bind to right_hand slot5 GPIO12",
        "left_hand is unavailable",
        "direction +1",
        "relative -5 commands servo 130",
        "rightHome135",
        "servo_key must be explicit",
        "servo_key must stay within",
        '"pin": 17',
        '"pin": 39',
        '"pin": 18',
        '"pin": 38',
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
    validate_board_init(values["board"])
    validate_websocket(values["ws"])
    validate_adapter(values["adapter"], values["adapter_h"])
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
        "adapter_h": read(ROOT / "main/boards/gosha-v1/motion_live_adapter.h"),
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
    require(needle in mutated[key], f"self-test mutation needle missing: {needle}")
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
        "depends on GOSHA_MOTION_LIVE_LOCAL_OPT_IN\n    default n\n    help\n        Adds the separate commissioning_right_arm",
        "# removed Live dependency\n    default n\n    help\n        Adds the separate commissioning_right_arm",
        "GOSHA_MOTION_LIVE_LOCAL_OPT_IN",
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
        "adapter",
        'if (std::strcmp(op, "initialize_right_arm") == 0)',
        'if (std::strcmp(op, "removed_initialize_right_arm") == 0)',
        "initialize_right_arm",
    )
    expect_rejection(
        values,
        "adapter",
        'cJSON_AddBoolToObject(reply, "initialization_required", caps.initialization_required);',
        "",
        "initialization_required",
    )
    expect_rejection(
        values,
        "core_h",
        "kRightArmHomeDegrees = 135",
        "kRightArmHomeDegrees = 45",
        "kRightArmHomeDegrees = 135",
    )
    expect_rejection(
        values,
        "core",
        "joint.direction != 1 ||",
        "false ||",
        "direction +1",
    )
    expect_rejection(
        values,
        "core",
        "std::abs(delta) > max_delta",
        "false",
        "over-baseline",
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
        "core",
        "if (!ProfileIsCommissioningRightArm()) {\n        if (!StepTowardTarget(now_ms, &reason, &hardware_changed))",
        "if (true) {\n        if (!StepTowardTarget(now_ms, &reason, &hardware_changed))",
        "keepalive",
    )
    expect_rejection(
        values,
        "board",
        "control_config.right_hand_pin = hw_config_.right_hand_pin;",
        "control_config.right_hand_pin = GPIO_NUM_12;",
        "right hand GPIO",
    )
    expect_rejection(
        values,
        "movements",
        "servo_[RIGHT_HAND].SetPosition(home_degrees);",
        "servo_[LEFT_HAND].SetPosition(home_degrees);",
        "right-arm initializer",
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

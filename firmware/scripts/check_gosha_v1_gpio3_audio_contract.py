#!/usr/bin/env python3
"""Static guard for gosha-v1 GPIO3 reuse and audio sample-rate contract."""

import argparse
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
GOSHA_CONFIG = ROOT / "main/boards/gosha-v1/config.h"
GOSHA_BOARD = ROOT / "main/boards/gosha-v1/otto_robot.cc"
PROTOCOL_H = ROOT / "main/protocols/protocol.h"
PROTOCOL_CC = ROOT / "main/protocols/protocol.cc"
WEBSOCKET = ROOT / "main/protocols/websocket_protocol.cc"
MQTT = ROOT / "main/protocols/mqtt_protocol.cc"
APPLICATION = ROOT / "main/application.cc"


class GuardError(Exception):
    """Static guard failure."""


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GuardError(message)


def extract_function_body(source: str, name: str) -> str:
    match = re.search(rf"\b(?:void|bool|std::string)\s+{re.escape(name)}\s*\([^)]*\)\s*(?:const\s*)?\{{", source)
    require(match is not None, f"{name} was not found")
    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    require(depth == 0, f"{name} body was not closed")
    return source[start : pos - 1]


def validate_gpio3_release(config: str, board: str) -> None:
    require("#define CAMERA_XCLK (GPIO_NUM_3)" in config, "camera probe XCLK is no longer GPIO3")
    require(
        re.search(r"\.display_backlight_pin\s*=\s*GPIO_NUM_3\s*,", config) is not None,
        "gosha-v1 non-camera backlight is no longer GPIO3",
    )

    release_body = extract_function_body(board, "ReleaseCameraProbePwm")
    require(
        "ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0)" in release_body,
        "ReleaseCameraProbePwm must stop the camera probe LEDC channel",
    )
    require(
        "gpio_reset_pin(CAMERA_XCLK)" in release_body,
        "ReleaseCameraProbePwm must reset CAMERA_XCLK so ESP-IDF revokes the GPIO reservation",
    )
    require(
        "esp_gpio_revoke" not in board,
        "gosha-v1 GPIO3 cleanup must use public gpio_reset_pin instead of private esp_gpio_revoke",
    )

    detect_body = extract_function_body(board, "DetectHardwareVersion")
    require(
        detect_body.count("ReleaseCameraProbePwm();") >= 2,
        "DetectHardwareVersion must release camera probe PWM on I2C failure and no-camera fallback",
    )
    require(
        "ReleaseCameraProbePwm();\n            return false;" in detect_body,
        "I2C bus creation failure must release camera probe PWM before returning false",
    )
    require(
        re.search(
            r"if\s*\(!camera_found\)\s*\{(?P<body>.*?)camera_type_\s*=\s*OTTO_CAMERA_NONE\s*;",
            detect_body,
            flags=re.DOTALL,
        )
        and "ReleaseCameraProbePwm();" in re.search(
            r"if\s*\(!camera_found\)\s*\{(?P<body>.*?)camera_type_\s*=\s*OTTO_CAMERA_NONE\s*;",
            detect_body,
            flags=re.DOTALL,
        ).group("body"),
        "no-camera fallback must release camera probe PWM before using GPIO3 for backlight",
    )


def validate_audio_contract(
    config: str,
    protocol_h: str,
    protocol_cc: str,
    websocket: str,
    mqtt: str,
    application: str,
) -> None:
    require(
        re.search(r"\.audio_input_sample_rate\s*=\s*16000\s*,", config) is not None,
        "gosha-v1 non-camera input/uplink sample rate must stay 16000",
    )
    require(
        re.search(r"\.audio_output_sample_rate\s*=\s*24000\s*,", config) is not None,
        "gosha-v1 non-camera codec output sample rate must stay 24000",
    )

    for constant in (
        "kLegacyAudioSampleRate = 16000",
        "kAudioInputSampleRate = 16000",
        "kAudioUplinkSampleRate = 16000",
    ):
        require(constant in protocol_h, f"Protocol is missing {constant}")

    add_audio_params = extract_function_body(protocol_cc, "Protocol::AddAudioParams")
    required_terms = (
        '"format", "opus"',
        '"sample_rate", kLegacyAudioSampleRate',
        '"input_sample_rate", kAudioInputSampleRate',
        '"uplink_sample_rate", kAudioUplinkSampleRate',
        '"output_sample_rate", output_sample_rate',
        "codec->output_sample_rate()",
        '"channels", 1',
    )
    for term in required_terms:
        require(term in add_audio_params, f"AddAudioParams is missing {term}")

    require("AddAudioParams(root, OPUS_FRAME_DURATION_MS);" in websocket,
            "WebSocket hello must use the shared audio params contract")
    require("AddAudioParams(root, OPUS_FRAME_DURATION_MS);" in mqtt,
            "MQTT hello must use the shared audio params contract")
    for source_name, source in (("websocket_protocol.cc", websocket), ("mqtt_protocol.cc", mqtt)):
        legacy_only = re.search(
            r'cJSON_AddNumberToObject\(audio_params,\s*"sample_rate",\s*16000\)',
            source,
        )
        require(legacy_only is None, f"{source_name} still builds a legacy-only sample_rate hello")

    require("ESP_LOGW(TAG, \"Server sample rate %d does not match device output sample rate %d" not in application,
            "expected 16000->24000 output resampling must not stay a warning-only contract")
    require("Audio contract: uplink/input %d Hz" in application,
            "application log must describe the explicit uplink/downlink/output audio contract")


def validate_current_tree() -> None:
    validate_gpio3_release(read(GOSHA_CONFIG), read(GOSHA_BOARD))
    validate_audio_contract(
        read(GOSHA_CONFIG),
        read(PROTOCOL_H),
        read(PROTOCOL_CC),
        read(WEBSOCKET),
        read(MQTT),
        read(APPLICATION),
    )


def run_self_test() -> None:
    config = read(GOSHA_CONFIG)
    board = read(GOSHA_BOARD)
    protocol_h = read(PROTOCOL_H)
    protocol_cc = read(PROTOCOL_CC)
    websocket = read(WEBSOCKET)
    mqtt = read(MQTT)
    application = read(APPLICATION)

    validate_gpio3_release(config, board)
    validate_audio_contract(config, protocol_h, protocol_cc, websocket, mqtt, application)

    try:
        validate_gpio3_release(config, board.replace("gpio_reset_pin(CAMERA_XCLK)", "ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, 0)"))
    except GuardError as exc:
        require("reset CAMERA_XCLK" in str(exc), "GPIO3 negative test failed to name CAMERA_XCLK reset")
    else:
        raise GuardError("GPIO3 negative test failed: missing gpio_reset_pin was accepted")

    try:
        validate_audio_contract(
            config,
            protocol_h,
            protocol_cc.replace('"output_sample_rate", output_sample_rate', '"sample_rate", kLegacyAudioSampleRate'),
            websocket,
            mqtt,
            application,
        )
    except GuardError as exc:
        require("output_sample_rate" in str(exc), "audio negative test failed to name output_sample_rate")
    else:
        raise GuardError("audio negative test failed: missing output_sample_rate was accepted")

    print("gosha-v1 GPIO3/audio contract guard self-test passed")


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
            print("gosha-v1 GPIO3/audio contract guard passed")
    except GuardError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Static guard for gosha-v1 non-camera GPIO ownership."""

import argparse
from collections import defaultdict
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "main/boards/gosha-v1/config.h"

NON_CAMERA_CONFIG_RE = re.compile(
    r"constexpr\s+HardwareConfig\s+NON_CAMERA_VERSION_CONFIG\s*=\s*\{"
    r"(?P<body>.*?)"
    r"\n\};",
    re.DOTALL,
)
GPIO_FIELD_RE = re.compile(
    r"\.(?P<field>[a-z0-9_]+)\s*=\s*(?P<gpio>GPIO_NUM_(?:NC|\d+))\s*,"
)
BOOT_BUTTON_RE = re.compile(
    r"^\s*#define\s+BOOT_BUTTON_GPIO\s+(?P<gpio>GPIO_NUM_(?:NC|\d+))\b.*$",
    re.MULTILINE,
)

CRITICAL_GPIO_ROLES = {
    "BOOT_BUTTON_GPIO": "boot button",
    "power_charge_detect_pin": "power charge detect",
    "right_leg_pin": "right leg servo",
    "right_foot_pin": "right foot servo",
    "left_leg_pin": "left leg servo",
    "left_foot_pin": "left foot servo",
    "left_hand_pin": "left hand servo",
    "right_hand_pin": "right hand servo",
    "audio_i2s_gpio_ws": "shared audio WS",
    "audio_i2s_gpio_bclk": "shared audio BCLK",
    "audio_i2s_gpio_din": "shared audio DIN",
    "audio_i2s_gpio_dout": "shared audio DOUT",
    "audio_i2s_mic_gpio_ws": "microphone WS",
    "audio_i2s_mic_gpio_sck": "microphone SCK",
    "audio_i2s_mic_gpio_din": "microphone DIN",
    "audio_i2s_spk_gpio_dout": "speaker DOUT",
    "audio_i2s_spk_gpio_bclk": "speaker BCLK",
    "audio_i2s_spk_gpio_lrck": "speaker LRCK",
    "display_backlight_pin": "display backlight",
    "display_mosi_pin": "display MOSI",
    "display_clk_pin": "display CLK",
    "display_dc_pin": "display DC",
    "display_rst_pin": "display RST",
    "display_cs_pin": "display CS",
    "i2c_sda_pin": "I2C SDA",
    "i2c_scl_pin": "I2C SCL",
}

INTENTIONAL_SHARED_VALUES = {
    # GPIO_NUM_NC means "not connected"; repeated NC entries are expected.
    "GPIO_NUM_NC",
}


class GuardError(Exception):
    """Pin map guard failure."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GuardError(message)


def parse_boot_button_gpio(source: str) -> str:
    matches = list(BOOT_BUTTON_RE.finditer(source))
    require(len(matches) == 1, "BOOT_BUTTON_GPIO define was not found exactly once")
    return matches[0].group("gpio")


def parse_non_camera_gpio_roles(source: str) -> dict[str, str]:
    config_match = NON_CAMERA_CONFIG_RE.search(source)
    require(config_match is not None, "NON_CAMERA_VERSION_CONFIG was not found")

    return {
        match.group("field"): match.group("gpio")
        for match in GPIO_FIELD_RE.finditer(config_match.group("body"))
        if match.group("field") in CRITICAL_GPIO_ROLES
    }


def validate_source(source: str) -> None:
    observed = parse_non_camera_gpio_roles(source)
    observed["BOOT_BUTTON_GPIO"] = parse_boot_button_gpio(source)

    missing = sorted(set(CRITICAL_GPIO_ROLES) - set(observed))
    require(not missing, f"missing critical non-camera GPIO fields: {', '.join(missing)}")

    owners_by_gpio: dict[str, list[str]] = defaultdict(list)
    for field, gpio in observed.items():
        if gpio in INTENTIONAL_SHARED_VALUES:
            continue
        owners_by_gpio[gpio].append(f"{field} ({CRITICAL_GPIO_ROLES[field]})")

    duplicates = {
        gpio: owners
        for gpio, owners in owners_by_gpio.items()
        if len(owners) > 1
    }
    require(
        not duplicates,
        "active non-camera GPIO is assigned to multiple critical roles: "
        + "; ".join(
            f"{gpio}: {', '.join(owners)}"
            for gpio, owners in sorted(duplicates.items())
        ),
    )


def replace_non_camera_gpio(source: str, field: str, gpio: str) -> str:
    config_match = NON_CAMERA_CONFIG_RE.search(source)
    require(config_match is not None, "NON_CAMERA_VERSION_CONFIG was not found")

    body = config_match.group("body")
    field_re = re.compile(
        rf"(?P<prefix>\.{re.escape(field)}\s*=\s*)"
        r"GPIO_NUM_(?:NC|\d+)"
        r"(?P<suffix>\s*,)"
    )
    patched_body, count = field_re.subn(
        lambda match: f"{match.group('prefix')}{gpio}{match.group('suffix')}",
        body,
        count=1,
    )
    require(count == 1, f"{field} was not found exactly once in NON_CAMERA_VERSION_CONFIG")
    return (
        source[: config_match.start("body")]
        + patched_body
        + source[config_match.end("body") :]
    )


def run_self_test() -> None:
    source = CONFIG.read_text(encoding="utf-8")
    validate_source(source)

    conflict_source = replace_non_camera_gpio(source, "display_cs_pin", "GPIO_NUM_0")
    try:
        validate_source(conflict_source)
    except GuardError as exc:
        message = str(exc)
        required_tokens = ("GPIO_NUM_0", "display_cs_pin", "BOOT_BUTTON_GPIO")
        missing_tokens = [token for token in required_tokens if token not in message]
        require(
            not missing_tokens,
            "GPIO0 conflict regression failed to name: " + ", ".join(missing_tokens),
        )
    else:
        raise GuardError(
            "GPIO0 conflict regression failed: display_cs_pin=GPIO_NUM_0 "
            "did not conflict with BOOT_BUTTON_GPIO"
        )

    print("gosha-v1 non-camera pin map guard self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="prove current map passes and display_cs_pin=GPIO_NUM_0 fails",
    )
    args = parser.parse_args()

    try:
        if args.self_test:
            run_self_test()
        else:
            validate_source(CONFIG.read_text(encoding="utf-8"))
            print("gosha-v1 non-camera pin map guard passed")
    except GuardError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()

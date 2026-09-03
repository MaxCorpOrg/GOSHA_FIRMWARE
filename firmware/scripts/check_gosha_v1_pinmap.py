#!/usr/bin/env python3
"""Static guard for gosha-v1 non-camera GPIO ownership."""

from collections import defaultdict
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "main/boards/gosha-v1/config.h"

NON_CAMERA_CONFIG_RE = re.compile(
    r"constexpr\s+HardwareConfig\s+NON_CAMERA_VERSION_CONFIG\s*=\s*\{(?P<body>.*?)\n\};",
    re.DOTALL,
)
GPIO_FIELD_RE = re.compile(
    r"\.(?P<field>[a-z0-9_]+)\s*=\s*(?P<gpio>GPIO_NUM_(?:NC|\d+))\s*,"
)

CRITICAL_GPIO_ROLES = {
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


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> None:
    source = CONFIG.read_text(encoding="utf-8")
    config_match = NON_CAMERA_CONFIG_RE.search(source)
    require(config_match is not None, "NON_CAMERA_VERSION_CONFIG was not found")

    observed = {
        match.group("field"): match.group("gpio")
        for match in GPIO_FIELD_RE.finditer(config_match.group("body"))
        if match.group("field") in CRITICAL_GPIO_ROLES
    }
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

    print("gosha-v1 non-camera pin map guard passed")


if __name__ == "__main__":
    main()

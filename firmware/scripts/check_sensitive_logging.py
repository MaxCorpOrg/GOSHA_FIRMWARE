#!/usr/bin/env python3
"""Static guard for firmware diagnostic redaction."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> None:
    application = read("main/application.cc")
    assets = read("main/assets.cc")
    ota = read("main/ota.cc")
    mcp = read("main/mcp_server.cc")
    websocket = read("main/protocols/websocket_protocol.cc")
    afsk = read("main/boards/common/afsk_demod.cc")
    helper = read("main/diagnostic_redaction.cc")
    cmake = read("main/CMakeLists.txt")

    for name, source in {
        "application.cc": application,
        "assets.cc": assets,
        "ota.cc": ota,
        "mcp_server.cc": mcp,
        "websocket_protocol.cc": websocket,
    }.items():
        require("RedactUrlForDiagnostics" in source, f"{name}: upgrade URL log is not redacted")

    combined_sources = "\n".join((application, assets, ota, mcp, websocket))
    for forbidden in (
        "Starting firmware upgrade from URL: %s",
        "Upgrading firmware from %s\", firmware_url.c_str()",
        "User requested firmware upgrade from URL: %s",
        "Downloading new version of assets from %s\", url.c_str()",
        "Connecting to websocket server: %s with version: %d\", url.c_str()",
        "Upload snapshot %u bytes to %s\", jpeg_data.size(), url.c_str()",
        "Failed to open URL: \" + url",
        "Failed to allocate memory for image: \" + url",
        "Failed to download image: \" + url",
        "FOUND_NEW_ASSETS, download_url.c_str()",
        "http->ReadAll().c_str()",
        "Failed to activate, code: %d, body: %s",
    ):
        require(forbidden not in combined_sources, f"forbidden full upgrade URL log remains: {forbidden}")

    require("userinfo" in helper, "diagnostic URL redaction must mark userinfo as redacted")
    require("query" in helper, "diagnostic URL redaction must mark query as redacted")
    require("fragment" in helper, "diagnostic URL redaction must mark fragments as redacted")
    require("scheme=" in helper, "diagnostic URL redaction must expose only the URL scheme")
    require("len=" not in helper, "diagnostic URL redaction must not expose full URL length")
    require("host_port" not in helper, "diagnostic URL redaction must not preserve host or port")
    require('"diagnostic_redaction.cc"' in cmake, "diagnostic redaction helper must be compiled")

    require("Received text data: %s" not in afsk, "AFSK must not log full decoded Wi-Fi text")
    require("decoded_text->c_str()" not in afsk, "AFSK must not print full decoded Wi-Fi text")
    require("WiFi credentials received" in afsk, "AFSK screen diagnostic should stay understandable")
    require("password length" in afsk, "AFSK log should keep non-secret diagnostic lengths")

    log_calls = re.findall(r"ESP_LOG[IEWD]\([^;]+;", combined_sources, flags=re.DOTALL)
    for log_call in log_calls:
        require(
            not re.search(r"(?<!diagnostic_)url\.c_str\(\)", log_call),
            f"full URL still reaches a log call: {log_call[:80]}",
        )
        require(
            not re.search(r"\bfirmware_url\.c_str\(\)", log_call),
            f"full firmware URL still reaches a log call: {log_call[:80]}",
        )
        require(
            not re.search(r"\bdownload_url\.c_str\(\)", log_call),
            f"full assets URL still reaches a log call: {log_call[:80]}",
        )


if __name__ == "__main__":
    main()

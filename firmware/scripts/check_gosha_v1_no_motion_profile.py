#!/usr/bin/env python3
"""Static guard for the gosha-v1 no-motion safe profile."""

import argparse
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CONFIG_JSON = ROOT / "main/boards/gosha-v1/config.json"
KCONFIG = ROOT / "main/Kconfig.projbuild"
OTTO_CONTROLLER = ROOT / "main/boards/gosha-v1/otto_controller.cc"
OTTO_MOVEMENTS_CC = ROOT / "main/boards/gosha-v1/otto_movements.cc"
OTTO_MOVEMENTS_H = ROOT / "main/boards/gosha-v1/otto_movements.h"
MCP_SERVER = ROOT / "main/mcp_server.cc"
APPLICATION = ROOT / "main/application.cc"
ASSETS = ROOT / "main/assets.cc"
RELEASE = ROOT / "scripts/release.py"

NO_MOTION_FLAG = "CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y"
UPGRADE_TOOL = '"self.upgrade_firmware"'
REBOOT_TOOL = '"self.reboot"'
ASSETS_DOWNLOAD_URL_TOOL = '"self.assets.set_download_url"'

DANGEROUS_TOOLS = (
    '"self.otto.action"',
    '"self.otto.servo_sequences"',
    '"self.otto.stop"',
    '"self.otto.set_trim"',
    '"self.otto.get_trims"',
)

SAFE_TOOLS = (
    '"self.otto.get_status"',
    '"self.battery.get_level"',
    '"self.otto.get_ip"',
)

ALLOWED_UPGRADE_CALL_LINES = {
    (
        "main/application.cc",
        "} else if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {",
    ),
    (
        "main/application.cc",
        "bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {",
    ),
    (
        "main/application.h",
        'bool UpgradeFirmware(const std::string& url, const std::string& version = "");',
    ),
    ("main/mcp_server.cc", "bool success = app.UpgradeFirmware(url);"),
}

ALLOWED_ASSETS_DOWNLOAD_CALL_LINES = {
    (
        "main/application.cc",
        "bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {",
    ),
    (
        "main/assets.cc",
        "bool Assets::Download(std::string url, std::function<void(int progress, size_t speed)> progress_callback) {",
    ),
}

ALLOWED_PARTITION_WRITE_CALL_LINES = {
    (
        "main/assets.cc",
        "esp_err_t err = esp_partition_write(partition_, total_written, buffer, ret);",
    ),
}


class GuardError(Exception):
    """No-motion static guard failure."""


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def collect_main_sources() -> dict[str, str]:
    sources: dict[str, str] = {}
    for path in (ROOT / "main").rglob("*"):
        if path.suffix in {".cc", ".cpp", ".h", ".hpp"}:
            sources[path.relative_to(ROOT).as_posix()] = read(path)
    return sources


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


def protected_intervals(source: str, guard_name: str = "kNoMotionSafeProfile") -> list[tuple[int, int]]:
    intervals: list[tuple[int, int]] = []
    pattern = rf"if\s*\(\s*!{re.escape(guard_name)}\s*\)\s*\{{"
    for match in re.finditer(pattern, source):
        brace = source.find("{", match.end() - 1)
        depth = 1
        pos = brace + 1
        while pos < len(source) and depth:
            if source[pos] == "{":
                depth += 1
            elif source[pos] == "}":
                depth -= 1
            pos += 1
        require(depth == 0, f"if (!{guard_name}) block was not closed")
        intervals.append((brace, pos))
    return intervals


def inside_any(position: int, intervals: list[tuple[int, int]]) -> bool:
    return any(start <= position <= end for start, end in intervals)


def validate_config(config_json: str, kconfig: str) -> None:
    data = json.loads(config_json)
    gosha_build = next(
        (
            build
            for build in data.get("builds", [])
            if build.get("name") == "gosha-v1"
        ),
        None,
    )
    require(gosha_build is not None, "gosha-v1 build was not found in config.json")
    require(
        NO_MOTION_FLAG in gosha_build.get("sdkconfig_append", []),
        "gosha-v1 release config must enable CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    require(
        re.search(
            r"config\s+GOSHA_NO_MOTION_SAFE_PROFILE(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
            kconfig,
            flags=re.DOTALL,
        )
        is not None,
        "GOSHA_NO_MOTION_SAFE_PROFILE Kconfig symbol was not found",
    )
    body = re.search(
        r"config\s+GOSHA_NO_MOTION_SAFE_PROFILE(?P<body>.*?)(?:\n\s*config\s+|\n\s*choice\s+|\nendmenu\b)",
        kconfig,
        flags=re.DOTALL,
    ).group("body")
    require("depends on BOARD_TYPE_GOSHA_V1" in body, "no-motion Kconfig must depend on gosha-v1")
    require("default n" in body, "no-motion Kconfig must default to off outside explicit release config")


def validate_motion_init(movements_h: str, movements_cc: str, controller: str) -> None:
    require(
        "int right_hand = -1, bool attach_servos = true" in movements_h,
        "Otto::Init declaration must expose an explicit attach_servos flag",
    )
    init_body = extract_body(
        movements_cc,
        r"void\s+Otto::Init\s*\([^)]*attach_servos[^)]*\)",
        "Otto::Init",
    )
    require(
        "if (attach_servos)" in init_body and "AttachServos();" in init_body,
        "Otto::Init must attach servos only when attach_servos is true",
    )
    require(
        "else" in init_body and "is_otto_resting_ = true;" in init_body,
        "Otto::Init must keep the robot resting when servos are not attached",
    )

    constructor_body = extract_body(
        controller,
        r"OttoController\s*\(\s*const\s+HardwareConfig&\s+hw_config\s*\)",
        "OttoController constructor",
    )
    require(
        "!kNoMotionSafeProfile" in constructor_body,
        "OttoController must pass !kNoMotionSafeProfile to Otto::Init",
    )
    intervals = protected_intervals(constructor_body)
    for match in re.finditer(r"QueueAction\s*\(\s*ACTION_HOME\b", constructor_body):
        require(
            inside_any(match.start(), intervals),
            "boot ACTION_HOME must be guarded by if (!kNoMotionSafeProfile)",
        )


def validate_motion_entrypoints(controller: str) -> None:
    require(
        "#ifdef CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in controller
        and "constexpr bool kNoMotionSafeProfile = true;" in controller,
        "controller must derive kNoMotionSafeProfile from CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    queue_body = extract_body(
        controller,
        r"void\s+QueueAction\s*\([^)]*\)",
        "QueueAction",
    )
    require(
        "if (kNoMotionSafeProfile)" in queue_body,
        "QueueAction must fail closed when no-motion profile is active",
    )
    require(
        queue_body.find("if (kNoMotionSafeProfile)") < queue_body.find("xQueueSend"),
        "QueueAction no-motion guard must run before xQueueSend",
    )

    sequence_body = extract_body(
        controller,
        r"void\s+QueueServoSequence\s*\([^)]*\)",
        "QueueServoSequence",
    )
    require(
        "if (kNoMotionSafeProfile)" in sequence_body,
        "QueueServoSequence must fail closed when no-motion profile is active",
    )
    require(
        sequence_body.find("if (kNoMotionSafeProfile)") < sequence_body.find("xQueueSend"),
        "QueueServoSequence no-motion guard must run before xQueueSend",
    )

    intervals = protected_intervals(controller)
    for token in DANGEROUS_TOOLS:
        pos = controller.find(token)
        require(pos >= 0, f"{token} was not found")
        require(
            inside_any(pos, intervals),
            f"{token} must be registered only inside if (!kNoMotionSafeProfile)",
        )
    for token in SAFE_TOOLS:
        pos = controller.find(token)
        require(pos >= 0, f"{token} was not found")
        require(
            not inside_any(pos, intervals),
            f"{token} must remain registered in the no-motion profile",
        )


def validate_firmware_upgrade_entrypoint(mcp_server: str) -> None:
    require(
        "#ifdef CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in mcp_server
        and "constexpr bool kGoshaNoMotionSafeProfile = true;" in mcp_server
        and "constexpr bool kGoshaNoMotionSafeProfile = false;" in mcp_server,
        "MCP server must derive kGoshaNoMotionSafeProfile from CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    user_tools_body = extract_body(
        mcp_server,
        r"void\s+McpServer::AddUserOnlyTools\s*\([^)]*\)",
        "McpServer::AddUserOnlyTools",
    )
    intervals = protected_intervals(user_tools_body, "kGoshaNoMotionSafeProfile")
    for tool in (REBOOT_TOOL, UPGRADE_TOOL, ASSETS_DOWNLOAD_URL_TOOL):
        pos = user_tools_body.find(tool)
        require(pos >= 0, f"{tool} was not found")
        require(
            inside_any(pos, intervals),
            f"{tool} must be registered only inside if (!kGoshaNoMotionSafeProfile)",
        )

    call_body = extract_body(
        mcp_server,
        r"void\s+McpServer::DoToolCall\s*\([^)]*\)",
        "McpServer::DoToolCall",
    )
    find_tool_pos = call_body.find("std::find_if")
    require(find_tool_pos >= 0, "McpServer::DoToolCall tool lookup was not found")

    for tool_name, tool in (
        ("self.reboot", REBOOT_TOOL),
        ("self.upgrade_firmware", UPGRADE_TOOL),
        ("self.assets.set_download_url", ASSETS_DOWNLOAD_URL_TOOL),
    ):
        reject_pos = call_body.find(f'kGoshaNoMotionSafeProfile && tool_name == "{tool_name}"')
        require(reject_pos >= 0, f"{tool} must be rejected when no-motion profile is active")
        require(
            reject_pos < find_tool_pos,
            f"{tool} no-motion rejection must run before tool lookup",
        )
        reject_tail = call_body[reject_pos : reject_pos + 400]
        require("ReplyError" in reject_tail and "return;" in reject_tail, f"{tool} rejection must return an error")


def validate_assets_update_entrypoints(application: str, assets_cc: str, source_files: dict[str, str]) -> None:
    check_body = extract_body(
        application,
        r"void\s+Application::CheckAssetsVersion\s*\([^)]*\)",
        "Application::CheckAssetsVersion",
    )
    no_motion_pos = check_body.find("if (kGoshaNoMotionSafeProfile)")
    settings_pos = check_body.find('Settings settings("assets", true)')
    require(no_motion_pos >= 0, "Application::CheckAssetsVersion must reject live assets replacement in no-motion profile")
    require(settings_pos >= 0, "Application::CheckAssetsVersion assets settings block was not found")
    require(
        no_motion_pos < settings_pos,
        "Application::CheckAssetsVersion no-motion rejection must run before assets/download_url persistence state is opened",
    )

    no_motion_body = extract_body(
        check_body,
        r"if\s*\(\s*kGoshaNoMotionSafeProfile\s*\)",
        "Application::CheckAssetsVersion no-motion guard",
    )
    require("assets.Apply();" in no_motion_body, "Application::CheckAssetsVersion must still apply local assets in no-motion profile")
    require("return;" in no_motion_body, "Application::CheckAssetsVersion no-motion guard must return before live assets replacement")
    for token in (
        'Settings settings("assets", true)',
        'settings.GetString("download_url")',
        'settings.EraseKey("download_url")',
        "Alert(Lang::Strings::LOADING_ASSETS",
        "vTaskDelay(pdMS_TO_TICKS(3000))",
        "SetDeviceState(kDeviceStateUpgrading)",
        "PowerSaveLevel::PERFORMANCE",
        "assets.Download(",
        "Schedule([display",
    ):
        require(token not in no_motion_body, f"Application::CheckAssetsVersion no-motion guard must not run live assets side effect: {token}")

    for token in (
        'settings.GetString("download_url")',
        'settings.EraseKey("download_url")',
        "Alert(Lang::Strings::LOADING_ASSETS",
        "SetDeviceState(kDeviceStateUpgrading)",
        "PowerSaveLevel::PERFORMANCE",
        "assets.Download(",
    ):
        pos = check_body.find(token)
        require(pos >= 0, f"Application::CheckAssetsVersion assets update token was not found: {token}")
        require(no_motion_pos < pos, f"Application::CheckAssetsVersion no-motion guard must run before {token}")

    require(
        "#ifdef CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in assets_cc
        and "constexpr bool kGoshaNoMotionSafeProfile = true;" in assets_cc
        and "constexpr bool kGoshaNoMotionSafeProfile = false;" in assets_cc,
        "Assets must derive kGoshaNoMotionSafeProfile from CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )
    download_body = extract_body(
        assets_cc,
        r"bool\s+Assets::Download\s*\(",
        "Assets::Download",
    )
    reject_pos = download_body.find("if (kGoshaNoMotionSafeProfile)")
    require(reject_pos >= 0, "Assets::Download must reject no-motion assets download requests")
    reject_tail = download_body[reject_pos : reject_pos + 300]
    require("return false;" in reject_tail, "Assets::Download no-motion rejection must return false")
    for token in (
        "diagnostic_redaction::RedactUrlForDiagnostics",
        "UnApplyPartition()",
        "CreateHttp",
        "esp_partition_erase_range",
        "esp_partition_write",
        "InitializePartition()",
    ):
        pos = download_body.find(token)
        require(pos >= 0, f"Assets::Download token was not found: {token}")
        require(reject_pos < pos, f"Assets::Download no-motion rejection must run before {token}")

    call_lines: list[tuple[str, str, int]] = []
    for rel_path, source in sorted(source_files.items()):
        for match in re.finditer(r"(?:Assets::Download|\.Download)\s*\(", source):
            line_no = source.count("\n", 0, match.start()) + 1
            line_start = source.rfind("\n", 0, match.start()) + 1
            line_end = source.find("\n", match.start())
            if line_end == -1:
                line_end = len(source)
            call_lines.append((rel_path, source[line_start:line_end].strip(), line_no))

    unexpected = [
        (rel_path, line, line_no)
        for rel_path, line, line_no in call_lines
        if (rel_path, line) not in ALLOWED_ASSETS_DOWNLOAD_CALL_LINES
    ]
    require(
        not unexpected,
        "unexpected Assets::Download caller: "
        + ", ".join(f"{rel_path}:{line_no}: {line}" for rel_path, line, line_no in unexpected),
    )

    partition_write_lines: list[tuple[str, str, int]] = []
    for rel_path, source in sorted(source_files.items()):
        for match in re.finditer(r"\besp_partition_write\s*\(", source):
            line_no = source.count("\n", 0, match.start()) + 1
            line_start = source.rfind("\n", 0, match.start()) + 1
            line_end = source.find("\n", match.start())
            if line_end == -1:
                line_end = len(source)
            partition_write_lines.append((rel_path, source[line_start:line_end].strip(), line_no))

    unexpected_partition_writes = [
        (rel_path, line, line_no)
        for rel_path, line, line_no in partition_write_lines
        if (rel_path, line) not in ALLOWED_PARTITION_WRITE_CALL_LINES
    ]
    require(
        not unexpected_partition_writes,
        "unexpected esp_partition_write caller: "
        + ", ".join(f"{rel_path}:{line_no}: {line}" for rel_path, line, line_no in unexpected_partition_writes),
    )


def validate_application_upgrade_entrypoint(application: str, source_files: dict[str, str]) -> None:
    require(
        "#ifdef CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in application
        and "constexpr bool kGoshaNoMotionSafeProfile = true;" in application
        and "constexpr bool kGoshaNoMotionSafeProfile = false;" in application,
        "Application must derive kGoshaNoMotionSafeProfile from CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE",
    )

    check_body = extract_body(
        application,
        r"void\s+Application::CheckNewVersion\s*\([^)]*\)",
        "Application::CheckNewVersion",
    )
    has_new_pos = check_body.find("if (ota_->HasNewVersion())")
    auto_call_pos = check_body.find("UpgradeFirmware(", has_new_pos)
    auto_guard_pos = check_body.find("if (kGoshaNoMotionSafeProfile)", has_new_pos)
    require(has_new_pos >= 0, "Application::CheckNewVersion new-version branch was not found")
    require(auto_call_pos >= 0, "Application::CheckNewVersion must keep the automatic upgrade call visible")
    require(
        auto_guard_pos >= 0 and auto_guard_pos < auto_call_pos,
        "automatic firmware upgrade must be rejected before calling Application::UpgradeFirmware",
    )

    upgrade_body = extract_body(
        application,
        r"bool\s+Application::UpgradeFirmware\s*\([^)]*\)",
        "Application::UpgradeFirmware",
    )
    reject_pos = upgrade_body.find("if (kGoshaNoMotionSafeProfile)")
    board_pos = upgrade_body.find("Board::GetInstance")
    require(reject_pos >= 0, "Application::UpgradeFirmware must reject no-motion runtime upgrade requests")
    require(board_pos >= 0, "Application::UpgradeFirmware board setup was not found")
    require(
        reject_pos < board_pos,
        "Application::UpgradeFirmware no-motion rejection must run before board/display/protocol/audio/OTA side effects",
    )
    reject_tail = upgrade_body[reject_pos : reject_pos + 300]
    require("return false;" in reject_tail, "Application::UpgradeFirmware no-motion rejection must return false")

    call_lines: list[tuple[str, str, int]] = []
    for rel_path, source in sorted(source_files.items()):
        for match in re.finditer(r"\bUpgradeFirmware\s*\(", source):
            line_no = source.count("\n", 0, match.start()) + 1
            line_start = source.rfind("\n", 0, match.start()) + 1
            line_end = source.find("\n", match.start())
            if line_end == -1:
                line_end = len(source)
            call_lines.append((rel_path, source[line_start:line_end].strip(), line_no))

    unexpected = [
        (rel_path, line, line_no)
        for rel_path, line, line_no in call_lines
        if (rel_path, line) not in ALLOWED_UPGRADE_CALL_LINES
    ]
    require(
        not unexpected,
        "unexpected Application::UpgradeFirmware caller: "
        + ", ".join(f"{rel_path}:{line_no}: {line}" for rel_path, line, line_no in unexpected),
    )

    reboot_command_pos = application.find('strcmp(command->valuestring, "reboot") == 0')
    reboot_schedule_pos = application.find("Schedule([this]()", reboot_command_pos)
    reboot_guard_pos = application.find("if (kGoshaNoMotionSafeProfile)", reboot_command_pos)
    require(reboot_command_pos >= 0, "Application system reboot command branch was not found")
    require(reboot_schedule_pos >= 0, "Application system reboot Schedule() call was not found")
    require(
        reboot_guard_pos >= 0 and reboot_guard_pos < reboot_schedule_pos,
        "Application system reboot command must be rejected before Schedule() in no-motion profile",
    )


def validate_release_hook(release_py: str) -> None:
    require(
        '"scripts/check_gosha_v1_no_motion_profile.py", "--self-test"' in release_py,
        "release.py must run the gosha-v1 no-motion static guard",
    )


def validate_tree(
    config_json: str,
    kconfig: str,
    controller: str,
    movements_cc: str,
    movements_h: str,
    mcp_server: str,
    application: str,
    release_py: str,
    source_files: dict[str, str],
) -> None:
    validate_config(config_json, kconfig)
    validate_motion_init(movements_h, movements_cc, controller)
    validate_motion_entrypoints(controller)
    validate_firmware_upgrade_entrypoint(mcp_server)
    validate_assets_update_entrypoints(application, read(ASSETS), source_files)
    validate_application_upgrade_entrypoint(application, source_files)
    validate_release_hook(release_py)


def validate_current_tree() -> None:
    validate_tree(
        read(CONFIG_JSON),
        read(KCONFIG),
        read(OTTO_CONTROLLER),
        read(OTTO_MOVEMENTS_CC),
        read(OTTO_MOVEMENTS_H),
        read(MCP_SERVER),
        read(APPLICATION),
        read(RELEASE),
        collect_main_sources(),
    )


def run_self_test() -> None:
    config_json = read(CONFIG_JSON)
    kconfig = read(KCONFIG)
    controller = read(OTTO_CONTROLLER)
    movements_cc = read(OTTO_MOVEMENTS_CC)
    movements_h = read(OTTO_MOVEMENTS_H)
    mcp_server = read(MCP_SERVER)
    application = read(APPLICATION)
    assets_cc = read(ASSETS)
    release_py = read(RELEASE)
    source_files = collect_main_sources()

    validate_tree(config_json, kconfig, controller, movements_cc, movements_h, mcp_server, application, release_py, source_files)

    try:
        validate_tree(
            config_json.replace(f'"{NO_MOTION_FLAG}",', ""),
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE" in str(exc), "flag negative test did not name the missing flag")
    else:
        raise GuardError("flag negative test failed: disabled release profile was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller.replace("if (!kNoMotionSafeProfile) {", "if (kNoMotionSafeProfile) {", 1),
            movements_cc,
            movements_h,
            mcp_server,
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("ACTION_HOME" in str(exc), "boot Home negative test did not name ACTION_HOME")
    else:
        raise GuardError("boot Home negative test failed: unguarded boot Home was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc.replace("if (attach_servos) {", "if (true) {"),
            movements_h,
            mcp_server,
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("attach_servos" in str(exc), "servo attach negative test did not name attach_servos")
    else:
        raise GuardError("servo attach negative test failed: unconditional attach was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace("if (!kGoshaNoMotionSafeProfile) {", "if (kGoshaNoMotionSafeProfile) {", 1),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.reboot" in str(exc), "reboot registration negative test did not name self.reboot")
    else:
        raise GuardError("reboot registration negative test failed: unguarded MCP reboot was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace(
                '    // Firmware upgrade\n    if (!kGoshaNoMotionSafeProfile) {',
                '    // Firmware upgrade\n    if (kGoshaNoMotionSafeProfile) {',
                1,
            ),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.upgrade_firmware" in str(exc), "upgrade registration negative test did not name self.upgrade_firmware")
    else:
        raise GuardError("upgrade registration negative test failed: unguarded MCP upgrade was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace(
                'kGoshaNoMotionSafeProfile && tool_name == "self.upgrade_firmware"',
                'false && tool_name == "self.upgrade_firmware"',
                1,
            ),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.upgrade_firmware" in str(exc), "upgrade rejection negative test did not name self.upgrade_firmware")
    else:
        raise GuardError("upgrade rejection negative test failed: missing MCP upgrade rejection was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace(
                'kGoshaNoMotionSafeProfile && tool_name == "self.reboot"',
                'false && tool_name == "self.reboot"',
                1,
            ),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.reboot" in str(exc), "reboot rejection negative test did not name self.reboot")
    else:
        raise GuardError("reboot rejection negative test failed: missing MCP reboot rejection was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace(
                'kGoshaNoMotionSafeProfile && tool_name == "self.assets.set_download_url"',
                'false && tool_name == "self.assets.set_download_url"',
                1,
            ),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.assets.set_download_url" in str(exc), "assets URL rejection negative test did not name self.assets.set_download_url")
    else:
        raise GuardError("assets URL rejection negative test failed: missing MCP assets URL rejection was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server.replace(
                '    // Assets download url\n    auto& assets = Assets::GetInstance();\n    if (assets.partition_valid()) {\n        if (!kGoshaNoMotionSafeProfile) {',
                '    // Assets download url\n    auto& assets = Assets::GetInstance();\n    if (assets.partition_valid()) {\n        if (kGoshaNoMotionSafeProfile) {',
                1,
            ),
            application,
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("self.assets.set_download_url" in str(exc), "assets URL registration negative test did not name self.assets.set_download_url")
    else:
        raise GuardError("assets URL registration negative test failed: unguarded MCP assets URL tool was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application.replace("if (kGoshaNoMotionSafeProfile) {\n        ESP_LOGW(TAG, \"Live assets replacement disabled by no-motion safe profile\");", "if (false) {\n        ESP_LOGW(TAG, \"Live assets replacement disabled by no-motion safe profile\");", 1),
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("CheckAssetsVersion" in str(exc), "assets CheckAssetsVersion negative test did not name CheckAssetsVersion")
    else:
        raise GuardError("assets CheckAssetsVersion negative test failed: missing no-motion guard was accepted")

    try:
        validate_assets_update_entrypoints(
            application,
            assets_cc.replace("if (kGoshaNoMotionSafeProfile) {", "if (false) {", 1),
            source_files,
        )
    except GuardError as exc:
        require("Assets::Download" in str(exc), "Assets::Download negative test did not name Assets::Download")
    else:
        raise GuardError("Assets::Download negative test failed: missing no-motion guard was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application.replace(
                'if (kGoshaNoMotionSafeProfile) {\n        ESP_LOGW(TAG, "Firmware upgrade blocked by no-motion safe profile");\n        return false;\n    }\n\n    auto& board',
                "auto& board",
                1,
            ),
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("Application::UpgradeFirmware" in str(exc), "upgrade entrypoint negative test did not name Application::UpgradeFirmware")
    else:
        raise GuardError("upgrade entrypoint negative test failed: missing runtime upgrade rejection was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application.replace(
                'if (kGoshaNoMotionSafeProfile) {\n                ESP_LOGW(TAG, "Firmware upgrade skipped by no-motion safe profile");\n            } else if (UpgradeFirmware',
                'if (false) {\n                ESP_LOGW(TAG, "Firmware upgrade skipped by no-motion safe profile");\n            } else if (UpgradeFirmware',
                1,
            ),
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("automatic firmware upgrade" in str(exc), "automatic upgrade negative test did not name automatic firmware upgrade")
    else:
        raise GuardError("automatic upgrade negative test failed: unguarded CheckNewVersion caller was accepted")

    future_sources = dict(source_files)
    future_sources["main/future_unchecked_ota_caller.cc"] = (
        'void FutureUncheckedOtaCaller() { Application::GetInstance().UpgradeFirmware("https://example.invalid/gosha.bin"); }\n'
    )
    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application,
            release_py,
            future_sources,
        )
    except GuardError as exc:
        require("UpgradeFirmware" in str(exc), "future caller negative test did not name UpgradeFirmware")
    else:
        raise GuardError("future caller negative test failed: unexpected UpgradeFirmware caller was accepted")

    future_assets_sources = dict(source_files)
    future_assets_sources["main/future_unchecked_assets_caller.cc"] = (
        'void FutureUncheckedAssetsCaller() { Assets::GetInstance().Download("https://example.invalid/assets.bin", nullptr); }\n'
    )
    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application,
            release_py,
            future_assets_sources,
        )
    except GuardError as exc:
        require("Assets::Download" in str(exc), "future assets caller negative test did not name Assets::Download")
    else:
        raise GuardError("future assets caller negative test failed: unexpected Assets::Download caller was accepted")

    future_partition_write_sources = dict(source_files)
    future_partition_write_sources["main/future_unchecked_assets_writer.cc"] = (
        "void FutureUncheckedAssetsWriter(const esp_partition_t* partition, const void* data, size_t size) { "
        "esp_partition_write(partition, 0, data, size); }\n"
    )
    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application,
            release_py,
            future_partition_write_sources,
        )
    except GuardError as exc:
        require("esp_partition_write" in str(exc), "future partition write negative test did not name esp_partition_write")
    else:
        raise GuardError("future partition write negative test failed: unexpected esp_partition_write caller was accepted")

    try:
        validate_tree(
            config_json,
            kconfig,
            controller,
            movements_cc,
            movements_h,
            mcp_server,
            application.replace(
                'if (kGoshaNoMotionSafeProfile) {\n                        ESP_LOGW(TAG, "System reboot command blocked by no-motion safe profile");\n                        return;\n                    }\n',
                'if (false) {\n                        ESP_LOGW(TAG, "System reboot command blocked by no-motion safe profile");\n                        return;\n                    }\n',
                1,
            ),
            release_py,
            source_files,
        )
    except GuardError as exc:
        require("system reboot" in str(exc), "system reboot negative test did not name system reboot")
    else:
        raise GuardError("system reboot negative test failed: unguarded system reboot was accepted")

    print("gosha-v1 no-motion profile guard self-test passed")


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
            print("gosha-v1 no-motion profile guard passed")
    except GuardError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()

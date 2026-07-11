#!/usr/bin/env python3
"""Static host check for gosha-v1 local MCP reply routing."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def require(name: str, condition: bool) -> bool:
    if condition:
        print(f"OK: {name}")
        return True
    print(f"FAIL: {name}", file=sys.stderr)
    return False


def main() -> int:
    mcp_h = read_text(ROOT / "main/mcp_server.h")
    mcp_cc = read_text(ROOT / "main/mcp_server.cc")
    ws_h = read_text(ROOT / "main/boards/gosha-v1/websocket_control_server.h")
    ws_cc = read_text(ROOT / "main/boards/gosha-v1/websocket_control_server.cc")

    ok = True
    ok &= require(
        "McpReplySender alias exists",
        "using McpReplySender = std::function<void(const std::string& payload)>;" in mcp_h,
    )
    ok &= require(
        "McpServer has cJSON ParseMessage overload with reply sender",
        "void ParseMessage(const cJSON* json, McpReplySender reply_sender);" in mcp_h,
    )
    ok &= require(
        "McpServer has string ParseMessage overload with reply sender",
        "void ParseMessage(const std::string& message, McpReplySender reply_sender);" in mcp_h,
    )
    ok &= require(
        "default cloud MCP sender remains Application::SendMcpMessage",
        "Application::GetInstance().SendMcpMessage(payload);" in mcp_cc
        and "ParseMessage(json, DefaultMcpReplySender())" in mcp_cc,
    )
    ok &= require(
        "reply sender is captured by value for delayed tool calls",
        "reply_sender = std::move(reply_sender)" in mcp_cc,
    )
    ok &= require(
        "ReplyResult uses injected sender",
        re.search(
            r"void McpServer::ReplyResult\(.*?McpReplySender reply_sender\).*?reply_sender\(payload\);",
            mcp_cc,
            re.DOTALL,
        )
        is not None,
    )
    ok &= require(
        "ReplyError uses injected sender",
        re.search(
            r"void McpServer::ReplyError\(.*?McpReplySender reply_sender\).*?reply_sender\(payload\);",
            mcp_cc,
            re.DOTALL,
        )
        is not None,
    )
    ok &= require(
        "gosha-v1 stores socket fds instead of request pointers",
        "std::set<int> client_fds_;" in ws_h
        and "std::map<int, httpd_req_t" not in ws_h
        and "clients_" not in ws_h,
    )
    ok &= require(
        "gosha-v1 queues WebSocket replies on httpd",
        "httpd_queue_work(server_handle_, SendAsyncWork, context)" in ws_cc,
    )
    ok &= require(
        "gosha-v1 sends async WebSocket frame to the original fd",
        "httpd_ws_send_frame_async(context->server_handle, context->sock_fd, &frame)" in ws_cc,
    )
    ok &= require(
        "gosha-v1 validates active WebSocket fd before sending",
        "httpd_ws_get_fd_info(context->server_handle, context->sock_fd)" in ws_cc
        and "HTTPD_WS_CLIENT_WEBSOCKET" in ws_cc,
    )
    ok &= require(
        "gosha-v1 passes local reply sender into McpServer",
        "ParseMessage(payload, reply_sender)" in ws_cc
        and "ParseMessage(root, reply_sender)" in ws_cc,
    )
    ok &= require(
        "gosha-v1 has no broadcast loop over local clients",
        re.search(r"for\s*\([^)]*(client_fds_|clients_)", ws_cc) is None,
    )

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

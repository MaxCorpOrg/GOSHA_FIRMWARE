#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def block_after(text, marker):
    start = text.find(marker)
    require(start >= 0, f"missing block marker: {marker}")
    brace = text.find("{", start)
    require(brace >= 0, f"missing block opening brace: {marker}")
    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
    raise SystemExit(f"FAIL: unterminated block: {marker}")


def span_between(text, start_marker, end_marker):
    start = text.find(start_marker)
    require(start >= 0, f"missing span start: {start_marker}")
    end = text.find(end_marker, start)
    require(end > start, f"missing span end: {end_marker}")
    return text[start:end]


websocket = read("main/protocols/websocket_protocol.cc")
websocket_h = read("main/protocols/websocket_protocol.h")
protocol_h = read("main/protocols/protocol.h")
protocol_cc = read("main/protocols/protocol.cc")
binary_parser = read("main/protocols/binary_protocol_parser.h")
application = read("main/application.cc")
application_h = read("main/application.h")
audio_service = read("main/audio/audio_service.cc")
kconfig = read("main/Kconfig.projbuild")
mqtt = read("main/protocols/mqtt_protocol.cc")
motion_adapter = read("main/boards/gosha-v1/motion_live_adapter.cc")
motion_adapter_h = read("main/boards/gosha-v1/motion_live_adapter.h")

require("config GOSHA_VOICE_SERVER_AEC_NEGOTIATION" in kconfig,
        "missing GOSHA voice server AEC Kconfig")
require("select USE_SERVER_AEC" in kconfig,
        "negotiated server AEC must select timestamp queue support")
require("config GOSHA_VOICE_MOTION_LIVE" in kconfig,
        "missing GOSHA voice Motion Live Kconfig")

require("version_ = 2" in websocket,
        "voice duplex candidate must force BinaryProtocol2 for timestamps")
require('"live_duplex", true' in websocket,
        "client hello must advertise live_duplex")
require('"aec", "server"' in websocket,
        "client hello must advertise server AEC string capability")
require("cJSON_IsTrue(live_duplex)" in websocket and
        'strcmp(aec->valuestring, "server") == 0' in websocket,
        "server hello must explicitly acknowledge live_duplex and aec=server")
require("std::atomic<bool> server_aec_negotiated_" in protocol_h and
        "server_aec_negotiated_.store(false, std::memory_order_release)" in websocket and
        "server_aec_negotiated_.store(server_aec_ack, std::memory_order_release)" in websocket,
        "server AEC negotiation must be atomic and reset per channel")
require("CONFIG_USE_SERVER_AEC && !defined(CONFIG_GOSHA_VOICE_SERVER_AEC_NEGOTIATION)" in mqtt,
        "MQTT must not inherit GOSHA voice WebSocket AEC negotiation")

require("std::atomic<uint32_t> connection_generation_" in websocket_h,
        "WebSocket must keep a connection generation for stale callback rejection")
close_audio = block_after(websocket, "void WebsocketProtocol::CloseAudioChannel")
require("connection_generation_.fetch_add(1, std::memory_order_acq_rel)" in close_audio,
        "CloseAudioChannel must invalidate the current WebSocket generation")
open_audio = block_after(websocket, "bool WebsocketProtocol::OpenAudioChannel")
require("const uint32_t connection_generation" in open_audio and
        "connection_generation_.fetch_add(1, std::memory_order_acq_rel) + 1" in open_audio,
        "OpenAudioChannel must snapshot a fresh WebSocket generation")
require("websocket_->OnData([this, connection_generation]" in open_audio and
        "connection_generation_.load(std::memory_order_acquire) != connection_generation" in open_audio,
        "WebSocket OnData must reject stale generations before parsing JSON or audio")
require("websocket_->OnDisconnected([this, connection_generation]" in open_audio and
        "compare_exchange_strong" in open_audio,
        "WebSocket OnDisconnected must fence stale close callbacks")

require("DecodeAudioPacket" in websocket and
        "BinaryProtocol2*" not in open_audio and
        "BinaryProtocol3*" not in open_audio,
        "WebSocket OnData must use bounded binary parsing without mutating callback data")
for required in [
    "len < kHeaderSize",
    "payload_size > len - kHeaderSize",
    "out->timestamp = timestamp",
    "out->payload.assign(payload, payload + payload_size)",
]:
    require(required in binary_parser, f"binary parser missing bounded/timestamp behavior: {required}")
require("cJSON_ParseWithLengthOpts(data, len" in open_audio and
        "Missing message type, data" not in websocket and
        "Failed to send text: %s" not in websocket and
        "Websocket JSON frame missing string type" in websocket,
        "WebSocket JSON parsing/logging must be length-bounded and avoid raw payload logs")

require("server_aec_negotiated()" in protocol_h,
        "Protocol must expose negotiated AEC state")
require("SendMotionLiveMessage" in protocol_h and
        "SendMotionLiveMessage" in protocol_cc and
        "motion_live" in protocol_cc,
        "Protocol must provide motion_live response envelope")
continue_open = block_after(application, "void Application::ContinueOpenAudioChannel")
require("protocol_->server_aec_negotiated()" in continue_open and
        "effective_mode = kListeningModeRealtime" in continue_open,
        "negotiated server AEC must force realtime mode for the opened session")
require("mode == kListeningModeAutoStop" not in continue_open,
        "negotiated server AEC must also upgrade manual-start listening to realtime")
start_listening = block_after(application, "void Application::HandleStartListeningEvent")
require("protocol_->server_aec_negotiated() ? kListeningModeRealtime" in start_listening and
        "ContinueOpenAudioChannel(kListeningModeManualStop)" in start_listening,
        "manual start must remain legacy manual only without negotiated server AEC")

speaking_block = span_between(application,
                              "case kDeviceStateSpeaking:",
                              "case kDeviceStateWifiConfiguring:")
require("listening_mode_ != kListeningModeRealtime" in speaking_block and
        "audio_service_.EnableVoiceProcessing(false)" in speaking_block,
        "Speaking state must keep the microphone active in realtime mode")

audio_output = block_after(audio_service, "void AudioService::AudioOutputTask")
timestamp_push = audio_output.find("timestamp_queue_.push_back(task->timestamp)")
output_data = audio_output.find("codec_->OutputData(task->pcm)")
require(output_data >= 0 and timestamp_push > output_data,
        "server AEC timestamps must be recorded only after actual speaker playback")
encode_queue = block_after(audio_service, "void AudioService::PushTaskToEncodeQueue")
require("type == kAudioTaskTypeEncodeToSendQueue" in encode_queue and
        "task->timestamp = timestamp_queue_.front()" in encode_queue and
        "timestamp_queue_.pop_front()" in encode_queue,
        "uplink audio must consume the playback timestamp queue")

allowed_block = span_between(application,
                             "bool IsAllowedVoiceMotionLiveOp",
                             "void CopyRequestFieldIfMissing")
for op in ["hello", "initialize_right_arm", "arm", "pose", "stop"]:
    require(f'"{op}"' in allowed_block,
            f"voice Motion Live allowlist missing {op}")
for forbidden in ["keepalive", "package", "upload", "delete", "set_trim", "home", "reboot"]:
    require(f'"{forbidden}"' not in allowed_block,
            f"voice Motion Live allowlist includes forbidden {forbidden}")

require("std::atomic<uint32_t> protocol_generation_" in application_h and
        "std::mutex voice_motion_live_mutex_" in application_h and
        "std::atomic<int> voice_motion_live_owner_id_" in application_h and
        "std::atomic<uint32_t> voice_motion_live_generation_" in application_h,
        "Application must fence voice Motion Live with protocol generation and serialized owner state")

init_protocol = block_after(application, "void Application::InitializeProtocol")
require("const uint32_t source_protocol_generation" in init_protocol and
        "Protocol* source_protocol = protocol_.get()" in init_protocol,
        "InitializeProtocol must snapshot source protocol identity")
require("OnIncomingJson([this, display, source_protocol, source_protocol_generation]" in init_protocol,
        "Incoming JSON callback must capture source protocol identity")
require("protocol_generation_.load(std::memory_order_acquire) != source_protocol_generation" in init_protocol,
        "protocol callbacks must reject stale protocol generations")

motion_block = span_between(application,
                            '} else if (strcmp(type->valuestring, "motion_live") == 0)',
                            '} else if (strcmp(type->valuestring, "system") == 0)')
require("std::lock_guard<std::mutex> voice_lock(voice_motion_live_mutex_)" in motion_block and
        "voice_motion_live_owner_id_.load(std::memory_order_acquire)" in motion_block and
        "voice_motion_live_generation_.load(std::memory_order_acquire)" in motion_block,
        "motion_live command handling must serialize admission with owner and generation snapshot")
require("HandleTransportMessage(" in motion_block and
        "owner_id, payload_copy, sender" in motion_block,
        "voice Motion Live wrapper must route to the existing adapter with the session owner")
require('CopyRequestFieldIfMissing(reply, payload_copy, "request_id")' in motion_block,
        "motion_live wrapper must copy request_id into every adapter reply, including ack/stopped")
require("SendMotionLiveMessageForOwner(" in motion_block and
        "source_protocol_generation, source_protocol" in motion_block,
        "motion_live adapter replies must use the fenced scheduled sender")

send_helper = block_after(application, "void Application::SendMotionLiveMessageForOwner")
for required in [
    "protocol_generation_.load(std::memory_order_acquire) != expected_protocol_generation",
    "voice_motion_live_generation_.load(std::memory_order_acquire) != expected_voice_generation",
    "protocol_.get() != expected_protocol",
    "!protocol_->IsAudioChannelOpened()",
    "require_active_owner",
    "expected_owner_id == 0",
    "voice_motion_live_owner_id_.load(std::memory_order_acquire) != expected_owner_id",
    "protocol_->SendMotionLiveMessage(payload)",
]:
    require(required in send_helper, f"fenced sender missing guard: {required}")

error_helper = block_after(application, "void Application::SendMotionLiveErrorForOwner")
require("expected_protocol, false" in error_helper,
        "voice_channel_closed errors must be fenced by generation/protocol without requiring an active owner")

transport_handler = block_after(motion_adapter, "bool MotionLiveAdapter::HandleTransportMessage")
send_frame = block_after(motion_adapter, "esp_err_t MotionLiveAdapter::SendJsonFrame")
require("const MotionLiveJsonSender& sender" in motion_adapter_h and
        "esp_err_t ret = sender(root)" in send_frame,
        "Motion Live adapter replies must synchronously call the supplied sender")
require(re.search(r"\bMotionLiveJsonSender\s+[A-Za-z0-9_]+_\s*;", motion_adapter_h) is None,
        "Motion Live adapter must not store voice reply senders across calls")
require("SendAck(sender" in transport_handler and
        "SendStopped(sender" in transport_handler and
        "SendCapabilities(sender" in transport_handler,
        "Motion Live adapter must route ack/stopped/capabilities through the supplied sender")

begin_owner = block_after(application, "void Application::BeginVoiceMotionLiveOwner")
retire_owner = block_after(application, "void Application::RetireVoiceMotionLiveOwner")
retire_owner_locked = block_after(application, "void Application::RetireVoiceMotionLiveOwnerLocked")
require("std::lock_guard<std::mutex> lock(voice_motion_live_mutex_)" in begin_owner and
        "RetireVoiceMotionLiveOwnerLocked()" in begin_owner,
        "voice Motion Live owner begin must serialize with pending retire and adapter calls")
require("std::lock_guard<std::mutex> lock(voice_motion_live_mutex_)" in retire_owner and
        "RetireVoiceMotionLiveOwnerLocked()" in retire_owner and
        "voice_motion_live_owner_id_.exchange(0, std::memory_order_acq_rel)" in retire_owner_locked and
        "voice_motion_live_generation_.fetch_add(1, std::memory_order_acq_rel)" in retire_owner_locked and
        "OnTransportClosed" in retire_owner_locked,
        "voice Motion Live owner retire must serialize with admission and close adapter state")

runtime = read("main/boards/gosha-v1/robot_motion_runtime.cc")
controller = read("main/boards/gosha-v1/otto_controller.cc")
mcp_server = read("main/mcp_server.cc")
require("config GOSHA_RUNTIME_MOTIONS" in kconfig,
        "named robot movements must be an explicit build option")
runtime_tools = span_between(controller, "#if defined(CONFIG_GOSHA_RUNTIME_MOTIONS)", "#endif")
for name in ("list", "play", "status", "stop"):
    require(f'"self.motion.{name}"' in runtime_tools,
            f"missing native movement operation {name}")
require('tool_name.rfind("self.motion.", 0) == 0' in mcp_server and
        "app.ScheduleRobotMovement(std::move(callback))" in mcp_server,
        "native movement calls must use the connection-fenced dispatcher")
dispatch = block_after(application, "void Application::ScheduleRobotMovement")
for guard in ("std::lock_guard<std::mutex>", "owner == 0", "!= owner",
              "!= generation", "!= protocol_generation", "!protocol_->IsAudioChannelOpened()"):
    require(guard in dispatch, f"movement dispatch missing stale-session guard {guard}")
require(dispatch.find("callback();") > dispatch.find("!protocol_->IsAudioChannelOpened()"),
        "movement callbacks must execute only after connection checks")
for op in ("InitializeRightArm", "Arm", "Pose", "Keepalive", "Stop"):
    require(f"robot_motion_runtime_.running() ? RuntimeBusyResult() : core_.{op}(" in motion_adapter,
            f"editor {op} must reject while ordinary movement owns drives")
require("robot_motion_runtime_.Tick(&core_, now_ms)" in motion_adapter and
        "robot_motion_runtime_.OnTransportClosed(&core_, owner_id)" in motion_adapter,
        "movement playback and disconnect cleanup must run inside device ownership")
require("LoadById" in runtime and "manager_->Select" not in runtime,
        "ordinary playback must not change the editor's active package")
print("check_gosha_voice_duplex_motion_contract: PASS")

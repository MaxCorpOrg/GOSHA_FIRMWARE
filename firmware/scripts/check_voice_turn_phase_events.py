#!/usr/bin/env python3
"""Static guard for privacy-safe voice turn phase runtime events."""

from pathlib import Path
import argparse
import re


ROOT = Path(__file__).resolve().parents[1]


class GuardError(Exception):
    """Voice turn phase static guard failure."""


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GuardError(message)


def extract_function_body(source: str, name: str) -> str:
    match = re.search(rf"{re.escape(name)}\s*\([^)]*\)\s*(?:const\s*)?\{{", source)
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


def require_ordered(body: str, tokens: tuple[str, ...], message: str) -> None:
    pos = -1
    for token in tokens:
        next_pos = body.find(token, pos + 1)
        require(next_pos >= 0, f"{message}: missing {token}")
        pos = next_pos


def validate_reporter(reporter_h: str, reporter_cc: str) -> None:
    require(
        "void PublishVoiceTurnPhase(const char* phase, const char* warm_state," in reporter_h,
        "RuntimeEventReporter must expose PublishVoiceTurnPhase",
    )
    require(
        "std::string BuildVoiceTurnPhaseEvent(const char* phase, const char* warm_state," in reporter_h,
        "RuntimeEventReporter must keep voice event building local",
    )

    body = extract_function_body(reporter_cc, "RuntimeEventReporter::BuildVoiceTurnPhaseEvent")
    for token in (
        '"schema_version", "gosha.runtime.event.v1"',
        '"event_type", "voice.turn.phase"',
        '"severity", "info"',
        'cJSON* source = cJSON_AddObjectToObject(root, "source")',
        '"kind", "robot"',
        'cJSON* trace = cJSON_AddObjectToObject(root, "trace")',
        '"correlation_id"',
        'cJSON* task = cJSON_AddObjectToObject(root, "task")',
        '"id"',
        'cJSON* voice = cJSON_AddObjectToObject(root, "voice")',
        '"phase"',
        '"warm_state"',
    ):
        require(token in body, f"voice.turn.phase event is missing {token}")

    voice_start = body.find('cJSON* voice = cJSON_AddObjectToObject(root, "voice")')
    printed_pos = body.find("char* printed", voice_start)
    require(voice_start >= 0 and printed_pos >= 0, "voice payload block was not found")
    voice_block = body[voice_start:printed_pos]
    voice_keys = set(re.findall(r'(?:AddString|cJSON_Add\w+ToObject)\(\s*voice,\s*"([^"]+)"', voice_block))
    require(
        voice_keys == {"phase", "warm_state"},
        f"voice payload must contain only phase and warm_state, got {sorted(voice_keys)}",
    )

    for forbidden in (
        "SystemInfo::GetMacAddress",
        "Board::GetInstance().GetUuid",
        "protocol_->session_id",
        "payload.data",
    ):
        require(forbidden not in body, f"voice event payload contains sensitive source: {forbidden}")

    forbidden_keys = {
        "transcript",
        "prompt",
        "token",
        "ssid",
        "password",
        "mac",
        "ip",
        "url",
        "raw_audio",
        "device_id",
    }
    string_literals = {literal.lower() for literal in re.findall(r'"([^"]+)"', body)}
    leaked_keys = sorted(forbidden_keys & string_literals)
    require(not leaked_keys, f"voice event payload contains sensitive keys: {leaked_keys}")


def validate_application(application_h: str, application_cc: str) -> None:
    for field in (
        "bool active = false;",
        "bool user_speech_active = false;",
        "bool first_audio_out_reported = false;",
        "bool tts_stop_seen = false;",
        "std::string warm_state;",
        "std::string correlation_id;",
        "std::string task_id;",
    ):
        require(field in application_h, f"VoiceTurnState is missing {field}")

    start_body = extract_function_body(application_cc, "Application::StartVoiceTurnLocked")
    for token in (
        "esp_random()",
        "snprintf(correlation_id, sizeof(correlation_id)",
        "snprintf(task_id, sizeof(task_id)",
        '"fw-turn-%08lx-%04lx"',
        '"fw-task-%08lx-%04lx"',
        "voice_turn_ = VoiceTurnState{};",
    ):
        require(token in start_body, f"StartVoiceTurnLocked is missing bounded id token {token}")
    for forbidden in ("GetMacAddress", "GetUuid", "session_id", "wake_word", "token", "url", "ssid"):
        require(forbidden not in start_body, f"turn ids must not use sensitive source: {forbidden}")

    warm_body = extract_function_body(application_cc, "Application::GetCurrentVoiceWarmState")
    require(
        '"warm"' in warm_body and '"cold"' in warm_body and "IsAudioChannelOpened()" in warm_body,
        "warm_state must be derived from the proven audio channel state",
    )

    wake_body = extract_function_body(application_cc, "Application::ReportWakeDetected")
    require("StartVoiceTurnLocked(warm_state)" in wake_body, "wake_detected must start a fresh turn")
    require('"wake_detected"' in wake_body, "wake_detected phase is not published")
    require("wake_word" not in wake_body, "wake_detected event must not include wake word text")

    vad_body = extract_function_body(application_cc, "Application::ReportUserSpeechChange")
    for token in ('"user_speech_start"', '"user_speech_end"', "voice_turn_.user_speech_active"):
        require(token in vad_body, f"VAD phase handling is missing {token}")

    first_audio_body = extract_function_body(application_cc, "Application::ReportRobotFirstAudioOutput")
    require_ordered(
        first_audio_body,
        (
            "if (!voice_turn_.active)",
            "return;",
            "if (voice_turn_.first_audio_out_reported)",
            "return;",
            "voice_turn_.first_audio_out_reported = true;",
            'PublishVoiceTurnPhaseLocked("robot_first_audio_out")',
        ),
        "first audio must be reported exactly once per turn",
    )

    reset_body = extract_function_body(application_cc, "Application::ResetVoiceTurnLocked")
    require(
        "voice_turn_ = VoiceTurnState{};" in reset_body,
        "turn reset must clear ids and phase flags",
    )
    require(
        "ResetVoiceTurn();" in application_cc,
        "audio channel close must reset the voice turn state",
    )
    closed_pos = application_cc.find("protocol_->OnAudioChannelClosed")
    reset_pos = application_cc.find("ResetVoiceTurn();", closed_pos)
    wait_pos = application_cc.find("audio_service_.WaitForPlaybackQueueEmpty();", closed_pos, reset_pos)
    require(
        closed_pos >= 0 and reset_pos >= 0 and wait_pos >= 0,
        "audio channel close must wait for queued playback before resetting the voice turn",
    )

    fail_body = extract_function_body(application_cc, "Application::FailVoiceTurnIfActive")
    require_ordered(
        fail_body,
        ('PublishVoiceTurnPhaseLocked("turn_failed")', "ResetVoiceTurnLocked()"),
        "turn_failed must be truthful and must reset the turn",
    )
    require(application_cc.count("FailVoiceTurnIfActive();") >= 4, "voice turn failures must cover channel and protocol failure paths")

    for phase in ("wake_detected", "user_speech_start", "user_speech_end", "robot_first_audio_out", "turn_failed"):
        require(phase in application_cc, f"application is missing voice phase {phase}")


def validate_audio_service(audio_h: str, audio_cc: str, protocol_h: str) -> None:
    require("enum class AudioStreamSource" in protocol_h, "AudioStreamSource enum is missing")
    require("AudioStreamSource source = AudioStreamSource::kRemote;" in protocol_h, "remote protocol audio must be default packet source")
    require("std::function<void(void)> on_remote_audio_output;" in audio_h, "remote audio output callback is missing")
    require("AudioStreamSource source = AudioStreamSource::kRemote;" in audio_h, "AudioTask must carry audio source")
    require("uint32_t playback_generation = 0;" in audio_h, "AudioTask must carry playback generation")
    require("AudioPlaybackDrainTracker playback_drain_tracker_;" in audio_h, "playback drain tracker is missing")

    output_body = extract_function_body(audio_cc, "AudioService::AudioOutputTask")
    require_ordered(
        output_body,
        (
            "playback_drain_tracker_.BeginPlayback();",
            "codec_->OutputData(task->pcm);",
            "playback_drain_tracker_.IsCurrent(task->playback_generation)",
            "task->source == AudioStreamSource::kRemote",
            "callbacks_.on_remote_audio_output",
            "remote_audio_output_callback();",
            "playback_drain_tracker_.FinishPlayback();",
        ),
        "remote first-audio callback must run during in-flight output after actual codec OutputData",
    )
    require("task->source = packet->source;" in audio_cc, "decode tasks must preserve packet source")
    require("task->playback_generation = playback_generation;" in audio_cc, "decode tasks must preserve playback generation")
    require_ordered(
        extract_function_body(audio_cc, "AudioService::OpusCodecTask"),
        (
            "playback_drain_tracker_.BeginDecode();",
            "esp_opus_dec_decode",
            "playback_drain_tracker_.FinishDecode();",
            "playback_drain_tracker_.IsCurrent(playback_generation)",
            "audio_playback_queue_.push_back(std::move(task));",
        ),
        "decode in-flight work must be tracked before it can drain playback",
    )
    wait_body = extract_function_body(audio_cc, "AudioService::WaitForPlaybackQueueEmpty")
    require(
        "playback_drain_tracker_.IsPlaybackDrained" in wait_body,
        "WaitForPlaybackQueueEmpty must include decode/output in-flight work",
    )
    idle_body = extract_function_body(audio_cc, "AudioService::IsIdle")
    require("playback_drain_tracker_.IsIdle" in idle_body, "IsIdle must include decode/output in-flight work")
    reset_body = extract_function_body(audio_cc, "AudioService::ResetDecoder")
    require(
        "playback_drain_tracker_.CancelQueuedPlayback();" in reset_body,
        "ResetDecoder must cancel queued playback generation",
    )
    require(
        "playback_drain_tracker_.HasInFlight()" in reset_body,
        "ResetDecoder must wait for decode/output in-flight work",
    )
    stop_body = extract_function_body(audio_cc, "AudioService::Stop")
    require(
        "playback_drain_tracker_.CancelQueuedPlayback();" in stop_body,
        "Stop must cancel queued playback generation",
    )
    require(
        audio_cc.count("packet->source = AudioStreamSource::kLocal;") >= 2,
        "local sounds and audio testing packets must not trigger robot_first_audio_out",
    )


def validate_no_motion_invariants(config_json: str, release_py: str, application_cc: str) -> None:
    require(
        "CONFIG_GOSHA_NO_MOTION_SAFE_PROFILE=y" in config_json,
        "gosha-v1 no-motion release flag must remain enabled",
    )
    require(
        '["scripts/check_gosha_v1_no_motion_profile.py", "--self-test"]' in release_py,
        "release.py must keep the no-motion static guard",
    )
    require(
        '["scripts/check_voice_turn_phase_events.py", "--self-test"]' in release_py,
        "release.py must run the voice turn phase static guard",
    )
    for helper_name in (
        "ReportWakeDetected",
        "ReportUserSpeechChange",
        "ReportRobotFirstAudioOutput",
        "MarkVoiceTurnTtsStart",
        "MarkVoiceTurnTtsStop",
        "FailVoiceTurnIfActive",
    ):
        body = extract_function_body(application_cc, f"Application::{helper_name}")
        for forbidden in (
            "QueueAction",
            "QueueServoSequence",
            "AttachServos",
            "ACTION_HOME",
            "UpgradeFirmware",
            "Reboot",
            "CheckAssetsVersion",
            "Assets::Download",
            "set_trim",
            "servo_sequences",
        ):
            require(forbidden not in body, f"{helper_name} must not touch motion/update/assets path: {forbidden}")


def validate_current_tree(
    *,
    reporter_h: str | None = None,
    reporter_cc: str | None = None,
    application_h: str | None = None,
    application_cc: str | None = None,
    audio_h: str | None = None,
    audio_cc: str | None = None,
    protocol_h: str | None = None,
    config_json: str | None = None,
    release_py: str | None = None,
) -> None:
    reporter_h = reporter_h if reporter_h is not None else read("main/runtime_event_reporter.h")
    reporter_cc = reporter_cc if reporter_cc is not None else read("main/runtime_event_reporter.cc")
    application_h = application_h if application_h is not None else read("main/application.h")
    application_cc = application_cc if application_cc is not None else read("main/application.cc")
    audio_h = audio_h if audio_h is not None else read("main/audio/audio_service.h")
    audio_cc = audio_cc if audio_cc is not None else read("main/audio/audio_service.cc")
    protocol_h = protocol_h if protocol_h is not None else read("main/protocols/protocol.h")
    config_json = config_json if config_json is not None else read("main/boards/gosha-v1/config.json")
    release_py = release_py if release_py is not None else read("scripts/release.py")

    validate_reporter(reporter_h, reporter_cc)
    validate_application(application_h, application_cc)
    validate_audio_service(audio_h, audio_cc, protocol_h)
    validate_no_motion_invariants(config_json, release_py, application_cc)


def run_self_test() -> None:
    reporter_h = read("main/runtime_event_reporter.h")
    reporter_cc = read("main/runtime_event_reporter.cc")
    application_h = read("main/application.h")
    application_cc = read("main/application.cc")
    audio_h = read("main/audio/audio_service.h")
    audio_cc = read("main/audio/audio_service.cc")
    protocol_h = read("main/protocols/protocol.h")
    config_json = read("main/boards/gosha-v1/config.json")
    release_py = read("scripts/release.py")

    validate_current_tree(
        reporter_h=reporter_h,
        reporter_cc=reporter_cc,
        application_h=application_h,
        application_cc=application_cc,
        audio_h=audio_h,
        audio_cc=audio_cc,
        protocol_h=protocol_h,
        config_json=config_json,
        release_py=release_py,
    )

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc.replace("if (voice_turn_.first_audio_out_reported)", "if (false)", 1),
            audio_h=audio_h,
            audio_cc=audio_cc,
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("first audio" in str(exc), "first-audio negative test did not name first audio")
    else:
        raise GuardError("first-audio negative test failed: duplicate first audio was accepted")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc,
            audio_h=audio_h,
            audio_cc=audio_cc.replace("task->source == AudioStreamSource::kRemote", "true", 1),
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("codec OutputData" in str(exc), "remote-source negative test did not name codec OutputData")
    else:
        raise GuardError("remote-source negative test failed: local audio could trigger first audio")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc,
            audio_h=audio_h,
            audio_cc=audio_cc.replace("playback_drain_tracker_.IsPlaybackDrained", "queues_only_drain", 1),
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("in-flight" in str(exc), "in-flight drain negative test did not name in-flight work")
    else:
        raise GuardError("in-flight drain negative test failed: queue-only drain was accepted")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc,
            audio_h=audio_h,
            audio_cc=audio_cc.replace("playback_drain_tracker_.BeginPlayback();", "", 1),
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("in-flight" in str(exc), "output in-flight negative test did not name in-flight work")
    else:
        raise GuardError("output in-flight negative test failed: untracked output was accepted")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc.replace(
                "void Application::ResetVoiceTurnLocked() {\n    voice_turn_ = VoiceTurnState{};\n}",
                "void Application::ResetVoiceTurnLocked() {\n}",
                1,
            ),
            audio_h=audio_h,
            audio_cc=audio_cc,
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("reset" in str(exc), "reset negative test did not name reset")
    else:
        raise GuardError("reset negative test failed: missing turn reset was accepted")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc.replace(
                'AddString(voice, "warm_state", warm_state);',
                'AddString(voice, "warm_state", warm_state);\n    cJSON_AddStringToObject(root, "device_id", SystemInfo::GetMacAddress().c_str());',
                1,
            ),
            application_h=application_h,
            application_cc=application_cc,
            audio_h=audio_h,
            audio_cc=audio_cc,
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py,
        )
    except GuardError as exc:
        require("sensitive" in str(exc), "sensitive-data negative test did not name sensitive data")
    else:
        raise GuardError("sensitive-data negative test failed: raw device id was accepted")

    try:
        validate_current_tree(
            reporter_h=reporter_h,
            reporter_cc=reporter_cc,
            application_h=application_h,
            application_cc=application_cc,
            audio_h=audio_h,
            audio_cc=audio_cc,
            protocol_h=protocol_h,
            config_json=config_json,
            release_py=release_py.replace(
                '        ["scripts/check_gosha_v1_no_motion_profile.py", "--self-test"],\n',
                "",
                1,
            ),
        )
    except GuardError as exc:
        require("no-motion" in str(exc), "no-motion negative test did not name no-motion")
    else:
        raise GuardError("no-motion negative test failed: missing no-motion guard was accepted")

    print("voice turn phase event guard self-test passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    try:
        if args.self_test:
            run_self_test()
        else:
            validate_current_tree()
            print("voice turn phase event guard passed")
    except GuardError as exc:
        raise SystemExit(str(exc)) from exc


if __name__ == "__main__":
    main()

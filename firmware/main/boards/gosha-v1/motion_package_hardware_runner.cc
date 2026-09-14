#include "motion_package_hardware_runner.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace gosha::motion_live {

namespace {

constexpr double kPoseEpsilonDegrees = 0.0001;

bool ValidRunSessionId(const std::string& value) {
    return value.size() >= 16 && value.size() <= 128;
}

bool ValidLiveSpeed(double speed_dps) {
    return std::isfinite(speed_dps) && speed_dps > 0.0 &&
           speed_dps <= kMotionEditorMaxServoRateDps;
}

uint32_t ElapsedMs(uint64_t start_ms, uint64_t now_ms) {
    if (now_ms <= start_ms) {
        return 0;
    }
    const uint64_t elapsed = now_ms - start_ms;
    return static_cast<uint32_t>(
        std::min<uint64_t>(elapsed, std::numeric_limits<uint32_t>::max()));
}

bool SafeStartSample(const MotionPackageSample& sample) {
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (sample.target.present[joint_index] &&
            std::fabs(sample.target.relative_degrees[joint_index]) >
                kPoseEpsilonDegrees) {
            return false;
        }
    }
    return true;
}

}  // namespace

MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::ResetResult() {
    result_ = MotionPackageHardwareRunnerResult{};
    result_.run_session_id = run_session_id_;
    result_.live_session_id = live_session_id_;
    result_.live_armed = live_started_;
    result_.hardware_apply = live_started_;
    return result_;
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::MakeError(
    const char* code) {
    MotionPackageHardwareRunnerResult& result = ResetResult();
    result.ok = false;
    result.code = code;
    return result;
}

bool MotionPackageHardwareRunner::SessionMatches(
    int owner_id,
    const std::string& run_session_id) const {
    return active_ && owner_id == owner_id_ && !run_session_id.empty() &&
           run_session_id == run_session_id_;
}

bool MotionPackageHardwareRunner::StartPoseMatches(
    const MotionLivePose& pose,
    const MotionPackageSample& sample) const {
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (!sample.target.present[joint_index]) {
            continue;
        }
        if (std::fabs(pose.relative_degrees[joint_index] -
                      sample.target.relative_degrees[joint_index]) >
            kPoseEpsilonDegrees) {
            return false;
        }
    }
    return true;
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::StopLiveSession(
    MotionLiveCore* core,
    bool clear_runner) {
    MotionPackageHardwareRunnerResult& result = ResetResult();
    result.hardware_apply = true;
    result.stopped = true;
    if (core == nullptr || !active_) {
        result.ok = false;
        result.code = "package_hardware_run_not_active";
    } else if (!live_started_) {
        result.ok = true;
        result.code = "ok";
        result.hardware_apply = false;
        result.live_result.ok = true;
        result.live_result.code = "ok";
        result.live_result.message = "ok";
        result.live_result.session_id = live_session_id_;
        result.live_result.seq = live_seq_;
        result.live_result.stopped = true;
    } else {
        uint32_t accepted_seq = live_seq_;
        const char* stop_code = core->StopForPackageHardwareRun(
            owner_id_, live_session_id_, ++live_seq_, &accepted_seq);
        result.live_result.ok = std::strcmp(stop_code, "ok") == 0;
        result.live_result.code = stop_code;
        result.live_result.message = stop_code;
        result.live_result.session_id = live_session_id_;
        result.live_result.seq = accepted_seq;
        result.live_result.stopped = true;
        result.ok = result.live_result.ok;
        result.code = result.live_result.ok ? "ok" : stop_code;
    }
    if (clear_runner) {
        ClearState();
    }
    return result;
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::Start(
    const MotionPackagePlayer& player,
    MotionLiveCore* core,
    int owner_id,
    const std::string& run_session_id,
    const std::string& live_session_id,
    const std::string& calibration_id,
    bool access_key_valid,
    double speed_dps,
    uint64_t now_ms) {
    if (active_ && !finished_) {
        return MakeError("package_hardware_run_busy");
    }
    if (finished_) {
        ClearState();
    }
    if (core == nullptr) {
        return MakeError("package_hardware_core_missing");
    }
    if (!player.loaded()) {
        return MakeError("package_not_loaded");
    }
    if (!ValidRunSessionId(run_session_id) || !ValidRunSessionId(live_session_id)) {
        return MakeError("package_hardware_run_session_id");
    }
    if (!ValidLiveSpeed(speed_dps)) {
        return MakeError("package_hardware_run_speed");
    }

    MotionPackageHardwareRunnerResult& result = ResetResult();
    result.run_session_id = run_session_id;
    result.live_session_id = live_session_id;
    const MotionPackagePlayerResult sample_result = player.Sample(0, &result.sample);
    if (!sample_result.ok) {
        result.ok = false;
        result.code = sample_result.code;
        return result;
    }
    if (!SafeStartSample(result.sample)) {
        result.ok = false;
        result.code = "package_hardware_start_pose";
        return result;
    }
    if (!StartPoseMatches(core->CommandedPose(), result.sample)) {
        result.ok = false;
        result.code = "package_hardware_start_pose";
        return result;
    }

    active_ = true;
    package_id_ = player.package_id();
    owner_id_ = owner_id;
    run_session_id_ = run_session_id;
    live_session_id_ = live_session_id;
    calibration_id_ = calibration_id;
    start_ms_ = now_ms;
    live_seq_ = 0;
    speed_dps_ = speed_dps;
    access_key_valid_ = access_key_valid;
    live_started_ = false;
    finished_ = false;

    result.ok = true;
    result.code = "ok";
    result.run_session_id = run_session_id_;
    result.live_session_id = live_session_id_;
    result.live_armed = false;
    result.hardware_apply = false;
    return result;
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::Tick(
    const MotionPackagePlayer& player,
    MotionLiveCore* core,
    int owner_id,
    const std::string& run_session_id,
    uint64_t now_ms) {
    if (!active_) {
        return MakeError("package_hardware_run_not_active");
    }
    if (!SessionMatches(owner_id, run_session_id)) {
        return MakeError("package_hardware_run_not_owner");
    }
    if (finished_) {
        return result_;
    }
    return TickActive(player, core, now_ms);
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::TickActive(
    const MotionPackagePlayer& player,
    MotionLiveCore* core,
    uint64_t now_ms) {
    if (!active_) {
        return MakeError("package_hardware_run_not_active");
    }
    if (finished_) {
        return result_;
    }
    if (core == nullptr) {
        ClearState();
        MotionPackageHardwareRunnerResult& result = ResetResult();
        result.ok = false;
        result.code = "package_hardware_core_missing";
        return result;
    }
    if (!player.loaded() || player.package_id() != package_id_) {
        const MotionPackageHardwareRunnerResult& stop_result =
            StopLiveSession(core, true);
        result_.ok = false;
        result_.code = "package_hardware_run_package_changed";
        return stop_result;
    }

    MotionPackageHardwareRunnerResult& result = ResetResult();
    const MotionPackagePlayerResult sample_result =
        player.Sample(ElapsedMs(start_ms_, now_ms), &result.sample);
    if (!sample_result.ok) {
        const MotionPackageHardwareRunnerResult& stop_result =
            StopLiveSession(core, true);
        result_.ok = false;
        result_.code = sample_result.code;
        return stop_result;
    }

    result.run_session_id = run_session_id_;
    result.live_session_id = live_session_id_;
    if (!live_started_) {
        std::string accepted_session_id;
        const char* arm_code = core->ArmForPackageHardwareRun(
            owner_id_, calibration_id_, access_key_valid_,
            live_session_id_, now_ms, &accepted_session_id);
        if (std::strcmp(arm_code, "ok") != 0) {
            result.ok = false;
            result.code = arm_code;
            ClearState();
            return result;
        }
        live_started_ = true;
        live_session_id_ = accepted_session_id;
        result.live_session_id = live_session_id_;
        result.sample = MotionPackageSample{};
        result.live_result.ok = true;
        result.live_result.code = "ok";
        result.live_result.message = "ok";
        result.live_result.session_id = live_session_id_;
        result.live_result.seq = live_seq_;
        result.live_result.should_apply = false;
        result.live_armed = true;
        result.hardware_apply = false;
        result.ok = true;
        result.code = "ok";
        return result;
    }

    result.live_armed = true;
    result.hardware_apply = true;
    result.live_result = core->Pose(owner_id_, live_session_id_, ++live_seq_,
                                    result.sample.target,
                                    speed_dps_, now_ms);
    if (!result.live_result.ok) {
        result.ok = false;
        result.code = result.live_result.code;
        result.stopped = result.live_result.stopped;
        ClearState();
        return result;
    }

    if (result.sample.finished) {
        uint32_t accepted_seq = live_seq_;
        const char* stop_code = core->StopForPackageHardwareRun(
            owner_id_, live_session_id_, ++live_seq_, &accepted_seq);
        result.live_result.ok = std::strcmp(stop_code, "ok") == 0;
        result.live_result.code = stop_code;
        result.live_result.message = stop_code;
        result.live_result.session_id = live_session_id_;
        result.live_result.seq = accepted_seq;
        result.live_result.stopped = true;
        result.stopped = true;
        result.ok = result.live_result.ok;
        result.code = result.live_result.ok ? "ok" : result.live_result.code;
        finished_ = true;
        return result;
    }

    result.ok = true;
    result.code = "ok";
    return result;
}

const MotionPackageHardwareRunnerResult& MotionPackageHardwareRunner::Stop(
    MotionLiveCore* core,
    int owner_id,
    const std::string& run_session_id) {
    if (!active_) {
        return MakeError("package_hardware_run_not_active");
    }
    if (!SessionMatches(owner_id, run_session_id)) {
        return MakeError("package_hardware_run_not_owner");
    }
    if (finished_) {
        return result_;
    }
    return StopLiveSession(core, true);
}

void MotionPackageHardwareRunner::OnTransportClosed(MotionLiveCore* core,
                                                   int owner_id) {
    if (active_ && owner_id == owner_id_) {
        if (core != nullptr && live_started_) {
            core->OnTransportClosed(owner_id);
        }
        ClearState();
    }
}

void MotionPackageHardwareRunner::ClearState() {
    package_id_.clear();
    run_session_id_.clear();
    live_session_id_.clear();
    calibration_id_.clear();
    owner_id_ = 0;
    start_ms_ = 0;
    live_seq_ = 0;
    speed_dps_ = kMotionEditorMaxServoRateDps;
    access_key_valid_ = false;
    live_started_ = false;
    finished_ = false;
    active_ = false;
}

void MotionPackageHardwareRunner::Clear() {
    ClearState();
    result_ = MotionPackageHardwareRunnerResult{};
}

}  // namespace gosha::motion_live

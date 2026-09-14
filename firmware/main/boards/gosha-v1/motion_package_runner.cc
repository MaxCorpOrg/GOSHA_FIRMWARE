#include "motion_package_runner.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gosha::motion_live {

namespace {

bool ValidRunSessionId(const std::string& value) {
    return value.size() >= 16 && value.size() <= 128;
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
    constexpr double kStartEpsilonDegrees = 0.0001;
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (sample.target.present[joint_index] &&
            std::fabs(sample.target.relative_degrees[joint_index]) >
                kStartEpsilonDegrees) {
            return false;
        }
    }
    return true;
}

}  // namespace

MotionPackageRunnerResult MotionPackageRunner::MakeError(
    const char* code) const {
    MotionPackageRunnerResult result;
    result.ok = false;
    result.code = code;
    result.run_session_id = run_session_id_;
    return result;
}

bool MotionPackageRunner::SessionMatches(
    int owner_id,
    const std::string& run_session_id) const {
    return running_ && owner_id == owner_id_ && !run_session_id.empty() &&
           run_session_id == run_session_id_;
}

MotionPackageRunnerResult MotionPackageRunner::SampleAt(
    const MotionPackagePlayer& player,
    uint64_t now_ms) {
    if (!running_) {
        return MakeError("package_run_not_active");
    }
    if (!player.loaded() || player.package_id() != package_id_) {
        Clear();
        MotionPackageRunnerResult result;
        result.ok = false;
        result.code = "package_run_package_changed";
        return result;
    }

    MotionPackageSample sample;
    const MotionPackagePlayerResult player_result =
        player.Sample(ElapsedMs(start_ms_, now_ms), &sample);
    if (!player_result.ok) {
        Clear();
        MotionPackageRunnerResult result;
        result.ok = false;
        result.code = player_result.code;
        return result;
    }

    MotionPackageRunnerResult result;
    result.ok = true;
    result.code = "ok";
    result.run_session_id = run_session_id_;
    result.sample = sample;
    if (sample.finished) {
        result.stopped = true;
        Clear();
    }
    return result;
}

MotionPackageRunnerResult MotionPackageRunner::Start(
    const MotionPackagePlayer& player,
    int owner_id,
    const std::string& run_session_id,
    uint64_t now_ms) {
    if (running_) {
        return MakeError("package_run_busy");
    }
    if (!player.loaded()) {
        return MakeError("package_not_loaded");
    }
    if (!ValidRunSessionId(run_session_id)) {
        return MakeError("package_run_session_id");
    }

    package_id_ = player.package_id();
    run_session_id_ = run_session_id;
    owner_id_ = owner_id;
    start_ms_ = now_ms;
    running_ = true;

    MotionPackageRunnerResult result = SampleAt(player, now_ms);
    if (!result.ok) {
        Clear();
        return result;
    }
    if (!SafeStartSample(result.sample)) {
        Clear();
        result.ok = false;
        result.code = "package_run_start_pose";
    }
    return result;
}

MotionPackageRunnerResult MotionPackageRunner::Tick(
    const MotionPackagePlayer& player,
    int owner_id,
    const std::string& run_session_id,
    uint64_t now_ms) {
    if (!running_) {
        return MakeError("package_run_not_active");
    }
    if (!SessionMatches(owner_id, run_session_id)) {
        return MakeError("package_run_not_owner");
    }
    return SampleAt(player, now_ms);
}

MotionPackageRunnerResult MotionPackageRunner::Stop(
    int owner_id,
    const std::string& run_session_id) {
    if (!running_) {
        return MakeError("package_run_not_active");
    }
    if (!SessionMatches(owner_id, run_session_id)) {
        return MakeError("package_run_not_owner");
    }
    MotionPackageRunnerResult result;
    result.ok = true;
    result.code = "ok";
    result.run_session_id = run_session_id_;
    result.stopped = true;
    Clear();
    return result;
}

void MotionPackageRunner::OnTransportClosed(int owner_id) {
    if (running_ && owner_id == owner_id_) {
        Clear();
    }
}

void MotionPackageRunner::Clear() {
    package_id_.clear();
    run_session_id_.clear();
    owner_id_ = 0;
    start_ms_ = 0;
    running_ = false;
}

}  // namespace gosha::motion_live

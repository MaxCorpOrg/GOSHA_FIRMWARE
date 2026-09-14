#ifndef GOSHA_V1_MOTION_PACKAGE_HARDWARE_RUNNER_H_
#define GOSHA_V1_MOTION_PACKAGE_HARDWARE_RUNNER_H_

#include <cstdint>
#include <string>

#include "motion_live_core.h"
#include "motion_package_player.h"

namespace gosha::motion_live {

struct MotionPackageHardwareRunnerResult {
    bool ok = false;
    const char* code = "package_hardware_run_invalid";
    std::string run_session_id;
    std::string live_session_id;
    MotionPackageSample sample{};
    MotionLiveResult live_result{};
    bool stopped = false;
    bool live_armed = false;
    bool hardware_apply = false;
};

class MotionPackageHardwareRunner {
public:
    const MotionPackageHardwareRunnerResult& Start(
        const MotionPackagePlayer& player,
        MotionLiveCore* core,
        int owner_id,
        const std::string& run_session_id,
        const std::string& live_session_id,
        const std::string& calibration_id,
        bool access_key_valid,
        double speed_dps,
        uint64_t now_ms);
    const MotionPackageHardwareRunnerResult& Tick(
        const MotionPackagePlayer& player,
        MotionLiveCore* core,
        int owner_id,
        const std::string& run_session_id,
        uint64_t now_ms);
    const MotionPackageHardwareRunnerResult& TickActive(
        const MotionPackagePlayer& player,
        MotionLiveCore* core,
        uint64_t now_ms);
    const MotionPackageHardwareRunnerResult& Stop(
        MotionLiveCore* core,
        int owner_id,
        const std::string& run_session_id);
    void OnTransportClosed(MotionLiveCore* core, int owner_id);
    void Clear();

    bool running() const { return active_ && !finished_; }
    const std::string& run_session_id() const { return run_session_id_; }
    const std::string& live_session_id() const { return live_session_id_; }

private:
    MotionPackageHardwareRunnerResult& ResetResult();
    const MotionPackageHardwareRunnerResult& MakeError(const char* code);
    bool SessionMatches(int owner_id, const std::string& run_session_id) const;
    bool StartPoseMatches(const MotionLivePose& pose,
                          const MotionPackageSample& sample) const;
    const MotionPackageHardwareRunnerResult& StopLiveSession(MotionLiveCore* core,
                                                             bool clear_runner);
    void ClearState();

    MotionPackageHardwareRunnerResult result_;
    std::string package_id_;
    std::string run_session_id_;
    std::string live_session_id_;
    std::string calibration_id_;
    int owner_id_ = 0;
    uint64_t start_ms_ = 0;
    uint32_t live_seq_ = 0;
    double speed_dps_ = kMotionEditorMaxServoRateDps;
    bool access_key_valid_ = false;
    bool live_started_ = false;
    bool finished_ = false;
    bool active_ = false;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_HARDWARE_RUNNER_H_

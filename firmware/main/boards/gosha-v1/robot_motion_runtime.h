#ifndef GOSHA_ROBOT_MOTION_RUNTIME_H_
#define GOSHA_ROBOT_MOTION_RUNTIME_H_

#include <cJSON.h>
#include <deque>
#include <string>
#include "motion_package_manager.h"
#include "motion_package_hardware_runner.h"
#include "legacy_motion_plan.h"

namespace gosha::motion_live {

// Playback API for normal robot use. Editing/upload and raw poses stay outside.
// The adapter serializes all calls with the shared hardware owner lock.
class RobotMotionRuntime {
public:
    explicit RobotMotionRuntime(MotionPackageManager* manager) : manager_(manager) {}
    cJSON* List(const MotionLivePreparedProfile* profile);
    cJSON* Play(const MotionLivePreparedProfile* profile, MotionLiveCore* core,
                int owner, const std::string& motion_id, const std::string& request_id,
                bool editor_busy, uint64_t now_ms);
    cJSON* Status() const;
    cJSON* Stop(MotionLiveCore* core, int owner);
    void Tick(MotionLiveCore* core, uint64_t now_ms);
    void OnTransportClosed(MotionLiveCore* core, int owner);
    bool running() const { return legacy_running_ || runner_.running(); }

private:
    struct Request { std::string id; std::string motion; };
    bool LoadRecord(const std::string& id,
                    MotionPackageLoadedRecord* record);
    cJSON* Error(const char* reason) const;
    MotionPackageManager* manager_;
    MotionPackagePlayer player_;
    MotionPackageHardwareRunner runner_;
    LegacyMotionPlan legacy_plan_;
    bool legacy_running_ = false;
    bool stored_after_home_ = false;
    uint64_t legacy_start_ms_ = 0;
    uint64_t last_tick_ms_ = 0;
    const MotionLivePreparedProfile* profile_ = nullptr;
    std::deque<Request> requests_;
    std::string motion_id_;
    std::string request_id_;
    std::string run_id_;
    int owner_ = 0;
    uint32_t duration_ms_ = 0;
    const char* state_ = "idle";
    const char* reason_ = "ok";
};

}  // namespace gosha::motion_live
#endif

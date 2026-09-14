#ifndef GOSHA_ROBOT_MOTION_RUNTIME_H_
#define GOSHA_ROBOT_MOTION_RUNTIME_H_

#include <cJSON.h>
#include <deque>
#include <string>
#include "motion_package_manager.h"
#include "motion_package_hardware_runner.h"

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
    bool running() const { return runner_.running(); }

private:
    struct Request { std::string id; std::string motion; };
    bool LoadRecord(const MotionLivePreparedProfile& profile, const std::string& id,
                    MotionPackageLoadedRecord* record);
    cJSON* Error(const char* reason) const;
    MotionPackageManager* manager_;
    MotionPackagePlayer player_;
    MotionPackageHardwareRunner runner_;
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

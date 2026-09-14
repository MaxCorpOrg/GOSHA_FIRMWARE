#ifndef GOSHA_V1_MOTION_PACKAGE_RUNNER_H_
#define GOSHA_V1_MOTION_PACKAGE_RUNNER_H_

#include <cstdint>
#include <string>

#include "motion_package_player.h"

namespace gosha::motion_live {

struct MotionPackageRunnerResult {
    bool ok = false;
    const char* code = "package_run_invalid";
    std::string run_session_id;
    MotionPackageSample sample{};
    bool stopped = false;
};

class MotionPackageRunner {
public:
    MotionPackageRunnerResult Start(const MotionPackagePlayer& player,
                                    int owner_id,
                                    const std::string& run_session_id,
                                    uint64_t now_ms);
    MotionPackageRunnerResult Tick(const MotionPackagePlayer& player,
                                   int owner_id,
                                   const std::string& run_session_id,
                                   uint64_t now_ms);
    MotionPackageRunnerResult Stop(int owner_id,
                                   const std::string& run_session_id);
    void OnTransportClosed(int owner_id);
    void Clear();

    bool running() const { return running_; }
    const std::string& package_id() const { return package_id_; }
    const std::string& run_session_id() const { return run_session_id_; }

private:
    MotionPackageRunnerResult MakeError(const char* code) const;
    bool SessionMatches(int owner_id, const std::string& run_session_id) const;
    MotionPackageRunnerResult SampleAt(const MotionPackagePlayer& player,
                                       uint64_t now_ms);

    std::string package_id_;
    std::string run_session_id_;
    int owner_id_ = 0;
    uint64_t start_ms_ = 0;
    bool running_ = false;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_RUNNER_H_

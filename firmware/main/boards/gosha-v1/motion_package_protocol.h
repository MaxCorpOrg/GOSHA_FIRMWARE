#ifndef GOSHA_V1_MOTION_PACKAGE_PROTOCOL_H_
#define GOSHA_V1_MOTION_PACKAGE_PROTOCOL_H_

#include <cJSON.h>

#include <functional>
#include <string>
#include <utility>

#include "motion_live_core.h"
#include "motion_package_hardware_runner.h"
#include "motion_package_manager.h"
#include "motion_package_player.h"
#include "motion_package_runner.h"

namespace gosha::motion_live {

using MotionPackageSessionIdGenerator = std::function<std::string()>;
using MotionPackageClock = std::function<uint64_t()>;

class MotionPackageProtocol {
public:
    MotionPackageProtocol(MotionPackageManager* manager,
                          MotionPackagePlayer* player,
                          MotionPackageRunner* runner,
                          MotionPackageHardwareRunner* hardware_runner,
                          MotionPackageSessionIdGenerator session_id_generator,
                          MotionPackageClock clock)
        : manager_(manager),
          player_(player),
          runner_(runner),
          hardware_runner_(hardware_runner),
          session_id_generator_(std::move(session_id_generator)),
          clock_(std::move(clock)) {}

    MotionPackageProtocol(MotionPackageManager* manager,
                          MotionPackagePlayer* player,
                          MotionPackageRunner* runner,
                          MotionPackageSessionIdGenerator session_id_generator,
                          MotionPackageClock clock)
        : MotionPackageProtocol(manager, player, runner, nullptr,
                                std::move(session_id_generator), clock) {}

    MotionPackageProtocol(MotionPackageManager* manager,
                          MotionPackagePlayer* player,
                          MotionPackageSessionIdGenerator session_id_generator)
        : MotionPackageProtocol(manager, player, nullptr,
                                std::move(session_id_generator), {}) {}

    MotionPackageProtocol(MotionPackageManager* manager,
                          MotionPackageSessionIdGenerator session_id_generator)
        : MotionPackageProtocol(manager, nullptr,
                                std::move(session_id_generator)) {}

    bool HandleMessage(cJSON* request,
                       int owner_id,
                       const MotionLivePreparedProfile* profile,
                       MotionLiveCore* core,
                       bool access_key_valid,
                       cJSON** reply);
    bool HandleMessage(cJSON* request,
                       int owner_id,
                       const MotionLivePreparedProfile* profile,
                       bool access_key_valid,
                       cJSON** reply) {
        return HandleMessage(request, owner_id, profile, nullptr,
                             access_key_valid, reply);
    }
    void OnTransportClosed(int owner_id);
    void OnLiveSessionStopped();
    void TickHardwareRun(MotionLiveCore* core, uint64_t now_ms);

    const std::string& upload_session_id() const { return upload_session_id_; }
    bool software_run_active() const { return runner_ != nullptr && runner_->running(); }
    bool hardware_run_active() const {
        return hardware_runner_ != nullptr && hardware_runner_->running();
    }
    bool run_active() const { return software_run_active() || hardware_run_active(); }

private:
    cJSON* MakeError(cJSON* request, const char* code, const char* message) const;
    cJSON* MakeStatus(cJSON* request, const char* status) const;
    bool RequireUploadSession(cJSON* request, int owner_id, cJSON** reply) const;
    std::string GenerateUploadSessionId() const;
    std::string GenerateRunSessionId() const;
    uint64_t NowMs() const;
    bool HandleHardwareRunStart(cJSON* request,
                                int owner_id,
                                const MotionLivePreparedProfile* profile,
                                MotionLiveCore* core,
                                bool access_key_valid,
                                cJSON** reply);
    bool HandleHardwareRunStatus(cJSON* request,
                                 int owner_id,
                                 MotionLiveCore* core,
                                 cJSON** reply);
    bool HandleHardwareRunStop(cJSON* request,
                               int owner_id,
                               MotionLiveCore* core,
                               cJSON** reply);

    MotionPackageManager* manager_ = nullptr;
    MotionPackagePlayer* player_ = nullptr;
    MotionPackageRunner* runner_ = nullptr;
    MotionPackageHardwareRunner* hardware_runner_ = nullptr;
    MotionPackageSessionIdGenerator session_id_generator_;
    MotionPackageClock clock_;
    std::string upload_session_id_;
    int upload_owner_id_ = 0;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_PROTOCOL_H_

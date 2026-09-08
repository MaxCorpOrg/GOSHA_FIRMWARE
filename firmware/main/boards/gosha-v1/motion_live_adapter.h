#ifndef GOSHA_V1_MOTION_LIVE_ADAPTER_H_
#define GOSHA_V1_MOTION_LIVE_ADAPTER_H_

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_timer.h>

#include <functional>
#include <mutex>

#include "motion_live_core.h"

namespace gosha::motion_live {

using MotionLiveJsonSender = std::function<esp_err_t(cJSON*)>;

class MotionLiveAdapter {
public:
    static MotionLiveAdapter& GetInstance();

    void ConfigureRuntime(const MotionLiveRuntimeConfig& runtime,
                          MotionLiveHardwareApplier applier,
                          MotionLiveRightArmInitializer right_arm_initializer,
                          MotionLivePwmDiagnosticsProvider pwm_diagnostics_provider = {});
    bool HandleWebSocketMessage(httpd_req_t* req, cJSON* root);
    bool HandleTransportMessage(int owner_id, cJSON* root,
                                const MotionLiveJsonSender& sender);
    void OnTransportClosed(int owner_id);
    void OnSocketClosed(int socket_fd);

private:
    MotionLiveAdapter();

    static void WatchdogTimerCallback(void* arg);

    bool EnsureWatchdogTimerStarted();
    void WatchdogTick();
    uint64_t NowMs() const;
    std::string GenerateSessionId() const;
    bool AccessKeyMatches(const char* access_key) const;

    esp_err_t SendJsonFrame(const MotionLiveJsonSender& sender, cJSON* root) const;
    esp_err_t SendError(const MotionLiveJsonSender& sender, cJSON* request,
                        const char* code, const char* message) const;
    esp_err_t SendCapabilities(const MotionLiveJsonSender& sender, cJSON* request,
                               const MotionLiveCapabilities& caps) const;
    esp_err_t SendArmed(const MotionLiveJsonSender& sender, cJSON* request,
                        const MotionLiveResult& result) const;
    esp_err_t SendAck(const MotionLiveJsonSender& sender,
                      const MotionLiveResult& result) const;
    esp_err_t SendStopped(const MotionLiveJsonSender& sender,
                          const MotionLiveResult& result,
                          const std::string& fallback_session_id) const;

    MotionLiveCore core_;
    mutable std::mutex mutex_;
    esp_timer_handle_t watchdog_timer_ = nullptr;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_LIVE_ADAPTER_H_

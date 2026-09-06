#ifndef GOSHA_V1_MOTION_LIVE_ADAPTER_H_
#define GOSHA_V1_MOTION_LIVE_ADAPTER_H_

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_timer.h>

#include <mutex>

#include "motion_live_core.h"

namespace gosha::motion_live {

class MotionLiveAdapter {
public:
    static MotionLiveAdapter& GetInstance();

    void ConfigureRuntime(const MotionLiveRuntimeConfig& runtime,
                          MotionLiveHardwareApplier applier);
    bool HandleWebSocketMessage(httpd_req_t* req, cJSON* root);
    void OnSocketClosed(int socket_fd);

private:
    MotionLiveAdapter();

    static void WatchdogTimerCallback(void* arg);

    bool EnsureWatchdogTimerStarted();
    void WatchdogTick();
    uint64_t NowMs() const;
    std::string GenerateSessionId() const;
    bool AccessKeyMatches(const char* access_key) const;

    esp_err_t SendJsonFrame(httpd_req_t* req, cJSON* root) const;
    esp_err_t SendError(httpd_req_t* req, cJSON* request, const char* code,
                        const char* message) const;
    esp_err_t SendCapabilities(httpd_req_t* req, cJSON* request,
                               const MotionLiveCapabilities& caps) const;
    esp_err_t SendArmed(httpd_req_t* req, cJSON* request,
                        const MotionLiveResult& result) const;
    esp_err_t SendAck(httpd_req_t* req, const MotionLiveResult& result) const;
    esp_err_t SendStopped(httpd_req_t* req, const MotionLiveResult& result,
                          const std::string& fallback_session_id) const;

    MotionLiveCore core_;
    mutable std::mutex mutex_;
    esp_timer_handle_t watchdog_timer_ = nullptr;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_LIVE_ADAPTER_H_

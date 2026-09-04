#ifndef RUNTIME_EVENT_REPORTER_H
#define RUNTIME_EVENT_REPORTER_H

#include <atomic>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class RuntimeEventReporter {
public:
    static RuntimeEventReporter& GetInstance();

    void Start();
    void PublishDeviceState(const char* previous_state, const char* current_state);
    void PublishNetworkState(const char* state, const char* status, const char* severity = "info",
                             const char* error_code = nullptr);
    void PublishHeartbeat();
    void MaybePublishHeartbeat(uint32_t uptime_seconds);
    void PublishVoiceTurnPhase(const char* phase, const char* warm_state,
                               const std::string& correlation_id, const std::string& task_id);

private:
    RuntimeEventReporter();
    ~RuntimeEventReporter() = default;
    RuntimeEventReporter(const RuntimeEventReporter&) = delete;
    RuntimeEventReporter& operator=(const RuntimeEventReporter&) = delete;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<uint32_t> sequence_{0};
    std::atomic<uint32_t> heartbeat_interval_seconds_{30};
    std::atomic<uint32_t> last_heartbeat_seconds_{0};
    std::string session_id_;

    void Enqueue(std::string&& payload);
    std::string BuildEvent(const char* event_type, const char* severity,
                           const char* state_domain, const char* state_name, const char* state_status,
                           const char* link_kind, const char* link_status,
                           const char* previous_state = nullptr, const char* error_code = nullptr,
                           bool heartbeat = false);
    std::string BuildVoiceTurnPhaseEvent(const char* phase, const char* warm_state,
                                         const std::string& correlation_id, const std::string& task_id);
    int Send(const std::string& payload);
    void Run();
    static void TaskEntry(void* arg);
};

#endif  // RUNTIME_EVENT_REPORTER_H

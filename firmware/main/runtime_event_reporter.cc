#include "runtime_event_reporter.h"

#include "board.h"
#include "settings.h"
#include "system_info.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_random.h>

#include <cstdio>
#include <ctime>
#include <new>
#include <utility>

#define TAG "RuntimeEvents"

namespace {
constexpr size_t kQueueLength = 16;
constexpr TickType_t kRetryDelay = pdMS_TO_TICKS(5000);
constexpr int kRuntimeHttpConnectId = 4;
constexpr int kRuntimeHttpTimeoutMs = 5000;

void AddString(cJSON* object, const char* key, const char* value) {
    if (object != nullptr && value != nullptr && value[0] != '\0') {
        cJSON_AddStringToObject(object, key, value);
    }
}
}  // namespace

RuntimeEventReporter& RuntimeEventReporter::GetInstance() {
    static RuntimeEventReporter instance;
    return instance;
}

RuntimeEventReporter::RuntimeEventReporter() {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "boot-%08lx", static_cast<unsigned long>(esp_random()));
    session_id_ = buffer;
}

void RuntimeEventReporter::Start() {
    if (task_ != nullptr) {
        return;
    }
    queue_ = xQueueCreate(kQueueLength, sizeof(std::string*));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create runtime event queue");
        return;
    }
    if (xTaskCreate(TaskEntry, "runtime_events", 6144, this, 1, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create runtime event task");
        vQueueDelete(queue_);
        queue_ = nullptr;
        task_ = nullptr;
    }
}

void RuntimeEventReporter::Enqueue(std::string&& payload) {
    if (queue_ == nullptr || payload.empty()) {
        return;
    }
    auto* queued = new (std::nothrow) std::string(std::move(payload));
    if (queued == nullptr) {
        ESP_LOGW(TAG, "Runtime event allocation failed");
        return;
    }
    if (xQueueSend(queue_, &queued, 0) != pdTRUE) {
        std::string* oldest = nullptr;
        if (xQueueReceive(queue_, &oldest, 0) == pdTRUE) {
            delete oldest;
        }
        if (xQueueSend(queue_, &queued, 0) != pdTRUE) {
            delete queued;
        }
        ESP_LOGW(TAG, "Runtime event queue full; oldest queued observation replaced");
    }
}

std::string RuntimeEventReporter::BuildEvent(
    const char* event_type,
    const char* severity,
    const char* state_domain,
    const char* state_name,
    const char* state_status,
    const char* link_kind,
    const char* link_status,
    const char* previous_state,
    const char* error_code,
    bool heartbeat) {
    const uint32_t sequence = ++sequence_;
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "%s-%lu", session_id_.c_str(), static_cast<unsigned long>(sequence));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "schema_version", "gosha.runtime.event.v1");
    cJSON_AddStringToObject(root, "event_id", event_id);
    cJSON_AddStringToObject(root, "event_type", event_type);
    cJSON_AddStringToObject(root, "severity", severity);
    cJSON_AddNumberToObject(root, "sequence", sequence);

    cJSON* source = cJSON_AddObjectToObject(root, "source");
    cJSON_AddStringToObject(source, "instance_id", session_id_.c_str());
    cJSON_AddStringToObject(source, "firmware_version", esp_app_get_description()->version);

    cJSON* trace = cJSON_AddObjectToObject(root, "trace");
    cJSON_AddStringToObject(trace, "session_id", session_id_.c_str());

    const time_t now = time(nullptr);
    if (now > 1577836800) {
        struct tm utc = {};
        gmtime_r(&now, &utc);
        char occurred_at[32];
        strftime(occurred_at, sizeof(occurred_at), "%Y-%m-%dT%H:%M:%SZ", &utc);
        cJSON_AddStringToObject(root, "occurred_at", occurred_at);
    }

    cJSON* state = cJSON_AddObjectToObject(root, "state");
    AddString(state, "domain", state_domain);
    AddString(state, "name", state_name);
    AddString(state, "status", state_status);
    AddString(state, "previous", previous_state);

    if (link_kind != nullptr && link_kind[0] != '\0') {
        cJSON* link = cJSON_AddObjectToObject(root, "link");
        AddString(link, "kind", link_kind);
        AddString(link, "status", link_status);
    }

    if (error_code != nullptr && error_code[0] != '\0') {
        cJSON* error = cJSON_AddObjectToObject(root, "error");
        cJSON_AddStringToObject(error, "code", error_code);
        cJSON_AddStringToObject(error, "message", "Сеть робота требует восстановления");
        cJSON_AddBoolToObject(error, "retryable", true);
    }

    if (heartbeat) {
        cJSON* metrics = cJSON_AddObjectToObject(root, "metrics");
        cJSON_AddNumberToObject(metrics, "free_heap_bytes", SystemInfo::GetFreeHeapSize());
        cJSON_AddNumberToObject(metrics, "minimum_free_heap_bytes", SystemInfo::GetMinimumFreeHeapSize());
        int battery_level = 0;
        bool charging = false;
        bool discharging = false;
        if (Board::GetInstance().GetBatteryLevel(battery_level, charging, discharging)) {
            cJSON_AddNumberToObject(metrics, "battery_percent", battery_level);
            cJSON_AddBoolToObject(metrics, "charging", charging);
        }
    }

    char* printed = cJSON_PrintUnformatted(root);
    std::string result = printed != nullptr ? printed : "";
    cJSON_free(printed);
    cJSON_Delete(root);
    return result;
}

void RuntimeEventReporter::PublishDeviceState(const char* previous_state, const char* current_state) {
    Enqueue(BuildEvent(
        "robot.device.state_changed",
        "info",
        "device",
        current_state,
        "active",
        "robot_platform",
        "available",
        previous_state));
}

void RuntimeEventReporter::PublishNetworkState(
    const char* state,
    const char* status,
    const char* severity,
    const char* error_code) {
    Enqueue(BuildEvent(
        "robot.network.state_changed",
        severity,
        "network",
        state,
        status,
        "robot_platform",
        status,
        nullptr,
        error_code));
}

void RuntimeEventReporter::PublishHeartbeat() {
    Enqueue(BuildEvent(
        "robot.runtime.heartbeat",
        "info",
        "runtime",
        "firmware",
        "running",
        "robot_platform",
        "available",
        nullptr,
        nullptr,
        true));
}

void RuntimeEventReporter::MaybePublishHeartbeat(uint32_t uptime_seconds) {
    const uint32_t interval = heartbeat_interval_seconds_.load();
    const uint32_t previous = last_heartbeat_seconds_.load();
    if (uptime_seconds < previous + interval) {
        return;
    }
    last_heartbeat_seconds_.store(uptime_seconds);
    PublishHeartbeat();
}

int RuntimeEventReporter::Send(const std::string& payload) {
    Settings settings("runtime_events", false);
    const std::string url = settings.GetString("url");
    const std::string token = settings.GetString("token");
    const int configured_interval = settings.GetInt("heartbeat_sec", 30);
    heartbeat_interval_seconds_.store(static_cast<uint32_t>(configured_interval < 10 ? 10 : configured_interval));
    if (url.empty() || token.empty()) {
        return 0;
    }

    auto& board = Board::GetInstance();
    auto http = board.GetNetwork()->CreateHttp(kRuntimeHttpConnectId);
    http->SetTimeout(kRuntimeHttpTimeoutMs);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid().c_str());
    http->SetHeader("Authorization", "Bearer " + token);
    std::string body = payload;
    http->SetContent(std::move(body));
    if (!http->Open("POST", url)) {
        ESP_LOGW(TAG, "Runtime event delivery unavailable");
        return -1;
    }
    const int status = http->GetStatusCode();
    // HttpClient receives data in a background task. Drain the response before
    // closing so that the client cannot be destroyed while its receive callback
    // is still finishing the server-side disconnect.
    (void)http->ReadAll();
    http->Close();
    if (status >= 200 && status < 300) {
        return 1;
    }
    if (status == 413 || status == 422) {
        ESP_LOGW(TAG, "Runtime event rejected, status=%d", status);
        return 0;
    }
    ESP_LOGW(TAG, "Runtime event delivery failed, status=%d", status);
    return -1;
}

void RuntimeEventReporter::TaskEntry(void* arg) {
    static_cast<RuntimeEventReporter*>(arg)->Run();
}

void RuntimeEventReporter::Run() {
    std::string* pending = nullptr;
    while (true) {
        if (pending == nullptr) {
            xQueueReceive(queue_, &pending, portMAX_DELAY);
        }
        if (pending == nullptr) {
            continue;
        }
        const int result = Send(*pending);
        if (result >= 0) {
            delete pending;
            pending = nullptr;
            continue;
        }
        // Do not let one failed old event block newer recovery observations.
        // Requeue it behind current data; if the bounded queue is already full,
        // preserve the newer queued states and discard this older retry.
        if (xQueueSend(queue_, &pending, 0) != pdTRUE) {
            delete pending;
        }
        pending = nullptr;
        vTaskDelay(kRetryDelay);
    }
}

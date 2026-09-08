#include "motion_live_adapter.h"
#include "motion_live_auth.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/md.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>

#include "sdkconfig.h"

#if defined(CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN) && \
    defined(GOSHA_MOTION_LIVE_PROFILE_HEADER_ENABLED)
#include GOSHA_MOTION_LIVE_PROFILE_HEADER
#endif

namespace gosha::motion_live {

namespace {

constexpr const char* TAG = "MotionLive";
constexpr int64_t kWatchdogTickPeriodUs = 50 * 1000;

bool JsonStringEquals(cJSON* item, const char* value) {
    return item != nullptr && cJSON_IsString(item) && item->valuestring != nullptr &&
           std::strcmp(item->valuestring, value) == 0;
}

bool IsObject(cJSON* item) {
    return item != nullptr && cJSON_IsObject(item);
}

void CopyRequestField(cJSON* reply, cJSON* request, const char* source_name,
                      const char* reply_name) {
    cJSON* value = cJSON_GetObjectItem(request, source_name);
    if (value == nullptr) {
        return;
    }
    cJSON* copied = cJSON_Duplicate(value, 1);
    if (copied != nullptr) {
        cJSON_AddItemToObject(reply, reply_name, copied);
    }
}

bool ReadString(cJSON* object, const char* key, std::string* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    *out = item->valuestring;
    return true;
}

bool ReadSeq(cJSON* object, uint32_t* out) {
    cJSON* item = cJSON_GetObjectItem(object, "seq");
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 1.0 ||
        item->valuedouble > static_cast<double>(UINT32_MAX) ||
        std::floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    *out = static_cast<uint32_t>(item->valuedouble);
    return true;
}

bool ReadNumber(cJSON* object, const char* key, double* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

MotionLiveTarget ReadTarget(cJSON* object) {
    MotionLiveTarget target;
    cJSON* target_object = cJSON_GetObjectItem(object, "target");
    if (!IsObject(target_object)) {
        target.has_unknown_joint = true;
        return target;
    }
    for (cJSON* item = target_object->child; item != nullptr; item = item->next) {
        const int index = FindJointIndexById(item->string);
        if (index < 0 || target.present[index] || !cJSON_IsNumber(item) ||
            !std::isfinite(item->valuedouble)) {
            target.has_unknown_joint = true;
            continue;
        }
        target.present[index] = true;
        target.relative_degrees[index] = item->valuedouble;
    }
    return target;
}

void AddPoseObject(cJSON* parent, const char* name, const MotionLivePose& pose) {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) {
        cJSON_AddNullToObject(parent, name);
        return;
    }
    for (int i = 0; i < kPoseJointCount; ++i) {
        cJSON_AddNumberToObject(object, kJointSpecs[i].id, pose.relative_degrees[i]);
    }
    cJSON_AddItemToObject(parent, name, object);
}

void AddServoDegreesObject(cJSON* parent,
                           const char* name,
                           const std::array<int, kPoseJointCount>& servo_degrees) {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) {
        cJSON_AddNullToObject(parent, name);
        return;
    }
    for (int i = 0; i < kPoseJointCount; ++i) {
        cJSON_AddNumberToObject(object, kServoSlotKeys[i], servo_degrees[i]);
    }
    cJSON_AddItemToObject(parent, name, object);
}

void AddOptionalNumber(cJSON* parent, const char* name, bool available, double value) {
    if (available) {
        cJSON_AddNumberToObject(parent, name, value);
    } else {
        cJSON_AddNullToObject(parent, name);
    }
}

void AddPwmDiagnosticsObject(cJSON* parent,
                             const char* name,
                             const MotionLivePwmDiagnostics& diagnostics) {
    cJSON* object = cJSON_CreateObject();
    if (object == nullptr) {
        cJSON_AddNullToObject(parent, name);
        return;
    }
    cJSON* servos = cJSON_CreateArray();
    if (servos == nullptr) {
        cJSON_Delete(object);
        cJSON_AddNullToObject(parent, name);
        return;
    }
    for (int i = 0; i < kPoseJointCount; ++i) {
        const auto& servo = diagnostics.servos[i];
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr) {
            continue;
        }
        const char* id = (servo.id != nullptr && servo.id[0] != '\0')
                             ? servo.id
                             : kServoSlotKeys[i];
        cJSON_AddStringToObject(item, "id", id);
        cJSON_AddStringToObject(item, "servo_key", kServoSlotKeys[i]);
        if (servo.joint_id != nullptr && servo.joint_id[0] != '\0') {
            cJSON_AddStringToObject(item, "joint_id", servo.joint_id);
        } else {
            cJSON_AddNullToObject(item, "joint_id");
        }
        cJSON_AddBoolToObject(item, "available", servo.available);
        cJSON_AddBoolToObject(item, "attached", servo.attached);
        if (servo.pin >= 0) {
            cJSON_AddNumberToObject(item, "pin", servo.pin);
        } else {
            cJSON_AddNullToObject(item, "pin");
        }
        if (servo.channel >= 0) {
            cJSON_AddNumberToObject(item, "channel", servo.channel);
        } else {
            cJSON_AddNullToObject(item, "channel");
        }
        cJSON_AddBoolToObject(item, "frequency_available",
                              servo.frequency_available);
        AddOptionalNumber(item, "freq_hz", servo.frequency_available,
                          servo.frequency_hz);
        cJSON_AddBoolToObject(item, "duty_available", servo.duty_available);
        AddOptionalNumber(item, "duty", servo.duty_available, servo.duty);
        cJSON_AddBoolToObject(item, "last_write_available",
                              servo.last_write_available);
        AddOptionalNumber(item, "requested_angle", servo.last_write_available,
                          servo.requested_angle_degrees);
        AddOptionalNumber(item, "software_angle", servo.last_write_ok,
                          servo.software_angle_degrees);
        AddOptionalNumber(item, "applied_angle", servo.last_write_ok,
                          servo.applied_angle_degrees);
        AddOptionalNumber(item, "applied_duty", servo.last_write_ok,
                          servo.applied_duty);
        AddOptionalNumber(item, "last_write_ms", servo.last_write_available,
                          servo.last_write_ms);
        cJSON_AddBoolToObject(item, "last_write_ok", servo.last_write_ok);
        cJSON_AddBoolToObject(item, "skipped_unattached",
                              servo.skipped_unattached);
        cJSON_AddItemToArray(servos, item);
    }
    cJSON_AddItemToObject(object, "servos", servos);
    cJSON_AddItemToObject(parent, name, object);
}

const MotionLivePreparedProfile* PreparedProfileOrNull() {
#if defined(CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN) && \
    defined(GOSHA_MOTION_LIVE_PROFILE_HEADER_ENABLED)
    return &kGoshaMotionLivePreparedProfile;
#else
    return nullptr;
#endif
}

bool LocalOptInEnabled() {
#if defined(CONFIG_GOSHA_MOTION_LIVE_LOCAL_OPT_IN) && \
    defined(GOSHA_MOTION_LIVE_PROFILE_HEADER_ENABLED)
    return true;
#else
    return false;
#endif
}

const char* PreparedAccessKeyHashOrNull() {
    const MotionLivePreparedProfile* profile = PreparedProfileOrNull();
    return profile == nullptr ? nullptr : profile->access_key_sha256;
}

}  // namespace

MotionLiveAdapter& MotionLiveAdapter::GetInstance() {
    static MotionLiveAdapter instance;
    return instance;
}

MotionLiveAdapter::MotionLiveAdapter() {
    core_.SetLocalOptInEnabled(LocalOptInEnabled());
    core_.SetPreparedProfile(PreparedProfileOrNull());
}

void MotionLiveAdapter::ConfigureRuntime(const MotionLiveRuntimeConfig& runtime,
                                         MotionLiveHardwareApplier applier,
                                         MotionLiveRightArmInitializer right_arm_initializer,
                                         MotionLivePwmDiagnosticsProvider pwm_diagnostics_provider) {
    MotionLiveRuntimeConfig checked_runtime = runtime;
    checked_runtime.watchdog_tick_ready = EnsureWatchdogTimerStarted();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        core_.SetRuntimeConfig(checked_runtime);
        core_.SetHardwareApplier(std::move(applier));
        core_.SetRightArmInitializer(std::move(right_arm_initializer));
        core_.SetPwmDiagnosticsProvider(std::move(pwm_diagnostics_provider));
    }
}

bool MotionLiveAdapter::EnsureWatchdogTimerStarted() {
    if (watchdog_timer_ != nullptr) {
        return true;
    }
    const esp_timer_create_args_t timer_args = {
        .callback = &MotionLiveAdapter::WatchdogTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "gosha_live_watchdog",
        .skip_unhandled_events = true,
    };
    esp_err_t ret = esp_timer_create(&timer_args, &watchdog_timer_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Live watchdog timer create failed: %d", ret);
        watchdog_timer_ = nullptr;
        return false;
    }
    ret = esp_timer_start_periodic(watchdog_timer_, kWatchdogTickPeriodUs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Live watchdog timer start failed: %d", ret);
        esp_timer_delete(watchdog_timer_);
        watchdog_timer_ = nullptr;
        return false;
    }
    return true;
}

void MotionLiveAdapter::WatchdogTimerCallback(void* arg) {
    static_cast<MotionLiveAdapter*>(arg)->WatchdogTick();
}

void MotionLiveAdapter::WatchdogTick() {
    MotionLiveTickResult result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result = core_.Tick(NowMs());
    }
    if (result.stopped) {
        ESP_LOGW(TAG, "Live session disarmed by watchdog: %s", result.code);
    }
}

uint64_t MotionLiveAdapter::NowMs() const {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

std::string MotionLiveAdapter::GenerateSessionId() const {
    uint32_t words[4] = {esp_random(), esp_random(), esp_random(), esp_random()};
    char buffer[33] = {};
    std::snprintf(buffer, sizeof(buffer), "%08lx%08lx%08lx%08lx",
                  static_cast<unsigned long>(words[0]),
                  static_cast<unsigned long>(words[1]),
                  static_cast<unsigned long>(words[2]),
                  static_cast<unsigned long>(words[3]));
    return std::string(buffer);
}

bool MotionLiveAdapter::AccessKeyMatches(const char* access_key) const {
    if (access_key == nullptr) {
        return false;
    }
    const size_t len = std::strlen(access_key);
    if (len < 16 || len > 128) {
        return false;
    }
    const char* expected_hex = PreparedAccessKeyHashOrNull();
    if (!IsSha256Hex(expected_hex)) {
        return false;
    }

    unsigned char digest[32] = {};
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr ||
        mbedtls_md_setup(&ctx, info, 0) != 0 ||
        mbedtls_md_starts(&ctx) != 0 ||
        mbedtls_md_update(&ctx, reinterpret_cast<const unsigned char*>(access_key), len) != 0 ||
        mbedtls_md_finish(&ctx, digest) != 0) {
        mbedtls_md_free(&ctx);
        return false;
    }
    mbedtls_md_free(&ctx);

    char actual_hex[65] = {};
    for (int i = 0; i < 32; ++i) {
        std::snprintf(actual_hex + i * 2, 3, "%02x", digest[i]);
    }
    return ConstantTimeEquals64(actual_hex, expected_hex);
}

esp_err_t MotionLiveAdapter::SendJsonFrame(const MotionLiveJsonSender& sender,
                                            cJSON* root) const {
    if (!sender) {
        ESP_LOGE(TAG, "Live response sender is unavailable");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = sender(root);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Live response send failed: %d", ret);
    }
    return ret;
}

esp_err_t MotionLiveAdapter::SendError(const MotionLiveJsonSender& sender,
                                       cJSON* request, const char* code,
                                       const char* message) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "error");
    cJSON_AddStringToObject(reply, "code", code);
    cJSON_AddStringToObject(reply, "message", message);
    CopyRequestField(reply, request, "request_id", "request_id");
    CopyRequestField(reply, request, "session_id", "session_id");
    esp_err_t ret = SendJsonFrame(sender, reply);
    cJSON_Delete(reply);
    return ret;
}

esp_err_t MotionLiveAdapter::SendCapabilities(const MotionLiveJsonSender& sender,
                                              cJSON* request,
                                              const MotionLiveCapabilities& caps) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "capabilities");
    CopyRequestField(reply, request, "request_id", "request_id");
    cJSON_AddBoolToObject(reply, "motion_allowed", caps.motion_allowed);
    cJSON_AddStringToObject(reply, "reason", caps.reason);
    cJSON_AddStringToObject(reply, "mode", caps.mode);
    cJSON_AddBoolToObject(reply, "commissioning", caps.commissioning);
    cJSON_AddStringToObject(reply, "profile_id", caps.profile_id);
    cJSON_AddBoolToObject(reply, "calibrated", caps.calibrated);
    cJSON_AddStringToObject(reply, "calibration_id", caps.calibration_id);
    cJSON_AddStringToObject(reply, "stop_mode", caps.stop_mode);
    cJSON_AddNumberToObject(reply, "watchdog_ms", caps.watchdog_ms);
    cJSON_AddNumberToObject(reply, "max_rate_hz", caps.max_rate_hz);
    cJSON_AddBoolToObject(reply, "auth_required", caps.auth_required);
    cJSON_AddBoolToObject(reply, "initialization_required", caps.initialization_required);
    cJSON_AddBoolToObject(reply, "right_arm_available", caps.right_arm_available);
    cJSON_AddBoolToObject(reply, "right_arm_initialized", caps.right_arm_initialized);
    if (caps.initialization_op != nullptr && caps.initialization_op[0] != '\0') {
        cJSON_AddStringToObject(reply, "initialization_op", caps.initialization_op);
    }

    cJSON* limits = cJSON_CreateArray();
    if (limits != nullptr) {
        for (int i = 0; i < caps.joint_limit_count; ++i) {
            const auto& limit = caps.joint_limits[i];
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) {
                continue;
            }
            cJSON_AddStringToObject(item, "id", limit.id);
            cJSON_AddNumberToObject(item, "min", limit.min_relative_degrees);
            cJSON_AddNumberToObject(item, "max", limit.max_relative_degrees);
            cJSON_AddNumberToObject(item, "max_speed_dps", limit.max_speed_dps);
            cJSON_AddItemToArray(limits, item);
        }
    }
    if (limits == nullptr) {
        limits = cJSON_CreateArray();
    }
    cJSON_AddItemToObject(reply, "joint_limits", limits);
    AddPoseObject(reply, "commanded_pose", caps.commanded_pose);
    AddServoDegreesObject(reply, "servo_degrees", caps.servo_degrees);
    AddPwmDiagnosticsObject(reply, "pwm_diagnostics", caps.pwm_diagnostics);
    cJSON* feedback = cJSON_CreateObject();
    if (feedback != nullptr) {
        cJSON_AddBoolToObject(feedback, "measured_position", false);
        cJSON_AddBoolToObject(feedback, "imu", false);
        cJSON_AddItemToObject(reply, "feedback", feedback);
    }

    esp_err_t ret = SendJsonFrame(sender, reply);
    cJSON_Delete(reply);
    return ret;
}

esp_err_t MotionLiveAdapter::SendArmed(const MotionLiveJsonSender& sender,
                                       cJSON* request,
                                       const MotionLiveResult& result) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "armed");
    CopyRequestField(reply, request, "request_id", "request_id");
    cJSON_AddStringToObject(reply, "session_id", result.session_id.c_str());
    const MotionLivePreparedProfile* profile = PreparedProfileOrNull();
    cJSON_AddStringToObject(reply, "calibration_id",
                            profile == nullptr ? "" : profile->calibration_id);
    esp_err_t ret = SendJsonFrame(sender, reply);
    cJSON_Delete(reply);
    return ret;
}

esp_err_t MotionLiveAdapter::SendAck(const MotionLiveJsonSender& sender,
                                     const MotionLiveResult& result) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "ack");
    cJSON_AddStringToObject(reply, "session_id", result.session_id.c_str());
    cJSON_AddNumberToObject(reply, "seq", result.seq);
    cJSON_AddBoolToObject(reply, "should_apply", result.should_apply);
    AddPoseObject(reply, "commanded_pose", result.commanded_pose);
    AddServoDegreesObject(reply, "servo_degrees", result.servo_degrees);
    AddPwmDiagnosticsObject(reply, "pwm_diagnostics", result.pwm_diagnostics);
    cJSON_AddNullToObject(reply, "measured_pose");
    cJSON_AddNullToObject(reply, "tilt");
    esp_err_t ret = SendJsonFrame(sender, reply);
    cJSON_Delete(reply);
    return ret;
}

esp_err_t MotionLiveAdapter::SendStopped(const MotionLiveJsonSender& sender,
                                         const MotionLiveResult& result,
                                         const std::string& fallback_session_id) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "stopped");
    const std::string& session_id = result.session_id.empty() ? fallback_session_id : result.session_id;
    cJSON_AddStringToObject(reply, "session_id", session_id.c_str());
    cJSON_AddBoolToObject(reply, "should_apply", result.should_apply);
    AddPoseObject(reply, "commanded_pose", result.commanded_pose);
    AddServoDegreesObject(reply, "servo_degrees", result.servo_degrees);
    AddPwmDiagnosticsObject(reply, "pwm_diagnostics", result.pwm_diagnostics);
    cJSON_AddNullToObject(reply, "measured_pose");
    cJSON_AddNullToObject(reply, "tilt");
    esp_err_t ret = SendJsonFrame(sender, reply);
    cJSON_Delete(reply);
    return ret;
}

bool MotionLiveAdapter::HandleWebSocketMessage(httpd_req_t* req, cJSON* root) {
    const int owner_id = httpd_req_to_sockfd(req);
    MotionLiveJsonSender sender = [req](cJSON* reply) -> esp_err_t {
        char* text = cJSON_PrintUnformatted(reply);
        if (text == nullptr) {
            ESP_LOGE(TAG, "Live response serialization failed");
            return ESP_ERR_NO_MEM;
        }

        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(text);
        frame.len = std::strlen(text);

        esp_err_t ret = httpd_ws_send_frame(req, &frame);
        cJSON_free(text);
        return ret;
    };
    return HandleTransportMessage(owner_id, root, sender);
}

bool MotionLiveAdapter::HandleTransportMessage(int owner_id, cJSON* root,
                                               const MotionLiveJsonSender& sender) {
    if (!IsObject(root)) {
        return false;
    }
    if (!JsonStringEquals(cJSON_GetObjectItem(root, "protocol"), kProtocol)) {
        return false;
    }

    cJSON* op_item = cJSON_GetObjectItem(root, "op");
    if (!cJSON_IsString(op_item) || op_item->valuestring == nullptr) {
        SendError(sender, root, "bad_json", "Live op is required");
        return true;
    }

    const char* op = op_item->valuestring;

    if (std::strcmp(op, "hello") == 0) {
        MotionLiveCapabilities caps;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            caps = core_.GetCapabilities();
        }
        SendCapabilities(sender, root, caps);
        return true;
    }

    if (std::strcmp(op, "initialize_right_arm") == 0) {
        std::string request_id;
        std::string calibration_id;
        if (!ReadString(root, "request_id", &request_id) ||
            !ReadString(root, "calibration_id", &calibration_id)) {
            SendError(sender, root, "bad_json",
                      "Live initialize_right_arm requires request_id and calibration_id");
            return true;
        }
        cJSON* key = cJSON_GetObjectItem(root, "access_key");
        const bool access_ok =
            cJSON_IsString(key) && key->valuestring != nullptr && AccessKeyMatches(key->valuestring);
        MotionLiveResult result;
        MotionLiveCapabilities caps;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = core_.InitializeRightArm(owner_id, calibration_id, access_ok);
            if (result.ok) {
                caps = core_.GetCapabilities();
            }
        }
        if (result.ok) {
            SendCapabilities(sender, root, caps);
        } else {
            SendError(sender, root, result.code, result.message);
        }
        return true;
    }

    if (std::strcmp(op, "arm") == 0) {
        std::string calibration_id;
        if (!ReadString(root, "calibration_id", &calibration_id)) {
            SendError(sender, root, "bad_json", "Live calibration_id is required");
            return true;
        }
        cJSON* key = cJSON_GetObjectItem(root, "access_key");
        const bool access_ok =
            cJSON_IsString(key) && key->valuestring != nullptr && AccessKeyMatches(key->valuestring);
        MotionLiveResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = core_.Arm(owner_id, calibration_id, access_ok,
                               GenerateSessionId(), NowMs());
        }
        if (result.ok) {
            SendArmed(sender, root, result);
        } else {
            SendError(sender, root, result.code, result.message);
        }
        return true;
    }

    if (std::strcmp(op, "pose") == 0) {
        std::string session_id;
        uint32_t seq = 0;
        double speed_dps = 0.0;
        if (!ReadString(root, "session_id", &session_id) ||
            !ReadSeq(root, &seq) ||
            !ReadNumber(root, "speed_dps", &speed_dps)) {
            SendError(sender, root, "bad_json", "Live pose requires session_id, seq and speed_dps");
            return true;
        }
        MotionLiveTarget target = ReadTarget(root);
        MotionLiveResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = core_.Pose(owner_id, session_id, seq, target, speed_dps, NowMs());
        }
        if (result.ok) {
            SendAck(sender, result);
        } else {
            SendError(sender, root, result.code, result.message);
        }
        return true;
    }

    if (std::strcmp(op, "keepalive") == 0) {
        std::string session_id;
        uint32_t seq = 0;
        if (!ReadString(root, "session_id", &session_id) || !ReadSeq(root, &seq)) {
            SendError(sender, root, "bad_json", "Live keepalive requires session_id and seq");
            return true;
        }
        MotionLiveResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = core_.Keepalive(owner_id, session_id, seq, NowMs());
        }
        if (result.ok) {
            SendAck(sender, result);
        } else {
            SendError(sender, root, result.code, result.message);
        }
        return true;
    }

    if (std::strcmp(op, "stop") == 0) {
        std::string session_id;
        uint32_t seq = 0;
        if (!ReadString(root, "session_id", &session_id) || !ReadSeq(root, &seq)) {
            SendError(sender, root, "bad_json", "Live stop requires session_id and seq");
            return true;
        }
        MotionLiveResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result = core_.Stop(owner_id, session_id, seq);
        }
        if (result.stopped) {
            SendStopped(sender, result, session_id);
        } else {
            SendError(sender, root, result.code, result.message);
        }
        return true;
    }

    SendError(sender, root, "bad_json", "Unknown Live op");
    return true;
}

void MotionLiveAdapter::OnTransportClosed(int owner_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    core_.OnTransportClosed(owner_id);
}

void MotionLiveAdapter::OnSocketClosed(int socket_fd) {
    OnTransportClosed(socket_fd);
}

}  // namespace gosha::motion_live

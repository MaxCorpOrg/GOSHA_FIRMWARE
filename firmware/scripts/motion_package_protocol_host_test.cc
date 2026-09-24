#include <cstdint>
#include <cstdlib>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_protocol.h"

using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionLiveCore;
using gosha::motion_live::MotionLiveRuntimeConfig;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageHardwareRunner;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackageManager;
using gosha::motion_live::MotionPackagePlayer;
using gosha::motion_live::MotionPackageProtocol;
using gosha::motion_live::MotionPackageRunner;
using gosha::motion_live::MotionPackageStore;
using gosha::motion_live::MotionPackageStoreBackend;
using gosha::motion_live::MotionPackageStoreRecord;
using gosha::motion_live::ServoSlot;
using gosha::motion_live::kPoseJointCount;
using gosha::motion_live::kMotionPackageStoreActiveKey;
using gosha::motion_live::kMotionPackageStoreCatalogKey;
using gosha::motion_live::kMotionPackageUploadMaxChunkBytes;
using gosha::motion_live::kProtocol;
using gosha::motion_live::kRightArmHomeDegrees;

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; \
            return 1;                                                                  \
        }                                                                              \
    } while (false)

namespace {

constexpr const char* kCalibration =
    "323456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0";

class FakeBackend : public MotionPackageStoreBackend {
public:
    bool Read(const char* key, std::vector<uint8_t>* value) override {
        auto it = values.find(key);
        if (it == values.end()) {
            return false;
        }
        if (value != nullptr) {
            *value = it->second;
        }
        return true;
    }

    bool Write(const char* key, const std::vector<uint8_t>& value) override {
        if (fail_writes.count(key) != 0) {
            return false;
        }
        values[key] = value;
        return true;
    }

    bool Erase(const char* key) override {
        values.erase(key);
        return true;
    }

    std::map<std::string, std::vector<uint8_t>> values;
    std::set<std::string> fail_writes;
};

MotionLivePreparedProfile MotionEditorProfile() {
    return {
        "gosha-preview-v1",
        kCalibration,
        "dbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, 17, 0, 90, 1, -35.0, 35.0, 55, 125, 10.0},
            {"leg_positive_x", "right_leg", 1, 39, 0, 90, -1, -35.0, 35.0, 55, 125, 10.0},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -30.0, 30.0, 60, 120, 10.0},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -30.0, 30.0, 60, 120, 10.0},
            {"arm_positive_x", "right_hand", 5, 12, 0, 135, 1, -70.0, 45.0, 65, 180, 10.0},
        }},
        "motion_editor",
        5,
    };
}

MotionLiveRuntimeConfig RuntimeConfig() {
    MotionLiveRuntimeConfig runtime;
    runtime.board_is_gosha_v1 = true;
    runtime.no_motion_safe_profile = true;
    runtime.safe_neutral_boot_profile = true;
    runtime.safe_neutral_commanded = true;
    runtime.lower_body_attached = true;
    runtime.watchdog_tick_ready = true;
    runtime.joints[static_cast<int>(ServoSlot::kLeftLeg)] = {17, 0, 90, true};
    runtime.joints[static_cast<int>(ServoSlot::kRightLeg)] = {39, 0, 90, true};
    runtime.joints[static_cast<int>(ServoSlot::kLeftFoot)] = {18, 0, 90, true};
    runtime.joints[static_cast<int>(ServoSlot::kRightFoot)] = {38, 0, 90, true};
    runtime.joints[static_cast<int>(ServoSlot::kLeftHand)] = {-1, 0, 90, false};
    runtime.joints[static_cast<int>(ServoSlot::kRightHand)] =
        {12, 0, kRightArmHomeDegrees, false};
    return runtime;
}

std::vector<uint8_t> PayloadWithRightArm(const std::string& name, double right_arm) {
    const std::string json =
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"source_motion_id\":\"" + name + "\","
        "\"name\":\"" + name + "\","
        "\"profile_id\":\"gosha-preview-v1\","
        "\"profile_version\":1,"
        "\"calibration_id\":\"" + kCalibration + "\","
        "\"units\":\"relative_degrees\","
        "\"source_preview_only\":true,"
        "\"hardware_validated\":false,"
        "\"live_compatible\":true,"
        "\"robot_storage_implemented\":false,"
        "\"duration_ms\":8000,"
        "\"interpolation\":\"smooth\","
        "\"active_joints\":[\"arm_positive_x\",\"leg_negative_x\","
        "\"leg_positive_x\",\"foot_negative_x\",\"foot_positive_x\"],"
        "\"constraints\":["
        "{\"id\":\"arm_positive_x\",\"min\":-70,\"max\":45,\"max_speed_dps\":10},"
        "{\"id\":\"leg_negative_x\",\"min\":-35,\"max\":35,\"max_speed_dps\":10},"
        "{\"id\":\"leg_positive_x\",\"min\":-35,\"max\":35,\"max_speed_dps\":10},"
        "{\"id\":\"foot_negative_x\",\"min\":-30,\"max\":30,\"max_speed_dps\":10},"
        "{\"id\":\"foot_positive_x\",\"min\":-30,\"max\":30,\"max_speed_dps\":10}],"
        "\"keyframes\":["
        "{\"time_ms\":0,\"target\":{\"arm_positive_x\":0,\"leg_negative_x\":0,"
        "\"leg_positive_x\":0,\"foot_negative_x\":0,\"foot_positive_x\":0}},"
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":" +
        std::to_string(right_arm) +
        ",\"leg_negative_x\":-12,\"leg_positive_x\":0,"
        "\"foot_negative_x\":0,\"foot_positive_x\":8}}]}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

std::vector<uint8_t> PayloadFor(const std::string& name) {
    return PayloadWithRightArm(name, 40.0);
}

std::string Base64Encode(const uint8_t* data, size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < size ? kAlphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < size ? kAlphabet[triple & 0x3F] : '=');
    }
    return out;
}

std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

cJSON* Parse(const std::string& json) {
    return cJSON_Parse(json.c_str());
}

std::string BeginRequest(const char* package_id, const std::vector<uint8_t>& payload) {
    return std::string("{\"protocol\":\"") + kProtocol +
           "\",\"op\":\"package_upload_begin\",\"request_id\":\"begin\","
           "\"access_key\":\"secret\","
           "\"package_id\":\"" + package_id + "\","
           "\"profile_id\":\"gosha-preview-v1\","
           "\"calibration_id\":\"" + kCalibration + "\","
           "\"total_size\":" + std::to_string(payload.size()) + ","
           "\"crc32\":" + std::to_string(MotionPackageCrc32(payload.data(), payload.size())) +
           "}";
}

std::string ChunkRequest(const char* session_id, uint32_t offset,
                         const uint8_t* data, size_t size) {
    return std::string("{\"protocol\":\"") + kProtocol +
           "\",\"op\":\"package_upload_chunk\",\"request_id\":\"chunk\","
           "\"upload_session_id\":\"" + session_id + "\","
           "\"offset\":" + std::to_string(offset) + ","
           "\"data_b64\":\"" + JsonEscape(Base64Encode(data, size)) + "\"}";
}

std::string FinishRequest(const char* session_id) {
    return std::string("{\"protocol\":\"") + kProtocol +
           "\",\"op\":\"package_upload_finish\",\"request_id\":\"finish\","
           "\"upload_session_id\":\"" + session_id + "\"}";
}

std::string AbortRequest(const char* session_id) {
    return std::string("{\"protocol\":\"") + kProtocol +
           "\",\"op\":\"package_upload_abort\",\"request_id\":\"abort\","
           "\"upload_session_id\":\"" + session_id + "\"}";
}

std::string ListRequest() {
    return std::string("{\"protocol\":\"") + kProtocol +
           "\",\"op\":\"package_list\",\"request_id\":\"list\"}";
}

cJSON* Dispatch(MotionPackageProtocol* protocol, const MotionLivePreparedProfile* profile,
                const std::string& json, bool access_key_valid = true,
                int owner_id = 7) {
    cJSON* request = Parse(json);
    if (request == nullptr) {
        std::cerr << "test request parse failed\n";
        std::abort();
    }
    cJSON* reply = nullptr;
    const bool handled =
        protocol->HandleMessage(request, owner_id, profile, access_key_valid, &reply);
    cJSON_Delete(request);
    if (!handled || reply == nullptr) {
        std::cerr << "test request was not handled\n";
        std::abort();
    }
    return reply;
}

cJSON* DispatchWithCore(MotionPackageProtocol* protocol,
                        const MotionLivePreparedProfile* profile,
                        MotionLiveCore* core,
                        const std::string& json,
                        bool access_key_valid = true,
                        int owner_id = 7) {
    cJSON* request = Parse(json);
    if (request == nullptr) {
        std::cerr << "test request parse failed\n";
        std::abort();
    }
    cJSON* reply = nullptr;
    const bool handled =
        protocol->HandleMessage(request, owner_id, profile, core,
                                access_key_valid, &reply);
    cJSON_Delete(request);
    if (!handled || reply == nullptr) {
        std::cerr << "test request was not handled\n";
        std::abort();
    }
    return reply;
}

bool StringFieldEquals(cJSON* object, const char* key, const char* expected) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsString(item) && item->valuestring != nullptr &&
           std::string(item->valuestring) == expected;
}

bool NumberFieldEquals(cJSON* object, const char* key, uint32_t expected) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) &&
           static_cast<uint32_t>(item->valuedouble) == expected;
}

bool NumberFieldNear(cJSON* object, const char* key, double expected) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsNumber(item) && std::abs(item->valuedouble - expected) <= 0.0001;
}

bool BoolFieldEquals(cJSON* object, const char* key, bool expected) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    return cJSON_IsBool(item) && cJSON_IsTrue(item) == expected;
}

}  // namespace

int main() {
    MotionLivePreparedProfile profile = MotionEditorProfile();

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackagePlayer player;
        MotionPackageRunner runner;
        int session_count = 0;
        uint64_t now_ms = 1000;
        MotionPackageProtocol protocol(&manager, &player, &runner, [&session_count]() {
            ++session_count;
            return "upload-session-" + std::to_string(session_count);
        }, [&now_ms]() { return now_ms; });
        const std::vector<uint8_t> payload = PayloadFor("first");

        cJSON* reply = Dispatch(&protocol, &profile, BeginRequest("motion-001", payload), false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "access_denied"));
        CHECK(!manager.upload_active());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, BeginRequest("motion-001", payload));
        CHECK(StringFieldEquals(reply, "op", "package_status"));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "upload-session-1"));
        CHECK(NumberFieldEquals(reply, "expected_size", payload.size()));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("wrong-session", 0, payload.data(), 12));
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "upload_session_mismatch"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("upload-session-1", 0, payload.data(), 12),
                         true, 8);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "upload_owner_mismatch"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("upload-session-1", 0, payload.data(), 11));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        CHECK(NumberFieldEquals(reply, "received_size", 11));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_upload_chunk\","
                             "\"upload_session_id\":\"upload-session-1\","
                             "\"offset\":11,\"data_b64\":\"bad!?\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "chunk_base64"));
        CHECK(manager.upload_active());
        cJSON_Delete(reply);

        const size_t max_b64 = ((kMotionPackageUploadMaxChunkBytes + 2) / 3) * 4;
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_upload_chunk\","
                             "\"upload_session_id\":\"upload-session-1\","
                             "\"offset\":11,\"data_b64\":\"" +
                             std::string(max_b64 + 4, 'A') + "\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "chunk_base64"));
        CHECK(manager.upload_active());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("upload-session-1", 11, payload.data() + 11,
                                      payload.size() - 11));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        CHECK(NumberFieldEquals(reply, "received_size", payload.size()));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, FinishRequest("upload-session-1"), false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "access_denied"));
        CHECK(manager.upload_active());
        CHECK(protocol.upload_session_id() == "upload-session-1");
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, FinishRequest("upload-session-1"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-001"));
        CHECK(!manager.upload_active());
        CHECK(protocol.upload_session_id().empty());
        CHECK(backend.values[kMotionPackageStoreActiveKey] == std::vector<uint8_t>{'B'});
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, ListRequest(), false);
        CHECK(StringFieldEquals(reply, "status", "listed"));
        CHECK(NumberFieldEquals(reply, "count", 1));
        cJSON* packages = cJSON_GetObjectItem(reply, "packages");
        CHECK(cJSON_IsArray(packages));
        CHECK(cJSON_GetArraySize(packages) == 1);
        cJSON* active = cJSON_GetArrayItem(packages, 0);
        CHECK(cJSON_IsObject(active));
        CHECK(StringFieldEquals(active, "package_id", "motion-001"));
        CHECK(StringFieldEquals(active, "name", "first"));
        CHECK(StringFieldEquals(active, "profile_id", "gosha-preview-v1"));
        CHECK(StringFieldEquals(active, "calibration_id", kCalibration));
        CHECK(NumberFieldEquals(active, "payload_size", payload.size()));
        CHECK(NumberFieldEquals(active, "duration_ms", 8000));
        CHECK(StringFieldEquals(active, "interpolation", "smooth"));
        CHECK(NumberFieldEquals(active, "keyframe_count", 2));
        CHECK(NumberFieldEquals(active, "active_joint_count", 5));
        CHECK(BoolFieldEquals(active, "active", true));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_load\",\"request_id\":\"load\"}");
        CHECK(StringFieldEquals(reply, "status", "loaded"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-001"));
        CHECK(StringFieldEquals(reply, "name", "first"));
        CHECK(NumberFieldEquals(reply, "payload_size", payload.size()));
        CHECK(NumberFieldEquals(reply, "duration_ms", 8000));
        CHECK(StringFieldEquals(reply, "interpolation", "smooth"));
        CHECK(NumberFieldEquals(reply, "keyframe_count", 2));
        CHECK(NumberFieldEquals(reply, "active_joint_count", 5));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_sample\","
                             "\"request_id\":\"sample-before\","
                             "\"elapsed_ms\":2000}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_not_loaded"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"run-before\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_not_loaded"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_prepare\","
                             "\"request_id\":\"prepare\"}");
        CHECK(StringFieldEquals(reply, "status", "prepared"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-001"));
        CHECK(NumberFieldEquals(reply, "duration_ms", 8000));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_sample\","
                             "\"request_id\":\"sample\","
                             "\"elapsed_ms\":2000}");
        CHECK(StringFieldEquals(reply, "status", "sampled"));
        cJSON* target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 6.25));
        CHECK(cJSON_GetObjectItem(target, "arm_negative_x") == nullptr);
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"run-denied\","
                             "\"access_key\":\"secret\"}",
                         false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "access_denied"));
        CHECK(!runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"run-start\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "upload-session-2"));
        CHECK(BoolFieldEquals(reply, "hardware_apply", false));
        CHECK(NumberFieldEquals(reply, "elapsed_ms", 0));
        target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 0.0));
        CHECK(runner.running());
        cJSON_Delete(reply);

        now_ms = 3000;
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_status\","
                             "\"request_id\":\"run-status\","
                             "\"run_session_id\":\"upload-session-2\"}",
                         true, 8);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_run_not_owner"));
        CHECK(runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_status\","
                             "\"request_id\":\"run-status\","
                             "\"run_session_id\":\"upload-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "run_running"));
        CHECK(BoolFieldEquals(reply, "hardware_apply", false));
        CHECK(NumberFieldEquals(reply, "elapsed_ms", 2000));
        target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 6.25));
        CHECK(runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_stop\","
                             "\"request_id\":\"run-stop\","
                             "\"run_session_id\":\"upload-session-2\"}",
                         true, 8);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_run_not_owner"));
        CHECK(runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_stop\","
                             "\"request_id\":\"run-stop\","
                             "\"run_session_id\":\"upload-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "run_stopped"));
        CHECK(BoolFieldEquals(reply, "hardware_apply", false));
        CHECK(!runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_status\","
                             "\"request_id\":\"run-after-stop\","
                             "\"run_session_id\":\"upload-session-2\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_run_not_active"));
        cJSON_Delete(reply);

        now_ms = 4000;
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"run-start-2\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "upload-session-3"));
        CHECK(runner.running());
        cJSON_Delete(reply);

        protocol.OnTransportClosed(99);
        CHECK(runner.running());
        protocol.OnTransportClosed(7);
        CHECK(!runner.running());

        now_ms = 5000;
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"run-start-3\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "upload-session-4"));
        cJSON_Delete(reply);

        now_ms = 14000;
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_status\","
                             "\"request_id\":\"run-finish\","
                             "\"run_session_id\":\"upload-session-4\"}");
        CHECK(StringFieldEquals(reply, "status", "run_finished"));
        CHECK(NumberFieldEquals(reply, "elapsed_ms", 8000));
        target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 40.0));
        CHECK(!runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "deleted"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, ListRequest(), false);
        CHECK(StringFieldEquals(reply, "status", "listed"));
        CHECK(NumberFieldEquals(reply, "count", 0));
        packages = cJSON_GetObjectItem(reply, "packages");
        CHECK(cJSON_IsArray(packages));
        CHECK(cJSON_GetArraySize(packages) == 0);
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_sample\","
                             "\"request_id\":\"sample-after\","
                             "\"elapsed_ms\":2000}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_not_loaded"));
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackagePlayer player;
        MotionPackageRunner runner;
        MotionPackageHardwareRunner hardware_runner;
        int session_count = 0;
        uint64_t now_ms = 1000;
        MotionPackageProtocol protocol(
            &manager, &player, &runner, &hardware_runner,
            [&session_count]() {
                ++session_count;
                return "hardware-session-" + std::to_string(session_count);
            },
            [&now_ms]() { return now_ms; });
        MotionLiveCore core;
        core.SetLocalOptInEnabled(true);
        core.SetPreparedProfile(&profile);
        core.SetRuntimeConfig(RuntimeConfig());
        std::vector<std::array<int, kPoseJointCount>> applied;
        core.SetHardwareApplier([&applied](const std::array<int, kPoseJointCount>& degrees) {
            applied.push_back(degrees);
            return true;
        });
        core.SetRightArmInitializer([](int home_degrees) {
            return home_degrees == kRightArmHomeDegrees;
        });
        CHECK(core.InitializeRightArm(7, kCalibration, true).ok);

        const std::vector<uint8_t> payload = PayloadFor("hardware-run");
        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-hardware", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "hardware-session-1"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("hardware-session-1", 0,
                                      payload.data(), payload.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("hardware-session-1"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_prepare\","
                             "\"request_id\":\"prepare-hardware\"}");
        CHECK(StringFieldEquals(reply, "status", "prepared"));
        cJSON_Delete(reply);

        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_start\","
                                     "\"request_id\":\"hardware-start\","
                                     "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "hardware-session-2"));
        CHECK(StringFieldEquals(reply, "live_session_id", "hardware-session-3"));
        CHECK(BoolFieldEquals(reply, "live_armed", false));
        CHECK(BoolFieldEquals(reply, "hardware_apply", false));
        CHECK(hardware_runner.running());
        CHECK(applied.empty());
        cJSON_Delete(reply);

        now_ms = 1300;
        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_status\","
                                     "\"request_id\":\"hardware-status\","
                                     "\"run_session_id\":\"hardware-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_running"));
        CHECK(BoolFieldEquals(reply, "live_armed", true));
        CHECK(BoolFieldEquals(reply, "hardware_apply", false));
        CHECK(NumberFieldEquals(reply, "live_seq", 0));
        cJSON* target = cJSON_GetObjectItem(reply, "target");
        CHECK(target == nullptr);
        CHECK(applied.empty());
        cJSON_Delete(reply);

        now_ms = 1600;
        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_status\","
                                     "\"request_id\":\"hardware-status-2\","
                                     "\"run_session_id\":\"hardware-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_running"));
        CHECK(BoolFieldEquals(reply, "live_armed", true));
        CHECK(BoolFieldEquals(reply, "hardware_apply", true));
        CHECK(NumberFieldEquals(reply, "live_seq", 1));
        target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 0.64125));
        CHECK(applied.empty());
        cJSON_Delete(reply);

        now_ms = 1900;
        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_status\","
                                     "\"request_id\":\"hardware-status-3\","
                                     "\"run_session_id\":\"hardware-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_running"));
        CHECK(BoolFieldEquals(reply, "live_armed", true));
        CHECK(BoolFieldEquals(reply, "hardware_apply", true));
        CHECK(NumberFieldEquals(reply, "live_seq", 2));
        target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 1.40484375));
        CHECK(!applied.empty());
        CHECK(applied.back()[static_cast<int>(ServoSlot::kRightHand)] == 136);
        cJSON_Delete(reply);

        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_stop\","
                                     "\"request_id\":\"hardware-stop\","
                                     "\"run_session_id\":\"hardware-session-2\"}",
                                 true, 8);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_hardware_run_not_owner"));
        CHECK(hardware_runner.running());
        cJSON_Delete(reply);

        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_stop\","
                                     "\"request_id\":\"hardware-stop\","
                                     "\"run_session_id\":\"hardware-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_stopped"));
        CHECK(BoolFieldEquals(reply, "live_armed", true));
        CHECK(BoolFieldEquals(reply, "hardware_apply", true));
        CHECK(NumberFieldEquals(reply, "live_seq", 3));
        CHECK(!hardware_runner.running());
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackagePlayer player;
        MotionPackageRunner runner;
        MotionPackageHardwareRunner hardware_runner;
        int session_count = 0;
        uint64_t now_ms = 1000;
        MotionPackageProtocol protocol(
            &manager, &player, &runner, &hardware_runner,
            [&session_count]() {
                ++session_count;
                return "hardware-finish-session-" + std::to_string(session_count);
            },
            [&now_ms]() { return now_ms; });
        MotionLiveCore core;
        core.SetLocalOptInEnabled(true);
        core.SetPreparedProfile(&profile);
        core.SetRuntimeConfig(RuntimeConfig());
        std::vector<std::array<int, kPoseJointCount>> applied;
        core.SetHardwareApplier([&applied](const std::array<int, kPoseJointCount>& degrees) {
            applied.push_back(degrees);
            return true;
        });
        core.SetRightArmInitializer([](int home_degrees) {
            return home_degrees == kRightArmHomeDegrees;
        });
        CHECK(core.InitializeRightArm(7, kCalibration, true).ok);

        const std::vector<uint8_t> payload = PayloadFor("hardware-finish");
        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-hardware-finish", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "hardware-finish-session-1"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("hardware-finish-session-1", 0,
                                      payload.data(), payload.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         FinishRequest("hardware-finish-session-1"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_prepare\","
                             "\"request_id\":\"prepare-hardware-finish\"}");
        CHECK(StringFieldEquals(reply, "status", "prepared"));
        cJSON_Delete(reply);
        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_start\","
                                     "\"request_id\":\"hardware-finish-start\","
                                     "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "hardware-finish-session-2"));
        cJSON_Delete(reply);
        for (now_ms = 1300; now_ms <= 9000; now_ms += 50) {
            protocol.TickHardwareRun(&core, now_ms);
            const auto tick_result = core.Tick(now_ms);
            CHECK(!tick_result.stopped);
        }
        reply = DispatchWithCore(&protocol, &profile, &core,
                                 std::string("{\"protocol\":\"") + kProtocol +
                                     "\",\"op\":\"package_hardware_run_status\","
                                     "\"request_id\":\"hardware-finish-status\","
                                     "\"run_session_id\":\"hardware-finish-session-2\"}");
        CHECK(StringFieldEquals(reply, "status", "hardware_run_finished"));
        CHECK(BoolFieldEquals(reply, "live_armed", true));
        CHECK(BoolFieldEquals(reply, "hardware_apply", true));
        cJSON* target = cJSON_GetObjectItem(reply, "target");
        CHECK(cJSON_IsObject(target));
        CHECK(NumberFieldNear(target, "arm_positive_x", 40.0));
        CHECK(!hardware_runner.running());
        CHECK(!applied.empty());
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackagePlayer player;
        MotionPackageRunner runner;
        int session_count = 0;
        uint64_t now_ms = 1000;
        MotionPackageProtocol protocol(&manager, &player, &runner, [&session_count]() {
            ++session_count;
            return "delete-run-session-" + std::to_string(session_count);
        }, [&now_ms]() { return now_ms; });
        const std::vector<uint8_t> payload = PayloadFor("delete-run");

        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-delete-run", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "delete-run-session-1"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("delete-run-session-1", 0,
                                      payload.data(), payload.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("delete-run-session-1"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_prepare\","
                             "\"request_id\":\"prepare-delete-run\"}");
        CHECK(StringFieldEquals(reply, "status", "prepared"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_start\","
                             "\"request_id\":\"start-delete-run\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "run_started"));
        CHECK(StringFieldEquals(reply, "run_session_id", "delete-run-session-2"));
        CHECK(runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-during-run\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "deleted"));
        CHECK(!runner.running());
        MotionPackageLoadedRecord loaded;
        CHECK(!manager.Load(&loaded).ok);
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_run_status\","
                             "\"request_id\":\"status-after-delete\","
                             "\"run_session_id\":\"delete-run-session-2\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_run_not_active"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_sample\","
                             "\"request_id\":\"sample-after-delete\","
                             "\"elapsed_ms\":0}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_not_loaded"));
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackagePlayer player;
        MotionPackageRunner runner;
        int session_count = 0;
        uint64_t now_ms = 1000;
        MotionPackageProtocol protocol(&manager, &player, &runner, [&session_count]() {
            ++session_count;
            return "select-session-" + std::to_string(session_count);
        }, [&now_ms]() { return now_ms; });
        const std::vector<uint8_t> first = PayloadFor("first-select");
        const std::vector<uint8_t> second = PayloadFor("second-select");

        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-first", first));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "select-session-1"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("select-session-1", 0,
                                      first.data(), first.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("select-session-1"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         BeginRequest("motion-second", second));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(StringFieldEquals(reply, "upload_session_id", "select-session-2"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("select-session-2", 0,
                                      second.data(), second.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("select-session-2"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, ListRequest(), false);
        CHECK(StringFieldEquals(reply, "status", "listed"));
        CHECK(NumberFieldEquals(reply, "count", 2));
        cJSON* packages = cJSON_GetObjectItem(reply, "packages");
        CHECK(cJSON_IsArray(packages));
        cJSON* item = cJSON_GetArrayItem(packages, 0);
        CHECK(StringFieldEquals(item, "package_id", "motion-second"));
        CHECK(BoolFieldEquals(item, "active", true));
        item = cJSON_GetArrayItem(packages, 1);
        CHECK(StringFieldEquals(item, "package_id", "motion-first"));
        CHECK(BoolFieldEquals(item, "active", false));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_load\","
                             "\"request_id\":\"load-first\","
                             "\"package_id\":\"motion-first\"}");
        CHECK(StringFieldEquals(reply, "status", "loaded"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-first"));
        CHECK(StringFieldEquals(reply, "name", "first-select"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_select\","
                             "\"request_id\":\"select-denied\","
                             "\"package_id\":\"motion-first\","
                             "\"access_key\":\"secret\"}",
                         false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "access_denied"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_select\","
                             "\"request_id\":\"select-first\","
                             "\"package_id\":\"motion-first\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "selected"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-first"));
        CHECK(BoolFieldEquals(reply, "active", true));
        CHECK(!runner.running());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_load\","
                             "\"request_id\":\"load-active\"}");
        CHECK(StringFieldEquals(reply, "status", "loaded"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-first"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-bad-id\","
                             "\"package_id\":42,"
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "bad_json"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-missing\","
                             "\"package_id\":\"motion-missing\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "store_package_not_found"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-second\","
                             "\"package_id\":\"motion-second\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "deleted"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-second"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, ListRequest());
        CHECK(StringFieldEquals(reply, "status", "listed"));
        CHECK(NumberFieldEquals(reply, "count", 1));
        packages = cJSON_GetObjectItem(reply, "packages");
        CHECK(cJSON_IsArray(packages));
        item = cJSON_GetArrayItem(packages, 0);
        CHECK(StringFieldEquals(item, "package_id", "motion-first"));
        CHECK(BoolFieldEquals(item, "active", true));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-first\","
                             "\"package_id\":\"motion-first\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "deleted"));
        CHECK(StringFieldEquals(reply, "package_id", "motion-first"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, ListRequest());
        CHECK(StringFieldEquals(reply, "status", "listed"));
        CHECK(NumberFieldEquals(reply, "count", 0));
        packages = cJSON_GetObjectItem(reply, "packages");
        CHECK(cJSON_IsArray(packages));
        CHECK(cJSON_GetArraySize(packages) == 0);
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() {
            return "list-corrupt-session";
        });
        const std::vector<uint8_t> payload = PayloadFor("list-corrupt");
        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-list-corrupt", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("list-corrupt-session", 0,
                                      payload.data(), payload.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("list-corrupt-session"));
        CHECK(StringFieldEquals(reply, "status", "stored"));
        cJSON_Delete(reply);

        backend.values.erase(kMotionPackageStoreCatalogKey);
        backend.values.erase(kMotionPackageStoreActiveKey);
        reply = Dispatch(&protocol, &profile, ListRequest(), false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "store_active_missing"));
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageStore store(&backend);
        const std::vector<uint8_t> bad_json = {'{', 'n', 'o', 't'};
        const MotionPackageStoreRecord record = {
            "motion-bad-json",
            "gosha-preview-v1",
            kCalibration,
            MotionPackageCrc32(bad_json.data(), bad_json.size()),
            bad_json.data(),
            bad_json.size(),
        };
        CHECK(store.Save(record).ok);
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() {
            return "list-bad-json-session";
        });

        cJSON* reply = Dispatch(&protocol, &profile, ListRequest(), false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_json_parse"));
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_load\",\"request_id\":\"load-bad-json\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_json_parse"));
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() {
            return "delete-upload-session";
        });
        const std::vector<uint8_t> payload = PayloadFor("delete-upload");
        cJSON* reply = Dispatch(&protocol, &profile,
                                BeginRequest("motion-delete-upload", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(manager.upload_active());
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile,
                         std::string("{\"protocol\":\"") + kProtocol +
                             "\",\"op\":\"package_delete\","
                             "\"request_id\":\"delete-during-upload\","
                             "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "package_upload_active"));
        CHECK(manager.upload_active());
        CHECK(protocol.upload_session_id() == "delete-upload-session");
        cJSON_Delete(reply);

        reply = Dispatch(&protocol, &profile, AbortRequest("delete-upload-session"));
        CHECK(StringFieldEquals(reply, "status", "upload_aborted"));
        CHECK(!manager.upload_active());
        CHECK(protocol.upload_session_id().empty());
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() { return "bad-arm-session"; });
        const std::vector<uint8_t> payload = PayloadWithRightArm("bad-arm", 55.0);
        cJSON* reply = Dispatch(&protocol, &profile, BeginRequest("motion-bad", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("bad-arm-session", 0, payload.data(), payload.size()));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("bad-arm-session"));
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "target_out_of_range"));
        CHECK(!manager.upload_active());
        CHECK(protocol.upload_session_id().empty());
        MotionPackageLoadedRecord loaded;
        CHECK(!manager.Load(&loaded).ok);
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() { return "closed-session"; });
        const std::vector<uint8_t> payload = PayloadFor("closed");
        cJSON* reply = Dispatch(&protocol, &profile, BeginRequest("motion-closed", payload),
                                true, 17);
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        CHECK(manager.upload_active());
        cJSON_Delete(reply);
        protocol.OnTransportClosed(99);
        CHECK(manager.upload_active());
        protocol.OnTransportClosed(17);
        CHECK(!manager.upload_active());
        CHECK(protocol.upload_session_id().empty());
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() { return "incomplete-session"; });
        const std::vector<uint8_t> payload = PayloadFor("incomplete");
        cJSON* reply = Dispatch(&protocol, &profile, BeginRequest("motion-inc", payload));
        CHECK(StringFieldEquals(reply, "status", "upload_started"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile,
                         ChunkRequest("incomplete-session", 0, payload.data(), payload.size() - 1));
        CHECK(StringFieldEquals(reply, "status", "upload_chunk"));
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, FinishRequest("incomplete-session"));
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "upload_incomplete"));
        CHECK(manager.upload_active());
        CHECK(protocol.upload_session_id() == "incomplete-session");
        cJSON_Delete(reply);
        reply = Dispatch(&protocol, &profile, AbortRequest("incomplete-session"));
        CHECK(StringFieldEquals(reply, "status", "upload_aborted"));
        CHECK(!manager.upload_active());
        cJSON_Delete(reply);
    }

    {
        FakeBackend backend;
        MotionPackageManager manager(&backend);
        MotionPackageProtocol protocol(&manager, []() { return "unused"; });
        cJSON* reply = Dispatch(
            &protocol, &profile,
            std::string("{\"protocol\":\"") + kProtocol +
                "\",\"op\":\"package_delete\",\"request_id\":\"delete\","
                "\"access_key\":\"secret\"}",
            false);
        CHECK(StringFieldEquals(reply, "op", "error"));
        CHECK(StringFieldEquals(reply, "code", "access_denied"));
        cJSON_Delete(reply);
        reply = Dispatch(
            &protocol, &profile,
            std::string("{\"protocol\":\"") + kProtocol +
                "\",\"op\":\"package_delete\",\"request_id\":\"delete\","
                "\"access_key\":\"secret\"}");
        CHECK(StringFieldEquals(reply, "status", "deleted"));
        cJSON_Delete(reply);
    }

    std::cout << "motion_package_protocol_host_test: PASS\n";
    return 0;
}

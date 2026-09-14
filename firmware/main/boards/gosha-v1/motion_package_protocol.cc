#include "motion_package_protocol.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "motion_package_json.h"

namespace gosha::motion_live {

namespace {

bool JsonStringEquals(cJSON* item, const char* value) {
    return item != nullptr && cJSON_IsString(item) && item->valuestring != nullptr &&
           std::strcmp(item->valuestring, value) == 0;
}

void CopyRequestField(cJSON* reply, cJSON* request, const char* name) {
    cJSON* value = cJSON_GetObjectItem(request, name);
    if (value == nullptr) {
        return;
    }
    cJSON* copy = cJSON_Duplicate(value, 1);
    if (copy != nullptr) {
        cJSON_AddItemToObject(reply, name, copy);
    }
}

bool ReadString(cJSON* object, const char* key, const char** out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    *out = item->valuestring;
    return true;
}

bool ReadUint32(cJSON* object, const char* key, uint32_t* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 ||
        item->valuedouble > static_cast<double>(UINT32_MAX) ||
        std::floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    *out = static_cast<uint32_t>(item->valuedouble);
    return true;
}

int Base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

bool DecodeBase64(const char* text, std::vector<uint8_t>* out) {
    if (text == nullptr || out == nullptr) {
        return false;
    }
    const size_t length = std::strlen(text);
    constexpr size_t kMaxBase64ChunkBytes =
        ((kMotionPackageUploadMaxChunkBytes + 2) / 3) * 4;
    if (length == 0 || length % 4 != 0 || length > kMaxBase64ChunkBytes) {
        return false;
    }
    out->clear();
    out->reserve((length / 4) * 3);
    for (size_t offset = 0; offset < length; offset += 4) {
        const char a = text[offset];
        const char b = text[offset + 1];
        const char c = text[offset + 2];
        const char d = text[offset + 3];
        const bool third_padding = c == '=';
        const bool fourth_padding = d == '=';
        if (a == '=' || b == '=' || (third_padding && !fourth_padding) ||
            ((third_padding || fourth_padding) && offset + 4 != length)) {
            return false;
        }
        const int va = Base64Value(a);
        const int vb = Base64Value(b);
        const int vc = third_padding ? 0 : Base64Value(c);
        const int vd = fourth_padding ? 0 : Base64Value(d);
        if (va < 0 || vb < 0 || vc < 0 || vd < 0) {
            return false;
        }
        const uint32_t triple =
            (static_cast<uint32_t>(va) << 18) |
            (static_cast<uint32_t>(vb) << 12) |
            (static_cast<uint32_t>(vc) << 6) |
            static_cast<uint32_t>(vd);
        out->push_back(static_cast<uint8_t>((triple >> 16) & 0xFFu));
        if (!third_padding) {
            out->push_back(static_cast<uint8_t>((triple >> 8) & 0xFFu));
        }
        if (!fourth_padding) {
            out->push_back(static_cast<uint8_t>(triple & 0xFFu));
        }
    }
    return true;
}

void AddManagerResult(cJSON* reply, const MotionPackageManagerResult& result) {
    if (result.expected_offset != 0) {
        cJSON_AddNumberToObject(reply, "expected_offset", result.expected_offset);
    }
    if (result.value != 0 || result.limit != 0) {
        cJSON_AddNumberToObject(reply, "value", result.value);
        cJSON_AddNumberToObject(reply, "limit", result.limit);
    }
}

bool IsPackageOp(const char* op) {
    return std::strcmp(op, "package_upload_begin") == 0 ||
           std::strcmp(op, "package_upload_chunk") == 0 ||
           std::strcmp(op, "package_upload_finish") == 0 ||
           std::strcmp(op, "package_upload_abort") == 0 ||
           std::strcmp(op, "package_list") == 0 ||
           std::strcmp(op, "package_load") == 0 ||
           std::strcmp(op, "package_select") == 0 ||
           std::strcmp(op, "package_prepare") == 0 ||
           std::strcmp(op, "package_sample") == 0 ||
           std::strcmp(op, "package_run_start") == 0 ||
           std::strcmp(op, "package_run_status") == 0 ||
           std::strcmp(op, "package_run_stop") == 0 ||
           std::strcmp(op, "package_hardware_run_start") == 0 ||
           std::strcmp(op, "package_hardware_run_status") == 0 ||
           std::strcmp(op, "package_hardware_run_stop") == 0 ||
           std::strcmp(op, "package_delete") == 0;
}

bool AddSample(cJSON* reply, const MotionPackageSample& sample) {
    if (reply == nullptr) {
        return false;
    }
    cJSON_AddStringToObject(reply, "package_id", sample.package_id);
    cJSON_AddNumberToObject(reply, "elapsed_ms", sample.elapsed_ms);
    cJSON_AddBoolToObject(reply, "finished", sample.finished);
    cJSON* target = cJSON_CreateObject();
    if (target == nullptr) {
        return false;
    }
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (sample.target.present[joint_index]) {
            cJSON_AddNumberToObject(
                target, kJointSpecs[joint_index].id,
                sample.target.relative_degrees[joint_index]);
        }
    }
    cJSON_AddItemToObject(reply, "target", target);
    return true;
}

MotionPackageJsonParseResult ParseLoadedPackageMetadata(
    const MotionPackageLoadedRecord& record, MotionPackageOwnedDraft* package) {
    return ParseMotionPackageDraftJson(record.payload.data(), record.payload.size(),
                                       package);
}

void AddLoadedPackageMetadata(cJSON* item, const MotionPackageLoadedRecord& record,
                              const MotionPackageOwnedDraft& package) {
    cJSON_AddStringToObject(item, "package_id", record.package_id.c_str());
    cJSON_AddStringToObject(
        item, "name",
        package.name.empty() ? record.package_id.c_str() : package.name.c_str());
    cJSON_AddStringToObject(item, "profile_id", record.profile_id.c_str());
    cJSON_AddStringToObject(item, "calibration_id",
                            record.calibration_id.c_str());
    cJSON_AddNumberToObject(item, "payload_size", record.payload.size());
    cJSON_AddNumberToObject(item, "crc32", record.payload_crc32);
    cJSON_AddNumberToObject(item, "duration_ms",
                            package.draft.duration_ms);
    cJSON_AddStringToObject(item, "interpolation",
                            package.draft.interpolation);
    cJSON_AddNumberToObject(item, "keyframe_count",
                            package.draft.keyframe_count);
    cJSON_AddNumberToObject(item, "active_joint_count",
                            package.draft.active_joint_count);
}

void AddLiveResult(cJSON* reply, const MotionLiveResult& result) {
    cJSON_AddStringToObject(reply, "live_session_id", result.session_id.c_str());
    cJSON_AddNumberToObject(reply, "live_seq", result.seq);
    cJSON_AddStringToObject(reply, "live_code", result.code);
}

bool AddHardwareRunResult(cJSON* reply,
                          const MotionPackageHardwareRunnerResult& result) {
    cJSON_AddStringToObject(reply, "run_session_id",
                            result.run_session_id.c_str());
    cJSON_AddStringToObject(reply, "live_session_id",
                            result.live_session_id.c_str());
    cJSON_AddBoolToObject(reply, "live_armed", result.live_armed);
    cJSON_AddBoolToObject(reply, "hardware_apply", result.hardware_apply);
    AddLiveResult(reply, result.live_result);
    if (result.sample.package_id[0] != '\0') {
        return AddSample(reply, result.sample);
    }
    return true;
}

}  // namespace

cJSON* MotionPackageProtocol::MakeError(cJSON* request, const char* code,
                                        const char* message) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return nullptr;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "error");
    cJSON_AddStringToObject(reply, "code", code);
    cJSON_AddStringToObject(reply, "message", message);
    CopyRequestField(reply, request, "request_id");
    CopyRequestField(reply, request, "upload_session_id");
    CopyRequestField(reply, request, "run_session_id");
    return reply;
}

cJSON* MotionPackageProtocol::MakeStatus(cJSON* request,
                                         const char* status) const {
    cJSON* reply = cJSON_CreateObject();
    if (reply == nullptr) {
        return nullptr;
    }
    cJSON_AddStringToObject(reply, "protocol", kProtocol);
    cJSON_AddStringToObject(reply, "op", "package_status");
    cJSON_AddStringToObject(reply, "status", status);
    CopyRequestField(reply, request, "request_id");
    return reply;
}

bool MotionPackageProtocol::RequireUploadSession(cJSON* request, int owner_id,
                                                 cJSON** reply) const {
    if (manager_ == nullptr || !manager_->upload_active() ||
        upload_session_id_.empty()) {
        if (reply != nullptr) {
            *reply = MakeError(request, "upload_not_started",
                               "Motion package upload is not active");
        }
        return false;
    }
    if (owner_id != upload_owner_id_) {
        if (reply != nullptr) {
            *reply = MakeError(request, "upload_owner_mismatch",
                               "Motion package upload belongs to another transport");
        }
        return false;
    }
    const char* upload_session_id = nullptr;
    if (!ReadString(request, "upload_session_id", &upload_session_id) ||
        upload_session_id_ != upload_session_id) {
        if (reply != nullptr) {
            *reply = MakeError(request, "upload_session_mismatch",
                               "Motion package upload session does not match");
        }
        return false;
    }
    return true;
}

std::string MotionPackageProtocol::GenerateUploadSessionId() const {
    if (session_id_generator_) {
        return session_id_generator_();
    }
    return "";
}

std::string MotionPackageProtocol::GenerateRunSessionId() const {
    if (session_id_generator_) {
        return session_id_generator_();
    }
    return "";
}

uint64_t MotionPackageProtocol::NowMs() const {
    if (clock_) {
        return clock_();
    }
    return 0;
}

bool MotionPackageProtocol::HandleHardwareRunStart(
    cJSON* request,
    int owner_id,
    const MotionLivePreparedProfile* profile,
    MotionLiveCore* core,
    bool access_key_valid,
    cJSON** reply) {
    if (!access_key_valid) {
        *reply = MakeError(request, "access_denied",
                           "Motion package hardware run requires the Live access key");
        return true;
    }
    if (profile == nullptr || core == nullptr) {
        *reply = MakeError(request, "profile_unavailable",
                           "Motion package hardware run requires a prepared Live profile and core");
        return true;
    }
    if (player_ == nullptr || hardware_runner_ == nullptr) {
        *reply = MakeError(request, "package_hardware_runner_missing",
                           "Motion package hardware runner is unavailable");
        return true;
    }
    if (!player_->loaded()) {
        *reply = MakeError(request, "package_not_loaded",
                           "Motion package hardware run requires a prepared package");
        return true;
    }
    if (hardware_runner_->running()) {
        *reply = MakeError(request, "package_hardware_run_busy",
                           "Another hardware package run is already active");
        return true;
    }
    double speed_dps = kMotionEditorMaxServoRateDps;
    cJSON* speed = cJSON_GetObjectItem(request, "speed_dps");
    if (speed != nullptr) {
        if (!cJSON_IsNumber(speed) || !std::isfinite(speed->valuedouble)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package hardware run speed_dps must be numeric");
            return true;
        }
        speed_dps = speed->valuedouble;
    }
    const std::string run_session_id = GenerateRunSessionId();
    const std::string live_session_id = GenerateRunSessionId();
    if (run_session_id.empty() || live_session_id.empty()) {
        *reply = MakeError(request, "package_hardware_run_session_unavailable",
                           "Motion package hardware run sessions could not be created");
        return true;
    }
    const MotionPackageHardwareRunnerResult& run_result =
        hardware_runner_->Start(*player_, core, owner_id, run_session_id,
                                live_session_id, profile->calibration_id,
                                access_key_valid, speed_dps, NowMs());
    if (!run_result.ok) {
        *reply = MakeError(request, run_result.code,
                           "Motion package hardware run start was rejected");
        return true;
    }
    *reply = MakeStatus(request, "hardware_run_started");
    cJSON_AddStringToObject(*reply, "run_session_id",
                            run_result.run_session_id.c_str());
    cJSON_AddStringToObject(*reply, "live_session_id",
                            run_result.live_session_id.c_str());
    cJSON_AddBoolToObject(*reply, "live_armed", run_result.live_armed);
    cJSON_AddBoolToObject(*reply, "hardware_apply", run_result.hardware_apply);
    return true;
}

bool MotionPackageProtocol::HandleHardwareRunStatus(cJSON* request,
                                                    int owner_id,
                                                    MotionLiveCore* core,
                                                    cJSON** reply) {
    if (player_ == nullptr || hardware_runner_ == nullptr) {
        *reply = MakeError(request, "package_hardware_runner_missing",
                           "Motion package hardware runner is unavailable");
        return true;
    }
    const char* run_session_id = nullptr;
    if (!ReadString(request, "run_session_id", &run_session_id)) {
        *reply = MakeError(request, "bad_json",
                           "Motion package hardware run status requires run_session_id");
        return true;
    }
    const MotionPackageHardwareRunnerResult& run_result =
        hardware_runner_->Tick(*player_, core, owner_id, run_session_id, NowMs());
    if (!run_result.ok) {
        *reply = MakeError(request, run_result.code,
                           "Motion package hardware run status was rejected");
        return true;
    }
    *reply = MakeStatus(request,
                        run_result.stopped ? "hardware_run_finished"
                                           : "hardware_run_running");
    if (!AddHardwareRunResult(*reply, run_result)) {
        cJSON_Delete(*reply);
        *reply = nullptr;
    }
    return true;
}

bool MotionPackageProtocol::HandleHardwareRunStop(cJSON* request,
                                                  int owner_id,
                                                  MotionLiveCore* core,
                                                  cJSON** reply) {
    if (hardware_runner_ == nullptr) {
        *reply = MakeError(request, "package_hardware_runner_missing",
                           "Motion package hardware runner is unavailable");
        return true;
    }
    const char* run_session_id = nullptr;
    if (!ReadString(request, "run_session_id", &run_session_id)) {
        *reply = MakeError(request, "bad_json",
                           "Motion package hardware run stop requires run_session_id");
        return true;
    }
    const MotionPackageHardwareRunnerResult& run_result =
        hardware_runner_->Stop(core, owner_id, run_session_id);
    if (!run_result.ok) {
        *reply = MakeError(request, run_result.code,
                           "Motion package hardware run stop was rejected");
        return true;
    }
    *reply = MakeStatus(request, "hardware_run_stopped");
    if (!AddHardwareRunResult(*reply, run_result)) {
        cJSON_Delete(*reply);
        *reply = nullptr;
    }
    return true;
}

void MotionPackageProtocol::TickHardwareRun(MotionLiveCore* core, uint64_t now_ms) {
    if (player_ == nullptr || hardware_runner_ == nullptr ||
        !hardware_runner_->running()) {
        return;
    }
    hardware_runner_->TickActive(*player_, core, now_ms);
}

bool MotionPackageProtocol::HandleMessage(cJSON* request,
                                          int owner_id,
                                          const MotionLivePreparedProfile* profile,
                                          MotionLiveCore* core,
                                          bool access_key_valid,
                                          cJSON** reply) {
    if (reply == nullptr) {
        return false;
    }
    *reply = nullptr;
    if (!cJSON_IsObject(request) ||
        !JsonStringEquals(cJSON_GetObjectItem(request, "protocol"), kProtocol)) {
        return false;
    }
    cJSON* op_item = cJSON_GetObjectItem(request, "op");
    if (!cJSON_IsString(op_item) || op_item->valuestring == nullptr ||
        !IsPackageOp(op_item->valuestring)) {
        return false;
    }
    if (manager_ == nullptr) {
        *reply = MakeError(request, "package_manager_missing",
                           "Motion package manager is unavailable");
        return true;
    }

    const char* op = op_item->valuestring;
    if (std::strcmp(op, "package_upload_begin") == 0) {
        if (!access_key_valid) {
            *reply = MakeError(request, "access_denied",
                               "Motion package upload requires the Live access key");
            return true;
        }
        if (profile == nullptr) {
            *reply = MakeError(request, "profile_unavailable",
                               "Motion package upload requires a prepared Live profile");
            return true;
        }
        const char* package_id = nullptr;
        const char* profile_id = nullptr;
        const char* calibration_id = nullptr;
        uint32_t total_size = 0;
        uint32_t crc32 = 0;
        if (!ReadString(request, "package_id", &package_id) ||
            !ReadString(request, "profile_id", &profile_id) ||
            !ReadString(request, "calibration_id", &calibration_id) ||
            !ReadUint32(request, "total_size", &total_size) ||
            !ReadUint32(request, "crc32", &crc32)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package upload begin requires package_id, profile_id, calibration_id, total_size and crc32");
            return true;
        }
        MotionPackageUploadBegin begin = {
            package_id,
            profile_id,
            calibration_id,
            total_size,
            crc32,
        };
        const MotionPackageManagerResult result = manager_->BeginUpload(*profile, begin);
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package upload begin was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        upload_session_id_ = GenerateUploadSessionId();
        if (upload_session_id_.empty()) {
            manager_->AbortUpload();
            upload_owner_id_ = 0;
            *reply = MakeError(request, "upload_session_unavailable",
                               "Motion package upload session could not be created");
            return true;
        }
        upload_owner_id_ = owner_id;
        *reply = MakeStatus(request, "upload_started");
        cJSON_AddStringToObject(*reply, "upload_session_id",
                                upload_session_id_.c_str());
        cJSON_AddStringToObject(*reply, "package_id",
                                manager_->upload_package_id().c_str());
        cJSON_AddNumberToObject(*reply, "expected_size",
                                manager_->upload_expected_size());
        cJSON_AddNumberToObject(*reply, "received_size",
                                manager_->upload_received_size());
        return true;
    }

    if (std::strcmp(op, "package_upload_chunk") == 0) {
        if (!RequireUploadSession(request, owner_id, reply)) {
            return true;
        }
        uint32_t offset = 0;
        const char* data_b64 = nullptr;
        if (!ReadUint32(request, "offset", &offset) ||
            !ReadString(request, "data_b64", &data_b64)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package upload chunk requires offset and data_b64");
            return true;
        }
        std::vector<uint8_t> decoded;
        if (!DecodeBase64(data_b64, &decoded)) {
            *reply = MakeError(request, "chunk_base64",
                               "Motion package upload chunk is not valid base64");
            return true;
        }
        const MotionPackageUploadChunk chunk = {
            offset,
            decoded.data(),
            decoded.size(),
        };
        const MotionPackageManagerResult result = manager_->AppendUpload(chunk);
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package upload chunk was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        *reply = MakeStatus(request, "upload_chunk");
        cJSON_AddStringToObject(*reply, "upload_session_id",
                                upload_session_id_.c_str());
        cJSON_AddStringToObject(*reply, "package_id",
                                manager_->upload_package_id().c_str());
        cJSON_AddNumberToObject(*reply, "received_size",
                                manager_->upload_received_size());
        cJSON_AddNumberToObject(*reply, "expected_size",
                                manager_->upload_expected_size());
        return true;
    }

    if (std::strcmp(op, "package_upload_finish") == 0) {
        if (!RequireUploadSession(request, owner_id, reply)) {
            return true;
        }
        if (!access_key_valid) {
            *reply = MakeError(request, "access_denied",
                               "Motion package upload finish requires the Live access key");
            return true;
        }
        const std::string package_id = manager_->upload_package_id();
        const MotionPackageManagerResult result = manager_->FinishUpload();
        if (!manager_->upload_active()) {
            upload_session_id_.clear();
            upload_owner_id_ = 0;
        }
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package upload finish was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        if (player_ != nullptr) {
            player_->Clear();
        }
        if (runner_ != nullptr) {
            runner_->Clear();
        }
        if (hardware_runner_ != nullptr) {
            hardware_runner_->Clear();
        }
        *reply = MakeStatus(request, "stored");
        cJSON_AddStringToObject(*reply, "package_id", package_id.c_str());
        return true;
    }

    if (std::strcmp(op, "package_upload_abort") == 0) {
        if (!RequireUploadSession(request, owner_id, reply)) {
            return true;
        }
        const std::string package_id = manager_->upload_package_id();
        manager_->AbortUpload();
        upload_session_id_.clear();
        upload_owner_id_ = 0;
        *reply = MakeStatus(request, "upload_aborted");
        cJSON_AddStringToObject(*reply, "package_id", package_id.c_str());
        return true;
    }

    if (std::strcmp(op, "package_list") == 0) {
        std::vector<MotionPackageStoreEntry> entries;
        const MotionPackageManagerResult result = manager_->List(&entries);
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package list could not read storage");
            AddManagerResult(*reply, result);
            return true;
        }
        *reply = MakeStatus(request, "listed");
        cJSON* packages = cJSON_CreateArray();
        if (packages == nullptr) {
            cJSON_Delete(*reply);
            *reply = nullptr;
            return true;
        }
        for (const MotionPackageStoreEntry& entry : entries) {
            MotionPackageOwnedDraft package;
            const MotionPackageJsonParseResult parse_result =
                ParseLoadedPackageMetadata(entry.record, &package);
            if (!parse_result.ok) {
                cJSON_Delete(packages);
                cJSON_Delete(*reply);
                *reply = MakeError(request, parse_result.code,
                                   "Motion package list could not parse active payload");
                return true;
            }
            cJSON* item = cJSON_CreateObject();
            if (item == nullptr) {
                cJSON_Delete(packages);
                cJSON_Delete(*reply);
                *reply = nullptr;
                return true;
            }
            AddLoadedPackageMetadata(item, entry.record, package);
            cJSON_AddBoolToObject(item, "active", entry.active);
            cJSON_AddItemToArray(packages, item);
        }
        cJSON_AddNumberToObject(*reply, "count", entries.size());
        cJSON_AddItemToObject(*reply, "packages", packages);
        return true;
    }

    if (std::strcmp(op, "package_load") == 0) {
        MotionPackageLoadedRecord record;
        const char* package_id = nullptr;
        cJSON* package_id_item = cJSON_GetObjectItem(request, "package_id");
        if (package_id_item != nullptr &&
            (!cJSON_IsString(package_id_item) ||
             package_id_item->valuestring == nullptr)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package load package_id must be a string");
            return true;
        }
        package_id = package_id_item != nullptr ? package_id_item->valuestring
                                                : nullptr;
        const MotionPackageManagerResult result =
            package_id != nullptr ? manager_->LoadById(package_id, &record)
                                  : manager_->Load(&record);
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package load was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        MotionPackageOwnedDraft package;
        const MotionPackageJsonParseResult parse_result =
            ParseLoadedPackageMetadata(record, &package);
        if (!parse_result.ok) {
            *reply = MakeError(request, parse_result.code,
                               "Motion package load could not parse active payload");
            return true;
        }
        *reply = MakeStatus(request, "loaded");
        AddLoadedPackageMetadata(*reply, record, package);
        return true;
    }

    if (std::strcmp(op, "package_select") == 0) {
        if (!access_key_valid) {
            *reply = MakeError(request, "access_denied",
                               "Motion package select requires the Live access key");
            return true;
        }
        const char* package_id = nullptr;
        if (!ReadString(request, "package_id", &package_id)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package select requires package_id");
            return true;
        }
        const MotionPackageManagerResult result = manager_->Select(package_id);
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package select was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        if (player_ != nullptr) {
            player_->Clear();
        }
        if (runner_ != nullptr) {
            runner_->Clear();
        }
        if (hardware_runner_ != nullptr) {
            hardware_runner_->Clear();
        }
        MotionPackageLoadedRecord record;
        const MotionPackageManagerResult load_result = manager_->Load(&record);
        if (!load_result.ok) {
            *reply = MakeError(request, load_result.code,
                               "Motion package select could not read active payload");
            AddManagerResult(*reply, load_result);
            return true;
        }
        MotionPackageOwnedDraft package;
        const MotionPackageJsonParseResult parse_result =
            ParseLoadedPackageMetadata(record, &package);
        if (!parse_result.ok) {
            *reply = MakeError(request, parse_result.code,
                               "Motion package select could not parse active payload");
            return true;
        }
        *reply = MakeStatus(request, "selected");
        AddLoadedPackageMetadata(*reply, record, package);
        cJSON_AddBoolToObject(*reply, "active", true);
        return true;
    }

    if (std::strcmp(op, "package_prepare") == 0) {
        if (profile == nullptr) {
            *reply = MakeError(request, "profile_unavailable",
                               "Motion package prepare requires a prepared Live profile");
            return true;
        }
        if (player_ == nullptr) {
            *reply = MakeError(request, "package_player_missing",
                               "Motion package player is unavailable");
            return true;
        }
        MotionPackageLoadedRecord record;
        const MotionPackageManagerResult load_result = manager_->Load(&record);
        if (!load_result.ok) {
            *reply = MakeError(request, load_result.code,
                               "Motion package prepare could not load a package");
            AddManagerResult(*reply, load_result);
            return true;
        }
        const MotionPackagePlayerResult player_result =
            player_->Load(*profile, record);
        if (!player_result.ok) {
            *reply = MakeError(request, player_result.code,
                               "Motion package prepare was rejected");
            if (player_result.frame_index >= 0) {
                cJSON_AddNumberToObject(*reply, "frame_index",
                                        player_result.frame_index);
            }
            if (player_result.joint_id[0] != '\0') {
                cJSON_AddStringToObject(*reply, "joint_id",
                                        player_result.joint_id);
            }
            return true;
        }
        *reply = MakeStatus(request, "prepared");
        cJSON_AddStringToObject(*reply, "package_id",
                                player_->package_id().c_str());
        cJSON_AddNumberToObject(*reply, "duration_ms", player_->duration_ms());
        return true;
    }

    if (std::strcmp(op, "package_sample") == 0) {
        if (player_ == nullptr) {
            *reply = MakeError(request, "package_player_missing",
                               "Motion package player is unavailable");
            return true;
        }
        uint32_t elapsed_ms = 0;
        if (!ReadUint32(request, "elapsed_ms", &elapsed_ms)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package sample requires elapsed_ms");
            return true;
        }
        MotionPackageSample sample;
        const MotionPackagePlayerResult player_result =
            player_->Sample(elapsed_ms, &sample);
        if (!player_result.ok) {
            *reply = MakeError(request, player_result.code,
                               "Motion package sample was rejected");
            if (player_result.frame_index >= 0) {
                cJSON_AddNumberToObject(*reply, "frame_index",
                                        player_result.frame_index);
            }
            if (player_result.joint_id[0] != '\0') {
                cJSON_AddStringToObject(*reply, "joint_id",
                                        player_result.joint_id);
            }
            return true;
        }
        *reply = MakeStatus(request, "sampled");
        if (!AddSample(*reply, sample)) {
            cJSON_Delete(*reply);
            *reply = nullptr;
            return true;
        }
        return true;
    }

    if (std::strcmp(op, "package_run_start") == 0) {
        if (!access_key_valid) {
            *reply = MakeError(request, "access_denied",
                               "Motion package run requires the Live access key");
            return true;
        }
        if (player_ == nullptr || runner_ == nullptr) {
            *reply = MakeError(request, "package_runner_missing",
                               "Motion package runner is unavailable");
            return true;
        }
        if (!player_->loaded()) {
            *reply = MakeError(request, "package_not_loaded",
                               "Motion package run requires a prepared package");
            return true;
        }
        if (runner_->running()) {
            *reply = MakeError(request, "package_run_busy",
                               "Another motion package run is already active");
            return true;
        }
        const std::string run_session_id = GenerateRunSessionId();
        if (run_session_id.empty()) {
            *reply = MakeError(request, "package_run_session_unavailable",
                               "Motion package run session could not be created");
            return true;
        }
        const MotionPackageRunnerResult run_result =
            runner_->Start(*player_, owner_id, run_session_id, NowMs());
        if (!run_result.ok) {
            *reply = MakeError(request, run_result.code,
                               "Motion package run start was rejected");
            return true;
        }
        *reply = MakeStatus(request, "run_started");
        cJSON_AddStringToObject(*reply, "run_session_id",
                                run_result.run_session_id.c_str());
        cJSON_AddBoolToObject(*reply, "hardware_apply", false);
        if (!AddSample(*reply, run_result.sample)) {
            cJSON_Delete(*reply);
            *reply = nullptr;
        }
        return true;
    }

    if (std::strcmp(op, "package_run_status") == 0) {
        if (player_ == nullptr || runner_ == nullptr) {
            *reply = MakeError(request, "package_runner_missing",
                               "Motion package runner is unavailable");
            return true;
        }
        const char* run_session_id = nullptr;
        if (!ReadString(request, "run_session_id", &run_session_id)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package run status requires run_session_id");
            return true;
        }
        const MotionPackageRunnerResult run_result =
            runner_->Tick(*player_, owner_id, run_session_id, NowMs());
        if (!run_result.ok) {
            *reply = MakeError(request, run_result.code,
                               "Motion package run status was rejected");
            return true;
        }
        *reply = MakeStatus(request,
                            run_result.stopped ? "run_finished" : "run_running");
        cJSON_AddStringToObject(*reply, "run_session_id",
                                run_result.run_session_id.c_str());
        cJSON_AddBoolToObject(*reply, "hardware_apply", false);
        if (!AddSample(*reply, run_result.sample)) {
            cJSON_Delete(*reply);
            *reply = nullptr;
        }
        return true;
    }

    if (std::strcmp(op, "package_run_stop") == 0) {
        if (runner_ == nullptr) {
            *reply = MakeError(request, "package_runner_missing",
                               "Motion package runner is unavailable");
            return true;
        }
        const char* run_session_id = nullptr;
        if (!ReadString(request, "run_session_id", &run_session_id)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package run stop requires run_session_id");
            return true;
        }
        const MotionPackageRunnerResult run_result =
            runner_->Stop(owner_id, run_session_id);
        if (!run_result.ok) {
            *reply = MakeError(request, run_result.code,
                               "Motion package run stop was rejected");
            return true;
        }
        *reply = MakeStatus(request, "run_stopped");
        cJSON_AddStringToObject(*reply, "run_session_id",
                                run_result.run_session_id.c_str());
        cJSON_AddBoolToObject(*reply, "hardware_apply", false);
        return true;
    }

    if (std::strcmp(op, "package_hardware_run_start") == 0) {
        return HandleHardwareRunStart(request, owner_id, profile, core,
                                      access_key_valid, reply);
    }

    if (std::strcmp(op, "package_hardware_run_status") == 0) {
        return HandleHardwareRunStatus(request, owner_id, core, reply);
    }

    if (std::strcmp(op, "package_hardware_run_stop") == 0) {
        return HandleHardwareRunStop(request, owner_id, core, reply);
    }

    if (std::strcmp(op, "package_delete") == 0) {
        if (!access_key_valid) {
            *reply = MakeError(request, "access_denied",
                               "Motion package delete requires the Live access key");
            return true;
        }
        const char* package_id = nullptr;
        cJSON* package_id_item = cJSON_GetObjectItem(request, "package_id");
        if (package_id_item != nullptr &&
            (!cJSON_IsString(package_id_item) ||
             package_id_item->valuestring == nullptr)) {
            *reply = MakeError(request, "bad_json",
                               "Motion package delete package_id must be a string");
            return true;
        }
        package_id = package_id_item != nullptr ? package_id_item->valuestring
                                                : nullptr;
        const MotionPackageManagerResult result =
            package_id != nullptr ? manager_->DeleteById(package_id)
                                  : manager_->Delete();
        if (!result.ok) {
            *reply = MakeError(request, result.code,
                               "Motion package delete was rejected");
            AddManagerResult(*reply, result);
            return true;
        }
        if (player_ != nullptr) {
            player_->Clear();
        }
        if (runner_ != nullptr) {
            runner_->Clear();
        }
        if (hardware_runner_ != nullptr) {
            hardware_runner_->Clear();
        }
        *reply = MakeStatus(request, "deleted");
        if (package_id != nullptr) {
            cJSON_AddStringToObject(*reply, "package_id", package_id);
        }
        return true;
    }

    return false;
}

void MotionPackageProtocol::OnTransportClosed(int owner_id) {
    if (owner_id == upload_owner_id_) {
        if (manager_ != nullptr) {
            manager_->AbortUpload();
        }
        upload_session_id_.clear();
        upload_owner_id_ = 0;
    }
    if (runner_ != nullptr) {
        runner_->OnTransportClosed(owner_id);
    }
    if (hardware_runner_ != nullptr) {
        hardware_runner_->OnTransportClosed(nullptr, owner_id);
    }
}

void MotionPackageProtocol::OnLiveSessionStopped() {
    if (hardware_runner_ != nullptr) {
        hardware_runner_->Clear();
    }
}

}  // namespace gosha::motion_live

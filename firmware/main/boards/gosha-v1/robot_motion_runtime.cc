#include "robot_motion_runtime.h"
#include "motion_package_upload.h"
#include <cstring>

namespace gosha::motion_live {
namespace {

bool ValidRequest(const std::string& id) {
    if (id.size() < 16 || id.size() > 64) return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

MotionPackageLoadedRecord Builtin(const MotionLivePreparedProfile& profile,
                                  const std::string& id) {
    const bool greeting = id == "builtin/greeting";
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema_version", 1);
    cJSON_AddStringToObject(root, "package_type", kMotionPackageDraftType);
    cJSON_AddStringToObject(root, "source_motion_id", id.c_str());
    cJSON_AddStringToObject(root, "name", greeting ? "Поздороваться" : "Помахать правой рукой");
    cJSON_AddStringToObject(root, "profile_id", profile.profile_id);
    cJSON_AddNumberToObject(root, "profile_version", 1);
    cJSON_AddStringToObject(root, "calibration_id", profile.calibration_id);
    cJSON_AddStringToObject(root, "units", "relative_degrees");
    // Existing package schema flags describe the file format, not publication.
    cJSON_AddBoolToObject(root, "source_preview_only", true);
    cJSON_AddBoolToObject(root, "hardware_validated", false);
    cJSON_AddBoolToObject(root, "live_compatible", true);
    cJSON_AddBoolToObject(root, "robot_storage_implemented", false);
    cJSON_AddNumberToObject(root, "duration_ms", greeting ? 12000 : 6000);
    cJSON_AddStringToObject(root, "interpolation", "linear");
    cJSON* joints = cJSON_AddArrayToObject(root, "active_joints");
    cJSON* constraints = cJSON_AddArrayToObject(root, "constraints");
    for (int i = 0; i < profile.joint_count; ++i) {
        const auto& joint = profile.joints[i];
        cJSON_AddItemToArray(joints, cJSON_CreateString(joint.id));
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", joint.id);
        cJSON_AddNumberToObject(item, "min", joint.min_relative_degrees);
        cJSON_AddNumberToObject(item, "max", joint.max_relative_degrees);
        cJSON_AddNumberToObject(item, "max_speed_dps", joint.max_speed_dps);
        cJSON_AddItemToArray(constraints, item);
    }
    cJSON* frames = cJSON_AddArrayToObject(root, "keyframes");
    for (int frame = 0; frame < (greeting ? 5 : 3); ++frame) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "time_ms", frame * 3000);
        cJSON* target = cJSON_AddObjectToObject(item, "target");
        for (int i = 0; i < profile.joint_count; ++i) {
            const char* joint = profile.joints[i].id;
            cJSON_AddNumberToObject(target, joint,
                std::strcmp(joint, "arm_positive_x") == 0 && frame % 2 ? -15 : 0);
        }
        cJSON_AddItemToArray(frames, item);
    }
    char* encoded = cJSON_PrintUnformatted(root);
    MotionPackageLoadedRecord record;
    record.package_id = id;
    record.profile_id = profile.profile_id;
    record.calibration_id = profile.calibration_id;
    if (encoded != nullptr) {
        record.payload.assign(encoded, encoded + std::strlen(encoded));
        record.payload_crc32 = MotionPackageCrc32(record.payload.data(), record.payload.size());
        cJSON_free(encoded);
    }
    cJSON_Delete(root);
    return record;
}

void AddEntry(cJSON* list, const char* id, const char* name, const char* source,
              uint32_t duration_ms) {
    cJSON* entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "motion_id", id);
    cJSON_AddStringToObject(entry, "name", name);
    cJSON_AddStringToObject(entry, "source", source);
    cJSON_AddNumberToObject(entry, "duration_ms", duration_ms);
    cJSON_AddItemToArray(list, entry);
}
}  // namespace

cJSON* RobotMotionRuntime::Error(const char* reason) const {
    cJSON* reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "status", "rejected");
    cJSON_AddStringToObject(reply, "reason", reason);
    return reply;
}

bool RobotMotionRuntime::LoadRecord(const MotionLivePreparedProfile& profile,
                                    const std::string& id,
                                    MotionPackageLoadedRecord* record) {
    if (id == "builtin/hand_wave" || id == "builtin/greeting") {
        *record = Builtin(profile, id);
        return !record->payload.empty();
    }
    if (id.rfind("stored/", 0) != 0 || id.size() <= 7 || id.size() > 128 || !manager_) return false;
    return manager_->LoadById(id.substr(7).c_str(), record).ok;
}

cJSON* RobotMotionRuntime::List(const MotionLivePreparedProfile* profile) {
    if (!profile) return Error("profile_unavailable");
    // Parsing the stored catalog must not hold the hardware lock while its
    // timer is actively applying a movement. Status remains a cheap read.
    if (running()) return Error("movement_busy");
    cJSON* reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "status", "confirmed");
    cJSON* items = cJSON_AddArrayToObject(reply, "movements");
    MotionPackagePlayer validator;
    for (const auto* id : {"builtin/hand_wave", "builtin/greeting"}) {
        auto record = Builtin(*profile, id);
        if (validator.Load(*profile, record).ok) {
            AddEntry(items, id, std::strcmp(id, "builtin/hand_wave") == 0 ?
                "Помахать правой рукой" : "Поздороваться", "firmware", validator.duration_ms());
        }
    }
    std::vector<MotionPackageStoreEntry> stored;
    if (manager_ && manager_->List(&stored).ok) {
        for (const auto& entry : stored) {
            if (!validator.Load(*profile, entry.record).ok) continue;
            cJSON* payload = cJSON_ParseWithLength(
                reinterpret_cast<const char*>(entry.record.payload.data()), entry.record.payload.size());
            const cJSON* name = cJSON_GetObjectItem(payload, "name");
            const std::string id = "stored/" + entry.record.package_id;
            AddEntry(items, id.c_str(), cJSON_IsString(name) ? name->valuestring : entry.record.package_id.c_str(),
                     "stored", validator.duration_ms());
            cJSON_Delete(payload);
        }
    }
    return reply;
}

cJSON* RobotMotionRuntime::Play(const MotionLivePreparedProfile* profile, MotionLiveCore* core,
                               int owner, const std::string& id, const std::string& request,
                               bool editor_busy, uint64_t now_ms) {
    if (!profile || !core || owner == 0) return Error("runtime_unavailable");
    if (!ValidRequest(request)) return Error("invalid_request_id");
    for (const auto& prior : requests_) {
        if (prior.id != request) continue;
        if (prior.motion != id) return Error("request_id_conflict");
        if (request == request_id_) return Status();
        return Error("request_already_processed");
    }
    if (running()) return Error("movement_busy");
    if (editor_busy || core->IsArmed() || (manager_ && manager_->upload_active())) return Error("editor_busy");
    MotionPackageLoadedRecord record;
    if (!LoadRecord(*profile, id, &record)) return Error("movement_unavailable");
    if (!player_.Load(*profile, record).ok) return Error("movement_invalid");
    MotionPackageSample start;
    if (!player_.Sample(0, &start).ok) return Error("movement_invalid");
    requests_.push_back({request, id});
    if (requests_.size() > 32) requests_.pop_front();
    request_id_ = request; motion_id_ = id; owner_ = owner;
    duration_ms_ = player_.duration_ms();
    run_id_ = "robot-motion-" + request;
    state_ = "failed";
    // A play request authorizes preparation within this local operation.
    // No initialization is required from the user or performed at boot/list.
    const auto initialized = core->InitializeRightArm(owner, profile->calibration_id, true);
    if (!initialized.ok) { reason_ = initialized.code; return Status(); }
    const auto& run = runner_.Start(player_, core, owner_, run_id_, "robot-drive-" + request,
                                    profile->calibration_id, true, 10, now_ms, true);
    reason_ = run.code;
    state_ = run.ok ? "in_progress" : "failed";
    return Status();
}

cJSON* RobotMotionRuntime::Status() const {
    cJSON* reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "status", state_);
    cJSON_AddStringToObject(reply, "reason", reason_);
    cJSON_AddStringToObject(reply, "motion_id", motion_id_.c_str());
    cJSON_AddStringToObject(reply, "request_id", request_id_.c_str());
    cJSON_AddNumberToObject(reply, "duration_ms", duration_ms_);
    cJSON_AddBoolToObject(reply, "completion_confirmed", std::strcmp(state_, "finished") == 0);
    return reply;
}

cJSON* RobotMotionRuntime::Stop(MotionLiveCore* core, int owner) {
    if (running()) {
        if (owner != owner_) return Error("movement_not_owner");
        const auto& stopped = runner_.Stop(core, owner_, run_id_);
        state_ = stopped.ok ? "stopped" : "failed"; reason_ = stopped.code;
    }
    return Status();
}

void RobotMotionRuntime::Tick(MotionLiveCore* core, uint64_t now_ms) {
    if (!running()) return;
    const auto& result = runner_.TickActive(player_, core, now_ms);
    if (!result.ok) { state_ = "failed"; reason_ = result.code; }
    else if (result.stopped) { state_ = "finished"; reason_ = "ok"; }
}

void RobotMotionRuntime::OnTransportClosed(MotionLiveCore* core, int owner) {
    if (!running() || owner != owner_) return;
    runner_.OnTransportClosed(core, owner);
    state_ = "stopped"; reason_ = "connection_closed";
}
}  // namespace gosha::motion_live

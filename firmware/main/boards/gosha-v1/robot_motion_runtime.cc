#include "robot_motion_runtime.h"
#include "motion_package_upload.h"
#include <cstring>
#include <algorithm>

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

bool RobotMotionRuntime::LoadRecord(const std::string& id,
                                    MotionPackageLoadedRecord* record) {
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
    cJSON_AddStringToObject(reply, "ordinary_profile", "otto_legacy_20260914");
    cJSON_AddStringToObject(reply, "hardware_note", "Левая рука отключена; движения выполняют ноги, стопы и правая рука.");
    LegacyMotionPlan plan;
    for (const auto& entry : LegacyMotionPlan::Catalog()) {
        if (!plan.Build(entry.id, {90, 90, 90, 90, 90, 135})) continue;
        AddEntry(items, entry.id, entry.name, "firmware", plan.duration_ms());
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
    const bool builtin = id.rfind("builtin/", 0) == 0;
    stored_after_home_ = false;
    profile_ = profile;
    if (builtin) {
        // Validate the complete named plan before any initialization/PWM write.
        auto start_pose = core->BuiltinStartPose();
        if (!core->right_arm_initialized_) start_pose[5] = kRightArmHomeDegrees;
        if (!legacy_plan_.Build(id, start_pose)) return Error("movement_unavailable");
        duration_ms_ = legacy_plan_.duration_ms();
    } else {
        MotionPackageLoadedRecord record;
        if (!LoadRecord(id, &record)) return Error("movement_unavailable");
        if (!player_.Load(*profile, record).ok) return Error("movement_invalid");
        MotionPackageSample start;
        if (!player_.Sample(0, &start).ok) return Error("movement_invalid");
        duration_ms_ = player_.duration_ms();
        if (std::strcmp(core->EvaluateSafety(), "ordinary_pose_outside_editor") == 0) {
            if (!legacy_plan_.Build("builtin/home", core->BuiltinStartPose())) return Error("movement_invalid");
            stored_after_home_ = true;
            duration_ms_ += legacy_plan_.duration_ms();
        }
    }
    requests_.push_back({request, id});
    if (requests_.size() > 32) requests_.pop_front();
    request_id_ = request; motion_id_ = id; owner_ = owner;
    run_id_ = "robot-motion-" + request;
    state_ = "failed";
    // A play request authorizes preparation within this local operation.
    // No initialization is required from the user or performed at boot/list.
    const auto initialized = core->InitializeRightArm(owner, profile->calibration_id, true);
    if (!initialized.ok) { reason_ = initialized.code; return Status(); }
    if (builtin || stored_after_home_) {
        reason_ = core->BeginBuiltinMovement(owner, now_ms);
        legacy_running_ = std::strcmp(reason_, "ok") == 0;
        legacy_start_ms_ = last_tick_ms_ = now_ms;
        state_ = legacy_running_ ? "in_progress" : "failed";
        return Status();
    }
    last_tick_ms_ = now_ms;
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
        if (legacy_running_) {
            core->EndBuiltinMovement(owner_);
            legacy_running_ = stored_after_home_ = false;
            state_ = "stopped"; reason_ = "ok";
            return Status();
        }
        const auto& stopped = runner_.Stop(core, owner_, run_id_);
        state_ = stopped.ok ? "stopped" : "failed"; reason_ = stopped.code;
    }
    return Status();
}

void RobotMotionRuntime::Tick(MotionLiveCore* core, uint64_t now_ms) {
    if (!running()) return;
    if (legacy_running_) {
        LegacyMotionPlan::Pose target;
        const uint64_t elapsed = now_ms >= legacy_start_ms_ ? now_ms - legacy_start_ms_ : 0;
        if (!legacy_plan_.Sample(static_cast<uint32_t>(std::min<uint64_t>(elapsed, 120000)), &target))
            reason_ = "movement_invalid";
        else reason_ = core->ApplyBuiltinMovement(owner_, target, now_ms);
        if (std::strcmp(reason_, "ok") != 0) {
            core->EndBuiltinMovement(owner_);
            legacy_running_ = stored_after_home_ = false;
            state_ = "failed";
            return;
        }
        if (elapsed < legacy_plan_.duration_ms()) return;
        core->EndBuiltinMovement(owner_);
        legacy_running_ = false;
        if (!stored_after_home_) { state_ = "finished"; return; }
        stored_after_home_ = false;
        const auto& run = runner_.Start(player_, core, owner_, run_id_, "robot-drive-" + request_id_,
                                        profile_->calibration_id, true, 10, now_ms, true);
        reason_ = run.code; state_ = run.ok ? "in_progress" : "failed";
        last_tick_ms_ = now_ms;
        return;
    }
    if (now_ms >= last_tick_ms_ && now_ms - last_tick_ms_ < 50) return;
    last_tick_ms_ = now_ms;
    const auto& result = runner_.TickActive(player_, core, now_ms);
    if (!result.ok) { state_ = "failed"; reason_ = result.code; }
    else if (result.stopped) { state_ = "finished"; reason_ = "ok"; }
}

void RobotMotionRuntime::OnTransportClosed(MotionLiveCore* core, int owner) {
    if (!running() || owner != owner_) return;
    if (legacy_running_) {
        core->EndBuiltinMovement(owner_);
        legacy_running_ = stored_after_home_ = false;
    } else runner_.OnTransportClosed(core, owner);
    state_ = "stopped"; reason_ = "connection_closed";
}
}  // namespace gosha::motion_live

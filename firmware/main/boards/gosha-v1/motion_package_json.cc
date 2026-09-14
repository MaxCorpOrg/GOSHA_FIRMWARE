#include "motion_package_json.h"

#include <cJSON.h>

#include <cmath>
#include <cstring>
#include <utility>

#include "motion_package_upload.h"

namespace gosha::motion_live {

namespace {

bool IsObject(cJSON* item) {
    return item != nullptr && cJSON_IsObject(item);
}

bool ReadString(cJSON* object, const char* key, const char** out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr || out == nullptr) {
        return false;
    }
    *out = item->valuestring;
    return true;
}

bool ReadBool(cJSON* object, const char* key, bool* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsBool(item) || out == nullptr) {
        return false;
    }
    *out = cJSON_IsTrue(item);
    return true;
}

bool ReadUint32(cJSON* object, const char* key, uint32_t* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        item->valuedouble < 0.0 ||
        item->valuedouble > static_cast<double>(UINT32_MAX) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        out == nullptr) {
        return false;
    }
    *out = static_cast<uint32_t>(item->valuedouble);
    return true;
}

bool ReadInt(cJSON* object, const char* key, int* out) {
    uint32_t value = 0;
    if (!ReadUint32(object, key, &value) || value > static_cast<uint32_t>(INT32_MAX) ||
        out == nullptr) {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool ReadDouble(cJSON* object, const char* key, double* out) {
    cJSON* item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        out == nullptr) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

MotionPackageJsonParseResult Error(const char* code, int frame_index = -1,
                                   const char* joint_id = "") {
    MotionPackageJsonParseResult result;
    result.ok = false;
    result.code = code;
    result.frame_index = frame_index;
    result.joint_id = joint_id == nullptr ? "" : joint_id;
    return result;
}

MotionPackageJsonParseResult Ok() {
    MotionPackageJsonParseResult result;
    result.ok = true;
    result.code = "ok";
    return result;
}

bool ReadConstraint(cJSON* item, MotionPackageConstraint* constraint,
                    std::string* id_storage) {
    const char* id = nullptr;
    if (!IsObject(item) || constraint == nullptr || id_storage == nullptr ||
        !ReadString(item, "id", &id) ||
        !ReadDouble(item, "min", &constraint->min_relative_degrees) ||
        !ReadDouble(item, "max", &constraint->max_relative_degrees) ||
        !ReadDouble(item, "max_speed_dps", &constraint->max_speed_dps)) {
        return false;
    }
    *id_storage = id;
    constraint->id = id_storage->c_str();
    return true;
}

}  // namespace

void MotionPackageOwnedDraft::BindPointers() {
    draft.package_type = package_type.c_str();
    draft.name = name.c_str();
    draft.profile_id = profile_id.c_str();
    draft.calibration_id = calibration_id.c_str();
    draft.interpolation = interpolation.c_str();
    draft.active_joints.fill("");
    for (int i = 0; i < draft.active_joint_count; ++i) {
        draft.active_joints[i] = active_joint_ids[i].c_str();
    }
    for (int i = 0; i < draft.constraint_count; ++i) {
        draft.constraints[i].id = constraint_ids[i].c_str();
    }
    for (int frame_index = 0; frame_index < draft.keyframe_count; ++frame_index) {
        MotionPackageKeyframe& frame = keyframes[frame_index];
        for (int target_index = 0; target_index < frame.target_count; ++target_index) {
            frame.targets[target_index].id =
                target_ids[frame_index][target_index].c_str();
        }
    }
    draft.keyframes = keyframes.empty() ? nullptr : keyframes.data();
}

MotionPackageJsonParseResult ParseMotionPackageDraftJson(
    const uint8_t* data, size_t size, MotionPackageOwnedDraft* package) {
    if (data == nullptr || size == 0 || size > kMotionPackageUploadMaxBytes ||
        package == nullptr) {
        return Error("package_json_size");
    }

    MotionPackageOwnedDraft parsed;
    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(
        reinterpret_cast<const char*>(data), size, &parse_end, false);
    if (!IsObject(root) ||
        parse_end != reinterpret_cast<const char*>(data) + size) {
        cJSON_Delete(root);
        return Error("package_json_parse");
    }

    const char* package_type = nullptr;
    const char* name = nullptr;
    const char* profile_id = nullptr;
    const char* calibration_id = nullptr;
    const char* interpolation = nullptr;
    if (!ReadInt(root, "schema_version", &parsed.draft.schema_version) ||
        !ReadString(root, "package_type", &package_type) ||
        !ReadString(root, "profile_id", &profile_id) ||
        !ReadInt(root, "profile_version", &parsed.draft.profile_version) ||
        !ReadString(root, "calibration_id", &calibration_id) ||
        !ReadBool(root, "source_preview_only",
                  &parsed.draft.source_preview_only) ||
        !ReadBool(root, "hardware_validated",
                  &parsed.draft.hardware_validated) ||
        !ReadBool(root, "live_compatible", &parsed.draft.live_compatible) ||
        !ReadBool(root, "robot_storage_implemented",
                  &parsed.draft.robot_storage_implemented) ||
        !ReadUint32(root, "duration_ms", &parsed.draft.duration_ms) ||
        !ReadString(root, "interpolation", &interpolation)) {
        cJSON_Delete(root);
        return Error("package_json_header");
    }
    if (ReadString(root, "name", &name)) {
        parsed.name = name;
    }
    parsed.package_type = package_type;
    parsed.profile_id = profile_id;
    parsed.calibration_id = calibration_id;
    parsed.interpolation = interpolation;

    cJSON* active_joints = cJSON_GetObjectItem(root, "active_joints");
    const int active_count = cJSON_GetArraySize(active_joints);
    if (!cJSON_IsArray(active_joints) || active_count <= 0 ||
        active_count > kMaxActiveJointCount) {
        cJSON_Delete(root);
        return Error("package_json_joint_set");
    }
    parsed.draft.active_joint_count = active_count;
    for (int i = 0; i < active_count; ++i) {
        cJSON* item = cJSON_GetArrayItem(active_joints, i);
        if (!cJSON_IsString(item) || item->valuestring == nullptr) {
            cJSON_Delete(root);
            return Error("package_json_joint_set");
        }
        parsed.active_joint_ids[i] = item->valuestring;
        parsed.draft.active_joints[i] = parsed.active_joint_ids[i].c_str();
    }

    cJSON* constraints = cJSON_GetObjectItem(root, "constraints");
    const int constraint_count = cJSON_GetArraySize(constraints);
    if (!cJSON_IsArray(constraints) || constraint_count <= 0 ||
        constraint_count > kMaxActiveJointCount) {
        cJSON_Delete(root);
        return Error("package_json_constraints");
    }
    parsed.draft.constraint_count = constraint_count;
    for (int i = 0; i < constraint_count; ++i) {
        if (!ReadConstraint(cJSON_GetArrayItem(constraints, i),
                            &parsed.draft.constraints[i],
                            &parsed.constraint_ids[i])) {
            cJSON_Delete(root);
            return Error("package_json_constraints");
        }
    }

    cJSON* keyframes = cJSON_GetObjectItem(root, "keyframes");
    const int keyframe_count = cJSON_GetArraySize(keyframes);
    if (!cJSON_IsArray(keyframes) || keyframe_count <= 0 ||
        keyframe_count > kMotionPackageMaxKeyframes) {
        cJSON_Delete(root);
        return Error("package_json_keyframes");
    }
    parsed.draft.keyframe_count = keyframe_count;
    parsed.keyframes.resize(static_cast<size_t>(keyframe_count));
    parsed.target_ids.resize(static_cast<size_t>(keyframe_count));
    for (int frame_index = 0; frame_index < keyframe_count; ++frame_index) {
        cJSON* frame_json = cJSON_GetArrayItem(keyframes, frame_index);
        MotionPackageKeyframe& frame = parsed.keyframes[frame_index];
        if (!IsObject(frame_json) ||
            !ReadUint32(frame_json, "time_ms", &frame.time_ms)) {
            cJSON_Delete(root);
            return Error("package_json_keyframe", frame_index);
        }
        cJSON* target = cJSON_GetObjectItem(frame_json, "target");
        if (!IsObject(target)) {
            cJSON_Delete(root);
            return Error("package_json_target", frame_index);
        }
        int target_count = 0;
        for (cJSON* item = target->child; item != nullptr; item = item->next) {
            if (target_count >= kMaxActiveJointCount ||
                item->string == nullptr ||
                !cJSON_IsNumber(item) ||
                !std::isfinite(item->valuedouble)) {
                cJSON_Delete(root);
                return Error("package_json_target", frame_index,
                             item == nullptr ? "" : item->string);
            }
            parsed.target_ids[frame_index][target_count] = item->string;
            frame.targets[target_count] = {
                parsed.target_ids[frame_index][target_count].c_str(),
                item->valuedouble,
            };
            ++target_count;
        }
        if (target_count <= 0) {
            cJSON_Delete(root);
            return Error("package_json_target", frame_index);
        }
        frame.target_count = target_count;
    }

    cJSON_Delete(root);
    parsed.BindPointers();
    *package = std::move(parsed);
    package->BindPointers();
    return Ok();
}

}  // namespace gosha::motion_live

#include "motion_package.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gosha::motion_live {

namespace {

constexpr double kAngleEpsilon = 0.000001;

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

bool IsFinite(double value) {
    return std::isfinite(value);
}

bool IsHex64(const char* value) {
    if (value == nullptr) {
        return false;
    }
    for (int i = 0; i < 64; ++i) {
        const char c = value[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return value[64] == '\0';
}

bool AlmostEqual(double left, double right) {
    return std::fabs(left - right) <= kAngleEpsilon;
}

MotionPackageValidationResult Error(const char* code, const char* joint_id = "",
                                    int frame_index = -1, double value = 0.0,
                                    double limit = 0.0) {
    MotionPackageValidationResult result;
    result.ok = false;
    result.code = code;
    result.joint_id = joint_id == nullptr ? "" : joint_id;
    result.frame_index = frame_index;
    result.value = value;
    result.limit = limit;
    return result;
}

MotionPackageValidationResult Ok() {
    MotionPackageValidationResult result;
    result.ok = true;
    result.code = "ok";
    return result;
}

int ProfileJointCount(const MotionLivePreparedProfile& profile) {
    if (profile.joint_count < 0 || profile.joint_count > kMaxActiveJointCount) {
        return -1;
    }
    return profile.joint_count;
}

int FindProfileJointById(const MotionLivePreparedProfile& profile, const char* id) {
    if (id == nullptr) {
        return -1;
    }
    const int count = ProfileJointCount(profile);
    if (count <= 0) {
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (Streq(profile.joints[i].id, id)) {
            return i;
        }
    }
    return -1;
}

int FindConstraintById(const MotionPackageDraft& package, const char* id) {
    for (int i = 0; i < package.constraint_count; ++i) {
        if (Streq(package.constraints[i].id, id)) {
            return i;
        }
    }
    return -1;
}

MotionPackageValidationResult ValidateProfileAndHeader(
    const MotionLivePreparedProfile& profile,
    const MotionPackageDraft& package) {
    const int profile_joint_count = ProfileJointCount(profile);
    if (!Streq(profile.mode, kProfileModeMotionEditor) ||
        !Streq(profile.profile_id, kModelProfileId) ||
        !IsHex64(profile.calibration_id) ||
        profile_joint_count != kMaxActiveJointCount) {
        return Error("profile_not_motion_editor");
    }
    if (package.schema_version != kMotionPackageDraftSchemaVersion ||
        !Streq(package.package_type, kMotionPackageDraftType)) {
        return Error("package_schema");
    }
    if (!Streq(package.profile_id, kModelProfileId) ||
        package.profile_version != kMotionPackageProfileVersion ||
        !Streq(package.calibration_id, profile.calibration_id)) {
        return Error("package_profile_mismatch");
    }
    if (!package.source_preview_only ||
        package.hardware_validated ||
        !package.live_compatible ||
        package.robot_storage_implemented) {
        return Error("package_flags");
    }
    if (package.duration_ms < kMotionPackageMinDurationMs ||
        package.duration_ms > kMotionPackageMaxDurationMs) {
        return Error("package_duration");
    }
    if (!Streq(package.interpolation, "smooth") &&
        !Streq(package.interpolation, "linear")) {
        return Error("package_interpolation");
    }
    if (package.active_joint_count != profile_joint_count ||
        package.constraint_count != profile_joint_count) {
        return Error("package_joint_set");
    }
    if (package.keyframes == nullptr ||
        package.keyframe_count <= 0 ||
        package.keyframe_count > kMotionPackageMaxKeyframes) {
        return Error("package_keyframes");
    }
    return Ok();
}

MotionPackageValidationResult ValidateJointSet(
    const MotionLivePreparedProfile& profile,
    const MotionPackageDraft& package) {
    const int profile_joint_count = ProfileJointCount(profile);
    std::array<bool, kMaxActiveJointCount> seen_profile{};
    std::array<bool, kMaxActiveJointCount> seen_constraints{};
    seen_profile.fill(false);
    seen_constraints.fill(false);

    for (int i = 0; i < package.active_joint_count; ++i) {
        const int profile_index = FindProfileJointById(profile, package.active_joints[i]);
        if (profile_index < 0 || seen_profile[profile_index]) {
            return Error("package_joint_set", package.active_joints[i]);
        }
        seen_profile[profile_index] = true;
    }

    for (int i = 0; i < package.constraint_count; ++i) {
        const auto& constraint = package.constraints[i];
        const int profile_index = FindProfileJointById(profile, constraint.id);
        if (profile_index < 0 || seen_constraints[profile_index]) {
            return Error("package_constraints", constraint.id);
        }
        const auto& joint = profile.joints[profile_index];
        if (!AlmostEqual(constraint.min_relative_degrees,
                         joint.min_relative_degrees) ||
            !AlmostEqual(constraint.max_relative_degrees,
                         joint.max_relative_degrees) ||
            !AlmostEqual(constraint.max_speed_dps, joint.max_speed_dps)) {
            return Error("package_constraints", constraint.id);
        }
        seen_constraints[profile_index] = true;
    }

    for (int i = 0; i < profile_joint_count; ++i) {
        if (!seen_profile[i] || !seen_constraints[i]) {
            return Error("package_joint_set", profile.joints[i].id);
        }
    }
    return Ok();
}

MotionPackageValidationResult ExtractFrameValues(
    const MotionLivePreparedProfile& profile,
    const MotionPackageDraft& package,
    const MotionPackageKeyframe& frame,
    int frame_index,
    std::array<double, kMaxActiveJointCount>* values) {
    if (frame.target_count != package.active_joint_count || values == nullptr) {
        return Error("frame_target_set", "", frame_index);
    }
    std::array<bool, kMaxActiveJointCount> seen_profile{};
    seen_profile.fill(false);

    for (int i = 0; i < frame.target_count; ++i) {
        const auto& target = frame.targets[i];
        const int profile_index = FindProfileJointById(profile, target.id);
        if (profile_index < 0) {
            return Error("target_inactive", target.id, frame_index,
                         target.relative_degrees);
        }
        if (seen_profile[profile_index]) {
            return Error("target_duplicate", target.id, frame_index,
                         target.relative_degrees);
        }
        const int constraint_index = FindConstraintById(package, target.id);
        if (constraint_index < 0) {
            return Error("package_constraints", target.id, frame_index);
        }
        const auto& constraint = package.constraints[constraint_index];
        const auto& joint = profile.joints[profile_index];
        const double value = target.relative_degrees;
        if (!IsFinite(value)) {
            return Error("target_not_finite", target.id, frame_index, value);
        }
        if (value < joint.min_relative_degrees ||
            value > joint.max_relative_degrees ||
            value < constraint.min_relative_degrees ||
            value > constraint.max_relative_degrees) {
            const double limit =
                value < joint.min_relative_degrees
                    ? std::max(joint.min_relative_degrees,
                               constraint.min_relative_degrees)
                    : std::min(joint.max_relative_degrees,
                               constraint.max_relative_degrees);
            return Error("target_out_of_range", target.id, frame_index, value, limit);
        }
        (*values)[profile_index] = value;
        seen_profile[profile_index] = true;
    }

    const int profile_joint_count = ProfileJointCount(profile);
    for (int i = 0; i < profile_joint_count; ++i) {
        if (!seen_profile[i]) {
            return Error("frame_target_set", profile.joints[i].id, frame_index);
        }
    }
    return Ok();
}

}  // namespace

MotionPackageValidationResult ValidateMotionPackageDraft(
    const MotionLivePreparedProfile& profile,
    const MotionPackageDraft& package) {
    MotionPackageValidationResult result = ValidateProfileAndHeader(profile, package);
    if (!result.ok) {
        return result;
    }
    result = ValidateJointSet(profile, package);
    if (!result.ok) {
        return result;
    }

    uint32_t previous_time_ms = 0;
    std::array<double, kMaxActiveJointCount> previous_values{};
    previous_values.fill(0.0);
    const double interpolation_factor =
        Streq(package.interpolation, "smooth") ? 1.5 : 1.0;

    for (int frame_index = 0; frame_index < package.keyframe_count; ++frame_index) {
        const MotionPackageKeyframe& frame = package.keyframes[frame_index];
        if ((frame_index == 0 && frame.time_ms != 0) ||
            (frame_index > 0 && frame.time_ms <= previous_time_ms) ||
            frame.time_ms > package.duration_ms) {
            return Error("frame_time", "", frame_index,
                         static_cast<double>(frame.time_ms));
        }

        std::array<double, kMaxActiveJointCount> values{};
        values.fill(0.0);
        result = ExtractFrameValues(profile, package, frame, frame_index, &values);
        if (!result.ok) {
            return result;
        }

        if (frame_index > 0) {
            const double seconds =
                static_cast<double>(frame.time_ms - previous_time_ms) / 1000.0;
            const int profile_joint_count = ProfileJointCount(profile);
            for (int profile_index = 0; profile_index < profile_joint_count;
                 ++profile_index) {
                const auto& joint = profile.joints[profile_index];
                const double required_speed =
                    (std::fabs(values[profile_index] -
                               previous_values[profile_index]) /
                     seconds) *
                    interpolation_factor;
                if (required_speed > joint.max_speed_dps + kAngleEpsilon) {
                    return Error("target_speed_exceeded", joint.id, frame_index,
                                 required_speed, joint.max_speed_dps);
                }
            }
        }

        previous_time_ms = frame.time_ms;
        previous_values = values;
    }

    return Ok();
}

}  // namespace gosha::motion_live

#ifndef GOSHA_V1_MOTION_PACKAGE_H_
#define GOSHA_V1_MOTION_PACKAGE_H_

#include <array>
#include <cstdint>

#include "motion_live_core.h"

namespace gosha::motion_live {

constexpr int kMotionPackageDraftSchemaVersion = 1;
constexpr int kMotionPackageProfileVersion = 1;
constexpr int kMotionPackageMaxKeyframes = 1000;
constexpr uint32_t kMotionPackageMinDurationMs = 500;
constexpr uint32_t kMotionPackageMaxDurationMs = 120000;
constexpr const char* kMotionPackageDraftType =
    "gosha.motion.robot-package-draft.v1";

struct MotionPackageConstraint {
    const char* id = "";
    double min_relative_degrees = 0.0;
    double max_relative_degrees = 0.0;
    double max_speed_dps = 0.0;
};

struct MotionPackageTarget {
    const char* id = "";
    double relative_degrees = 0.0;
};

struct MotionPackageKeyframe {
    uint32_t time_ms = 0;
    int target_count = 0;
    std::array<MotionPackageTarget, kMaxActiveJointCount> targets{};
};

struct MotionPackageDraft {
    int schema_version = 0;
    const char* package_type = "";
    const char* name = "";
    const char* profile_id = "";
    int profile_version = 0;
    const char* calibration_id = "";
    bool source_preview_only = false;
    bool hardware_validated = true;
    bool live_compatible = false;
    bool robot_storage_implemented = true;
    uint32_t duration_ms = 0;
    const char* interpolation = "";
    int active_joint_count = 0;
    std::array<const char*, kMaxActiveJointCount> active_joints{};
    int constraint_count = 0;
    std::array<MotionPackageConstraint, kMaxActiveJointCount> constraints{};
    int keyframe_count = 0;
    const MotionPackageKeyframe* keyframes = nullptr;
};

struct MotionPackageValidationResult {
    bool ok = false;
    const char* code = "package_invalid";
    const char* joint_id = "";
    int frame_index = -1;
    double value = 0.0;
    double limit = 0.0;
};

MotionPackageValidationResult ValidateMotionPackageDraft(
    const MotionLivePreparedProfile& profile,
    const MotionPackageDraft& package);

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_H_

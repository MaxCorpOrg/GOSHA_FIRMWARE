#ifndef GOSHA_V1_MOTION_PACKAGE_JSON_H_
#define GOSHA_V1_MOTION_PACKAGE_JSON_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "motion_package.h"

namespace gosha::motion_live {

struct MotionPackageJsonParseResult {
    bool ok = false;
    const char* code = "package_json_invalid";
    int frame_index = -1;
    const char* joint_id = "";
};

struct MotionPackageOwnedDraft {
    MotionPackageDraft draft;
    std::string package_type;
    std::string name;
    std::string profile_id;
    std::string calibration_id;
    std::string interpolation;
    std::array<std::string, kMaxActiveJointCount> active_joint_ids;
    std::array<std::string, kMaxActiveJointCount> constraint_ids;
    std::vector<MotionPackageKeyframe> keyframes;
    std::vector<std::array<std::string, kMaxActiveJointCount>> target_ids;

    void BindPointers();
};

MotionPackageJsonParseResult ParseMotionPackageDraftJson(
    const uint8_t* data, size_t size, MotionPackageOwnedDraft* package);

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_PACKAGE_JSON_H_

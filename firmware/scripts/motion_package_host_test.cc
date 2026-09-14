#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../main/boards/gosha-v1/motion_package.h"

using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageConstraint;
using gosha::motion_live::MotionPackageDraft;
using gosha::motion_live::MotionPackageKeyframe;
using gosha::motion_live::MotionPackageTarget;
using gosha::motion_live::ValidateMotionPackageDraft;
using gosha::motion_live::kMaxActiveJointCount;
using gosha::motion_live::kMotionPackageDraftSchemaVersion;
using gosha::motion_live::kMotionPackageDraftType;
using gosha::motion_live::kMotionPackageProfileVersion;

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

MotionPackageKeyframe Frame(uint32_t time_ms, double right_arm, double left_leg,
                            double right_leg, double left_foot, double right_foot) {
    MotionPackageKeyframe frame;
    frame.time_ms = time_ms;
    frame.target_count = 5;
    frame.targets = {{
        {"arm_positive_x", right_arm},
        {"leg_negative_x", left_leg},
        {"leg_positive_x", right_leg},
        {"foot_negative_x", left_foot},
        {"foot_positive_x", right_foot},
    }};
    return frame;
}

MotionPackageDraft PackageFromFrames(const MotionPackageKeyframe* frames, int count) {
    MotionPackageDraft package;
    package.schema_version = kMotionPackageDraftSchemaVersion;
    package.package_type = kMotionPackageDraftType;
    package.profile_id = "gosha-preview-v1";
    package.profile_version = kMotionPackageProfileVersion;
    package.calibration_id = kCalibration;
    package.source_preview_only = true;
    package.hardware_validated = false;
    package.live_compatible = true;
    package.robot_storage_implemented = false;
    package.duration_ms = 8000;
    package.interpolation = "smooth";
    package.active_joint_count = 5;
    package.active_joints = {{
        "arm_positive_x",
        "leg_negative_x",
        "leg_positive_x",
        "foot_negative_x",
        "foot_positive_x",
    }};
    package.constraint_count = 5;
    package.constraints = {{
        {"arm_positive_x", -70.0, 45.0, 10.0},
        {"leg_negative_x", -35.0, 35.0, 10.0},
        {"leg_positive_x", -35.0, 35.0, 10.0},
        {"foot_negative_x", -30.0, 30.0, 10.0},
        {"foot_positive_x", -30.0, 30.0, 10.0},
    }};
    package.keyframe_count = count;
    package.keyframes = frames;
    return package;
}

bool CodeIs(const char* actual, const char* expected) {
    return std::string(actual) == expected;
}

}  // namespace

int main() {
    MotionLivePreparedProfile profile = MotionEditorProfile();
    std::array<MotionPackageKeyframe, 2> frames = {{
        Frame(0, 0, 0, 0, 0, 0),
        Frame(8000, 40, -12, 0, 0, 8),
    }};

    {
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(result.ok);
        CHECK(CodeIs(result.code, "ok"));
    }

    {
        std::array<MotionPackageKeyframe, 2> bad_frames = frames;
        bad_frames[1].targets[0] = {"arm_positive_x", 55.0};
        MotionPackageDraft package = PackageFromFrames(bad_frames.data(), 2);
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "target_out_of_range"));
        CHECK(CodeIs(result.joint_id, "arm_positive_x"));
        CHECK(result.frame_index == 1);
        CHECK(result.limit == 45.0);
    }

    {
        std::array<MotionPackageKeyframe, 2> bad_frames = frames;
        bad_frames[1].targets[0] = {"arm_negative_x", 10.0};
        MotionPackageDraft package = PackageFromFrames(bad_frames.data(), 2);
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "target_inactive"));
        CHECK(CodeIs(result.joint_id, "arm_negative_x"));
    }

    {
        std::array<MotionPackageKeyframe, 2> fast_frames = {{
            Frame(0, 0, 0, 0, 0, 0),
            Frame(1000, 20, 0, 0, 0, 0),
        }};
        MotionPackageDraft package = PackageFromFrames(fast_frames.data(), 2);
        package.duration_ms = 1000;
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "target_speed_exceeded"));
        CHECK(CodeIs(result.joint_id, "arm_positive_x"));
        CHECK(result.frame_index == 1);
        CHECK(result.limit == 10.0);
    }

    {
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        package.interpolation = "hold";
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "package_interpolation"));
    }

    {
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        package.calibration_id =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "package_profile_mismatch"));
    }

    {
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        package.robot_storage_implemented = true;
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "package_flags"));
    }

    {
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        package.constraints[0] =
            MotionPackageConstraint{"arm_positive_x", -70.0, 55.0, 10.0};
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "package_constraints"));
        CHECK(CodeIs(result.joint_id, "arm_positive_x"));
    }

    {
        std::array<MotionPackageKeyframe, 2> bad_frames = frames;
        bad_frames[0].time_ms = 1;
        MotionPackageDraft package = PackageFromFrames(bad_frames.data(), 2);
        auto result = ValidateMotionPackageDraft(profile, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "frame_time"));
    }

    {
        MotionLivePreparedProfile wrong_mode = profile;
        wrong_mode.mode = "commissioning_right_arm";
        MotionPackageDraft package = PackageFromFrames(frames.data(), 2);
        auto result = ValidateMotionPackageDraft(wrong_mode, package);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "profile_not_motion_editor"));
    }

    std::cout << "motion_package_host_test: PASS\n";
    return 0;
}

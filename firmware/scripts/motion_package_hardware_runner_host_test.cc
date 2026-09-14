#include <cmath>
#include <cstdint>
#include <array>
#include <iostream>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_hardware_runner.h"
#include "../main/boards/gosha-v1/motion_package_upload.h"

using gosha::motion_live::MotionLiveCore;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionLiveRuntimeConfig;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageHardwareRunner;
using gosha::motion_live::MotionPackageHardwareRunnerResult;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackagePlayer;
using gosha::motion_live::ServoSlot;
using gosha::motion_live::kPoseJointCount;
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
    runtime.joints[static_cast<int>(ServoSlot::kRightHand)] = {12, 0, kRightArmHomeDegrees, false};
    return runtime;
}

std::vector<uint8_t> Payload() {
    const std::string json =
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"source_motion_id\":\"hardware-runner\","
        "\"name\":\"hardware-runner\","
        "\"profile_id\":\"gosha-preview-v1\","
        "\"profile_version\":1,"
        "\"calibration_id\":\"" + std::string(kCalibration) + "\","
        "\"units\":\"relative_degrees\","
        "\"source_preview_only\":true,"
        "\"hardware_validated\":false,"
        "\"live_compatible\":true,"
        "\"robot_storage_implemented\":false,"
        "\"duration_ms\":8000,"
        "\"interpolation\":\"linear\","
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
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":40,"
        "\"leg_negative_x\":-16,\"leg_positive_x\":0,"
        "\"foot_negative_x\":0,\"foot_positive_x\":8}}]}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

MotionPackageLoadedRecord RecordFor(const std::vector<uint8_t>& payload) {
    return {
        "hardware-runner-001",
        "gosha-preview-v1",
        kCalibration,
        MotionPackageCrc32(payload.data(), payload.size()),
        payload,
    };
}

bool Near(double left, double right) {
    return std::fabs(left - right) <= 0.0001;
}

}  // namespace

int main() {
    const MotionLivePreparedProfile profile = MotionEditorProfile();
    const std::vector<uint8_t> payload = Payload();

    MotionPackagePlayer player;
    CHECK(player.Load(profile, RecordFor(payload)).ok);

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

    MotionPackageHardwareRunner runner;
    MotionPackageHardwareRunnerResult result =
        runner.Start(player, &core, 7, "hardware-run-0001", "live-session-0001",
                     kCalibration, true, 10.0, 1000);
    CHECK(result.ok);
    CHECK(runner.running());
    CHECK(!result.hardware_apply);
    CHECK(!result.live_armed);
    CHECK(!result.live_result.ok);
    CHECK(result.run_session_id == "hardware-run-0001");
    CHECK(result.live_session_id == "live-session-0001");
    CHECK(applied.empty());

    result = runner.Tick(player, &core, 8, "hardware-run-0001", 3000);
    CHECK(!result.ok);
    CHECK(std::string(result.code) == "package_hardware_run_not_owner");
    CHECK(runner.running());

    result = runner.Tick(player, &core, 7, "hardware-run-0001", 1300);
    CHECK(result.ok);
    CHECK(runner.running());
    CHECK(result.live_result.ok);
    CHECK(result.live_armed);
    CHECK(!result.hardware_apply);
    CHECK(!result.live_result.should_apply);
    CHECK(applied.empty());
    CHECK(result.sample.package_id[0] == '\0');
    CHECK(Near(core.CommandedPose().relative_degrees[1], 0.0));
    CHECK(Near(core.CommandedPose().relative_degrees[2], 0.0));

    result = runner.Tick(player, &core, 7, "hardware-run-0001", 1600);
    CHECK(result.ok);
    CHECK(runner.running());
    CHECK(result.live_result.ok);
    CHECK(result.live_armed);
    CHECK(result.hardware_apply);
    CHECK(result.live_result.should_apply);
    CHECK(!applied.empty());
    CHECK(applied.back()[static_cast<int>(ServoSlot::kRightHand)] == 138);
    CHECK(applied.back()[static_cast<int>(ServoSlot::kLeftLeg)] == 89);
    CHECK(Near(result.sample.target.relative_degrees[1], 3.0));
    CHECK(Near(result.sample.target.relative_degrees[2], -1.2));
    CHECK(Near(core.CommandedPose().relative_degrees[1], 3.0));
    CHECK(Near(core.CommandedPose().relative_degrees[2], -1.0));

    result = runner.Stop(&core, 8, "hardware-run-0001");
    CHECK(!result.ok);
    CHECK(std::string(result.code) == "package_hardware_run_not_owner");
    CHECK(runner.running());

    result = runner.Stop(&core, 7, "hardware-run-0001");
    CHECK(result.ok);
    CHECK(result.stopped);
    CHECK(!runner.running());

    result = runner.Tick(player, &core, 7, "hardware-run-0001", 4000);
    CHECK(!result.ok);
    CHECK(std::string(result.code) == "package_hardware_run_not_active");

    MotionPackagePlayer finish_player;
    CHECK(finish_player.Load(profile, RecordFor(payload)).ok);
    MotionLiveCore finish_core;
    finish_core.SetLocalOptInEnabled(true);
    finish_core.SetPreparedProfile(&profile);
    finish_core.SetRuntimeConfig(RuntimeConfig());
    finish_core.SetHardwareApplier([&applied](const std::array<int, kPoseJointCount>& degrees) {
        applied.push_back(degrees);
        return true;
    });
    finish_core.SetRightArmInitializer([](int home_degrees) {
        return home_degrees == kRightArmHomeDegrees;
    });
    CHECK(finish_core.InitializeRightArm(7, kCalibration, true).ok);
    MotionPackageHardwareRunner finish_runner;
    applied.clear();
    result = finish_runner.Start(finish_player, &finish_core, 7, "hardware-run-0002", "live-session-0002",
                          kCalibration, true, 10.0, 10000);
    CHECK(result.ok);
    result = finish_runner.Tick(finish_player, &finish_core, 7, "hardware-run-0002", 10300);
    CHECK(result.ok);
    CHECK(finish_runner.running());
    CHECK(result.live_armed);
    CHECK(!result.hardware_apply);
    for (uint64_t now_ms = 10600; now_ms < 18000; now_ms += 250) {
        result = finish_runner.Tick(finish_player, &finish_core, 7,
                                    "hardware-run-0002", now_ms);
        CHECK(result.ok);
        CHECK(!result.stopped);
        CHECK(finish_runner.running());
    }
    result = finish_runner.Tick(finish_player, &finish_core, 7,
                                "hardware-run-0002", 18000);
    CHECK(result.ok);
    CHECK(result.stopped);
    CHECK(!finish_runner.running());
    CHECK(result.live_armed);
    CHECK(result.hardware_apply);
    CHECK(result.live_result.ok);
    CHECK(result.live_result.stopped);
    CHECK(result.live_result.seq > 0);
    CHECK(result.sample.finished);
    CHECK(Near(result.sample.target.relative_degrees[1], 40.0));
    CHECK(Near(result.sample.target.relative_degrees[2], -16.0));
    CHECK(Near(finish_core.CommandedPose().relative_degrees[1], 40.0));
    CHECK(Near(finish_core.CommandedPose().relative_degrees[2], -16.0));

    result = finish_runner.Tick(finish_player, &finish_core, 7,
                                "hardware-run-0002", 18050);
    CHECK(result.ok);
    CHECK(result.stopped);
    CHECK(!finish_runner.running());
    CHECK(Near(result.sample.target.relative_degrees[1], 40.0));

    std::cout << "motion_package_hardware_runner_host_test: PASS\n";
    return 0;
}

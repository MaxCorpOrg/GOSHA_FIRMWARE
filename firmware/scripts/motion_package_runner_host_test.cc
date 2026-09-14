#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_runner.h"
#include "../main/boards/gosha-v1/motion_package_upload.h"

using gosha::motion_live::FindJointIndexById;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackagePlayer;
using gosha::motion_live::MotionPackageRunner;
using gosha::motion_live::MotionPackageRunnerResult;

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

std::vector<uint8_t> Payload(double first_right_arm = 0.0) {
    const std::string json =
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"source_motion_id\":\"runner\","
        "\"name\":\"runner\","
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
        "{\"time_ms\":0,\"target\":{\"arm_positive_x\":" +
        std::to_string(first_right_arm) +
        ",\"leg_negative_x\":0,"
        "\"leg_positive_x\":0,\"foot_negative_x\":0,\"foot_positive_x\":0}},"
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":40,"
        "\"leg_negative_x\":-16,\"leg_positive_x\":0,"
        "\"foot_negative_x\":0,\"foot_positive_x\":8}}]}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

MotionPackageLoadedRecord RecordFor(const char* package_id,
                                    const std::vector<uint8_t>& payload) {
    return {
        package_id,
        "gosha-preview-v1",
        kCalibration,
        MotionPackageCrc32(payload.data(), payload.size()),
        payload,
    };
}

double JointValue(const MotionPackageRunnerResult& result, const char* id) {
    const int index = FindJointIndexById(id);
    return index < 0 ? 0.0 : result.sample.target.relative_degrees[index];
}

bool Near(double left, double right) {
    return std::fabs(left - right) <= 0.0001;
}

}  // namespace

int main() {
    const MotionLivePreparedProfile profile = MotionEditorProfile();
    const std::vector<uint8_t> payload = Payload();

    MotionPackagePlayer player;
    CHECK(player.Load(profile, RecordFor("runner-001", payload)).ok);

    {
        MotionPackageRunner runner;
        MotionPackageRunnerResult result =
            runner.Start(player, 7, "short", 1000);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_session_id");
        CHECK(!runner.running());
    }

    {
        MotionPackageRunner runner;
        MotionPackagePlayer empty_player;
        MotionPackageRunnerResult result =
            runner.Start(empty_player, 7, "run-session-0001", 1000);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_not_loaded");
        CHECK(!runner.running());
    }

    {
        MotionPackagePlayer nonzero_player;
        const std::vector<uint8_t> nonzero_payload = Payload(5.0);
        CHECK(nonzero_player.Load(profile, RecordFor("nonzero-start", nonzero_payload)).ok);
        MotionPackageRunner runner;
        MotionPackageRunnerResult result =
            runner.Start(nonzero_player, 7, "run-session-0005", 1000);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_start_pose");
        CHECK(!runner.running());
    }

    {
        MotionPackageRunner runner;
        MotionPackageRunnerResult result =
            runner.Start(player, 7, "run-session-0001", 1000);
        CHECK(result.ok);
        CHECK(runner.running());
        CHECK(result.run_session_id == "run-session-0001");
        CHECK(std::string(result.sample.package_id) == "runner-001");
        CHECK(result.sample.elapsed_ms == 0);
        CHECK(!result.sample.finished);
        CHECK(Near(JointValue(result, "arm_positive_x"), 0.0));

        result = runner.Start(player, 7, "run-session-0002", 1100);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_busy");
        CHECK(runner.running());

        result = runner.Tick(player, 8, "run-session-0001", 3000);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_not_owner");
        CHECK(runner.running());

        result = runner.Tick(player, 7, "run-session-0001", 3000);
        CHECK(result.ok);
        CHECK(result.sample.elapsed_ms == 2000);
        CHECK(Near(JointValue(result, "arm_positive_x"), 10.0));
        CHECK(Near(JointValue(result, "leg_negative_x"), -4.0));

        result = runner.Stop(8, "run-session-0001");
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_not_owner");
        CHECK(runner.running());

        result = runner.Stop(7, "run-session-0001");
        CHECK(result.ok);
        CHECK(result.stopped);
        CHECK(!runner.running());

        result = runner.Tick(player, 7, "run-session-0001", 4000);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_not_active");
    }

    {
        MotionPackageRunner runner;
        MotionPackageRunnerResult result =
            runner.Start(player, 7, "run-session-0003", 1000);
        CHECK(result.ok);
        result = runner.Tick(player, 7, "run-session-0003", 10000);
        CHECK(result.ok);
        CHECK(result.stopped);
        CHECK(result.sample.finished);
        CHECK(result.sample.elapsed_ms == 8000);
        CHECK(Near(JointValue(result, "arm_positive_x"), 40.0));
        CHECK(!runner.running());
    }

    {
        MotionPackageRunner runner;
        MotionPackageRunnerResult result =
            runner.Start(player, 7, "run-session-0004", 1000);
        CHECK(result.ok);
        player.Clear();
        result = runner.Tick(player, 7, "run-session-0004", 1500);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_run_package_changed");
        CHECK(!runner.running());
    }

    std::cout << "motion_package_runner_host_test: PASS\n";
    return 0;
}

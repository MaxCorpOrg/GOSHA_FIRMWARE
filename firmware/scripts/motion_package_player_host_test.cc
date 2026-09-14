#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_player.h"
#include "../main/boards/gosha-v1/motion_package_upload.h"

using gosha::motion_live::FindJointIndexById;
using gosha::motion_live::JointIndex;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageLoadedRecord;
using gosha::motion_live::MotionPackagePlayer;
using gosha::motion_live::MotionPackageSample;

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

std::vector<uint8_t> PayloadWithInterpolation(const std::string& interpolation,
                                              double right_arm = 40.0) {
    const std::string json =
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"source_motion_id\":\"sample\","
        "\"name\":\"sample\","
        "\"profile_id\":\"gosha-preview-v1\","
        "\"profile_version\":1,"
        "\"calibration_id\":\"" + std::string(kCalibration) + "\","
        "\"units\":\"relative_degrees\","
        "\"source_preview_only\":true,"
        "\"hardware_validated\":false,"
        "\"live_compatible\":true,"
        "\"robot_storage_implemented\":false,"
        "\"duration_ms\":8000,"
        "\"interpolation\":\"" + interpolation + "\","
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
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":" +
        std::to_string(right_arm) +
        ",\"leg_negative_x\":-16,\"leg_positive_x\":0,"
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

bool Near(double left, double right) {
    return std::fabs(left - right) <= 0.0001;
}

double JointValue(const MotionPackageSample& sample, const char* id) {
    const int index = FindJointIndexById(id);
    if (index < 0) {
        return 0.0;
    }
    return sample.target.relative_degrees[index];
}

bool JointPresent(const MotionPackageSample& sample, const char* id) {
    const int index = FindJointIndexById(id);
    return index >= 0 && sample.target.present[index];
}

}  // namespace

int main() {
    const MotionLivePreparedProfile profile = MotionEditorProfile();

    {
        const std::vector<uint8_t> payload = PayloadWithInterpolation("linear");
        MotionPackagePlayer player;
        auto result = player.Load(profile, RecordFor("linear-001", payload));
        CHECK(result.ok);
        CHECK(player.loaded());
        CHECK(player.package_id() == "linear-001");
        CHECK(player.duration_ms() == 8000);

        MotionPackageSample sample;
        result = player.Sample(2000, &sample);
        CHECK(result.ok);
        CHECK(!sample.finished);
        CHECK(std::string(sample.package_id) == "linear-001");
        CHECK(JointPresent(sample, "arm_positive_x"));
        CHECK(!JointPresent(sample, "arm_negative_x"));
        CHECK(Near(JointValue(sample, "arm_positive_x"), 10.0));
        CHECK(Near(JointValue(sample, "leg_negative_x"), -4.0));

        result = player.Sample(9000, &sample);
        CHECK(result.ok);
        CHECK(sample.finished);
        CHECK(sample.elapsed_ms == 8000);
        CHECK(Near(JointValue(sample, "arm_positive_x"), 40.0));
        CHECK(Near(JointValue(sample, "foot_positive_x"), 8.0));
    }

    {
        const std::vector<uint8_t> payload = PayloadWithInterpolation("smooth");
        MotionPackagePlayer player;
        CHECK(player.Load(profile, RecordFor("smooth-001", payload)).ok);
        MotionPackageSample sample;
        CHECK(player.Sample(2000, &sample).ok);
        CHECK(Near(JointValue(sample, "arm_positive_x"), 6.25));
        CHECK(Near(JointValue(sample, "leg_negative_x"), -2.5));
    }

    {
        const std::vector<uint8_t> payload = PayloadWithInterpolation("linear", 55.0);
        MotionPackagePlayer player;
        const auto result = player.Load(profile, RecordFor("bad-arm", payload));
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "target_out_of_range");
        CHECK(!player.loaded());
    }

    {
        const std::vector<uint8_t> payload = PayloadWithInterpolation("linear");
        MotionPackageLoadedRecord record = RecordFor("wrong-calibration", payload);
        record.calibration_id = std::string(64, 'b');
        MotionPackagePlayer player;
        const auto result = player.Load(profile, record);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_profile_mismatch");
    }

    {
        const std::vector<uint8_t> payload = PayloadWithInterpolation("linear");
        MotionPackageLoadedRecord record = RecordFor("bad-crc", payload);
        record.payload_crc32 ^= 1u;
        MotionPackagePlayer player;
        const auto result = player.Load(profile, record);
        CHECK(!result.ok);
        CHECK(std::string(result.code) == "package_crc32");
    }

    std::cout << "motion_package_player_host_test: PASS\n";
    return 0;
}

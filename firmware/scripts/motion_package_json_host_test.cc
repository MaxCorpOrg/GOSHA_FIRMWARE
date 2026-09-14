#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_json.h"

using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageOwnedDraft;
using gosha::motion_live::ParseMotionPackageDraftJson;
using gosha::motion_live::ValidateMotionPackageDraft;

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

std::vector<uint8_t> PayloadWithEdits(const std::string& edits) {
    const std::string json =
        std::string(
        "{\"schema_version\":1,"
        "\"package_type\":\"gosha.motion.robot-package-draft.v1\","
        "\"name\":\"Parser Test\","
        "\"profile_id\":\"gosha-preview-v1\","
        "\"profile_version\":1,"
        "\"calibration_id\":\"") +
        kCalibration +
        "\","
        "\"source_preview_only\":true,"
        "\"hardware_validated\":false,"
        "\"live_compatible\":true,"
        "\"robot_storage_implemented\":false,"
        "\"duration_ms\":8000,"
        "\"interpolation\":\"smooth\","
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
        "{\"time_ms\":8000,\"target\":{\"arm_positive_x\":40,\"leg_negative_x\":-12,"
        "\"leg_positive_x\":0,\"foot_negative_x\":0,\"foot_positive_x\":8}}]" +
        edits + "}";
    return std::vector<uint8_t>(json.begin(), json.end());
}

std::vector<uint8_t> ValidPayload() {
    return PayloadWithEdits("");
}

bool CodeIs(const char* actual, const char* expected) {
    return std::string(actual) == expected;
}

}  // namespace

int main() {
    MotionLivePreparedProfile profile = MotionEditorProfile();

    {
        const std::vector<uint8_t> payload = ValidPayload();
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(parse.ok);
        CHECK(CodeIs(parsed.draft.name, "Parser Test"));
        CHECK(parsed.draft.keyframe_count == 2);
        CHECK(parsed.draft.keyframes[1].target_count == 5);
        CHECK(CodeIs(parsed.draft.keyframes[1].targets[0].id, "arm_positive_x"));
        CHECK(parsed.draft.keyframes[1].targets[0].relative_degrees == 40.0);
        auto validation = ValidateMotionPackageDraft(profile, parsed.draft);
        CHECK(validation.ok);
    }

    {
        std::string json = "{\"schema_version\":1} trailing";
        std::vector<uint8_t> payload(json.begin(), json.end());
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(!parse.ok);
        CHECK(CodeIs(parse.code, "package_json_parse"));
    }

    {
        std::string json =
            "{\"schema_version\":1,"
            "\"package_type\":\"gosha.motion.robot-package-draft.v1\"}";
        std::vector<uint8_t> payload(json.begin(), json.end());
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(!parse.ok);
        CHECK(CodeIs(parse.code, "package_json_header"));
    }

    {
        std::vector<uint8_t> payload = ValidPayload();
        std::string text(payload.begin(), payload.end());
        const std::string from = "\"arm_positive_x\":40";
        const std::string to = "\"arm_positive_x\":55";
        text.replace(text.find(from), from.size(), to);
        payload.assign(text.begin(), text.end());
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(parse.ok);
        auto validation = ValidateMotionPackageDraft(profile, parsed.draft);
        CHECK(!validation.ok);
        CHECK(CodeIs(validation.code, "target_out_of_range"));
        CHECK(CodeIs(validation.joint_id, "arm_positive_x"));
    }

    {
        std::vector<uint8_t> payload = ValidPayload();
        std::string text(payload.begin(), payload.end());
        const std::string from = "\"arm_positive_x\":40";
        const std::string to = "\"arm_negative_x\":10";
        text.replace(text.find(from), from.size(), to);
        payload.assign(text.begin(), text.end());
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(parse.ok);
        auto validation = ValidateMotionPackageDraft(profile, parsed.draft);
        CHECK(!validation.ok);
        CHECK(CodeIs(validation.code, "target_inactive"));
        CHECK(CodeIs(validation.joint_id, "arm_negative_x"));
    }

    {
        std::vector<uint8_t> payload = ValidPayload();
        std::string text(payload.begin(), payload.end());
        const std::string from = "\"interpolation\":\"smooth\"";
        const std::string to = "\"interpolation\":\"hold\"";
        text.replace(text.find(from), from.size(), to);
        payload.assign(text.begin(), text.end());
        MotionPackageOwnedDraft parsed;
        auto parse = ParseMotionPackageDraftJson(payload.data(), payload.size(), &parsed);
        CHECK(parse.ok);
        auto validation = ValidateMotionPackageDraft(profile, parsed.draft);
        CHECK(!validation.ok);
        CHECK(CodeIs(validation.code, "package_interpolation"));
    }

    std::cout << "motion_package_json_host_test: PASS\n";
    return 0;
}

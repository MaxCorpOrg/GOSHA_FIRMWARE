#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_package_upload.h"

using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionPackageCrc32;
using gosha::motion_live::MotionPackageUploadBegin;
using gosha::motion_live::MotionPackageUploadChunk;
using gosha::motion_live::MotionPackageUploadSession;
using gosha::motion_live::kMotionPackageUploadMaxBytes;
using gosha::motion_live::kMotionPackageUploadMaxChunkBytes;

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

MotionPackageUploadBegin BeginFor(const std::vector<uint8_t>& payload) {
    return {
        "motion-001",
        "gosha-preview-v1",
        kCalibration,
        static_cast<uint32_t>(payload.size()),
        MotionPackageCrc32(payload.data(), payload.size()),
    };
}

bool CodeIs(const char* actual, const char* expected) {
    return std::string(actual) == expected;
}

}  // namespace

int main() {
    const std::string json =
        R"({"package_type":"gosha.motion.robot-package-draft.v1","duration_ms":8000})";
    const std::vector<uint8_t> payload(json.begin(), json.end());
    MotionLivePreparedProfile profile = MotionEditorProfile();

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        CHECK(session.active());
        CHECK(session.expected_size() == payload.size());
        result = session.Append({0, payload.data(), 10});
        CHECK(result.ok);
        result = session.Append({10, payload.data() + 10, payload.size() - 10});
        CHECK(result.ok);
        std::vector<uint8_t> output;
        result = session.Finish(&output);
        CHECK(result.ok);
        CHECK(!session.active());
        CHECK(output == payload);
    }

    {
        MotionLivePreparedProfile wrong_mode = profile;
        wrong_mode.mode = "verified";
        MotionPackageUploadSession session;
        auto result = session.Begin(wrong_mode, BeginFor(payload));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "profile_not_motion_editor"));
    }

    {
        MotionPackageUploadSession session;
        MotionPackageUploadBegin begin = BeginFor(payload);
        begin.package_id = "../bad";
        auto result = session.Begin(profile, begin);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_package_id"));
    }

    {
        MotionPackageUploadSession session;
        MotionPackageUploadBegin begin = BeginFor(payload);
        begin.package_id = ".";
        auto result = session.Begin(profile, begin);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_package_id"));
        begin.package_id = "..";
        result = session.Begin(profile, begin);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_package_id"));
    }

    {
        MotionLivePreparedProfile wrong_joint = profile;
        wrong_joint.joints[4].id = "arm_negative_x";
        MotionPackageUploadSession session;
        auto result = session.Begin(wrong_joint, BeginFor(payload));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "profile_not_motion_editor"));
    }

    {
        MotionLivePreparedProfile wrong_range = profile;
        wrong_range.joints[4].max_relative_degrees = 55.0;
        MotionPackageUploadSession session;
        auto result = session.Begin(wrong_range, BeginFor(payload));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "profile_not_motion_editor"));
    }

    {
        MotionPackageUploadSession session;
        MotionPackageUploadBegin begin = BeginFor(payload);
        begin.total_size = static_cast<uint32_t>(kMotionPackageUploadMaxBytes + 1);
        auto result = session.Begin(profile, begin);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_size"));
    }

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        result = session.Begin(profile, BeginFor(payload));
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_active"));
    }

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        result = session.Append({1, payload.data(), 4});
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_offset"));
        CHECK(result.expected_offset == 0);
    }

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        std::vector<uint8_t> too_large(kMotionPackageUploadMaxChunkBytes + 1, 'x');
        result = session.Append({0, too_large.data(), too_large.size()});
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_chunk_size"));
    }

    {
        CHECK(kMotionPackageUploadMaxChunkBytes == 2048);
        const size_t base64_worst_case =
            ((kMotionPackageUploadMaxChunkBytes + 2) / 3) * 4;
        CHECK(base64_worst_case + 512 < 4096);
    }

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        result = session.Append({0, payload.data(), payload.size() - 1});
        CHECK(result.ok);
        std::vector<uint8_t> output;
        result = session.Finish(&output);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_incomplete"));
        CHECK(session.active());
    }

    {
        MotionPackageUploadSession session;
        MotionPackageUploadBegin begin = BeginFor(payload);
        begin.crc32 ^= 0x1000u;
        auto result = session.Begin(profile, begin);
        CHECK(result.ok);
        result = session.Append({0, payload.data(), payload.size()});
        CHECK(result.ok);
        std::vector<uint8_t> output;
        result = session.Finish(&output);
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_crc32"));
        CHECK(!session.active());
        CHECK(output.empty());
    }

    {
        MotionPackageUploadSession session;
        auto result = session.Begin(profile, BeginFor(payload));
        CHECK(result.ok);
        session.Abort();
        CHECK(!session.active());
        result = session.Append({0, payload.data(), payload.size()});
        CHECK(!result.ok);
        CHECK(CodeIs(result.code, "upload_not_started"));
    }

    std::cout << "motion_package_upload_host_test: PASS\n";
    return 0;
}

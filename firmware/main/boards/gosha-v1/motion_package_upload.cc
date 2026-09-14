#include "motion_package_upload.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>

namespace gosha::motion_live {

namespace {

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
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

bool IsSafePackageId(const char* value) {
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    size_t length = 0;
    for (; value[length] != '\0'; ++length) {
        if (length >= kMotionPackageUploadMaxIdBytes) {
            return false;
        }
        const auto ch = static_cast<unsigned char>(value[length]);
        if (!std::isalnum(ch) && value[length] != '-' && value[length] != '_' &&
            value[length] != '.') {
            return false;
        }
    }
    if (length == 1 && value[0] == '.') {
        return false;
    }
    if (length == 2 && value[0] == '.' && value[1] == '.') {
        return false;
    }
    return length > 0;
}

bool AlmostEqual(double left, double right) {
    return std::fabs(left - right) <= 0.000001;
}

struct ExpectedUploadJoint {
    const char* id;
    double min_relative_degrees;
    double max_relative_degrees;
};

constexpr std::array<ExpectedUploadJoint, kMaxActiveJointCount> kExpectedUploadJoints = {{
    {"arm_positive_x", -70.0, 45.0},
    {"leg_negative_x", -35.0, 35.0},
    {"leg_positive_x", -35.0, 35.0},
    {"foot_negative_x", -30.0, 30.0},
    {"foot_positive_x", -30.0, 30.0},
}};

bool CurrentUploadProfileMatches(const MotionLivePreparedProfile& profile) {
    if (!Streq(profile.mode, kProfileModeMotionEditor) ||
        !Streq(profile.profile_id, kModelProfileId) ||
        !IsHex64(profile.calibration_id) ||
        profile.joint_count != kMaxActiveJointCount) {
        return false;
    }
    std::array<bool, kMaxActiveJointCount> seen{};
    seen.fill(false);
    for (int profile_index = 0; profile_index < profile.joint_count; ++profile_index) {
        const auto& joint = profile.joints[profile_index];
        int expected_index = -1;
        for (int i = 0; i < kMaxActiveJointCount; ++i) {
            if (Streq(joint.id, kExpectedUploadJoints[i].id)) {
                expected_index = i;
                break;
            }
        }
        if (expected_index < 0 || seen[expected_index]) {
            return false;
        }
        const auto& expected = kExpectedUploadJoints[expected_index];
        if (!AlmostEqual(joint.min_relative_degrees, expected.min_relative_degrees) ||
            !AlmostEqual(joint.max_relative_degrees, expected.max_relative_degrees)) {
            return false;
        }
        seen[expected_index] = true;
    }
    for (bool found : seen) {
        if (!found) {
            return false;
        }
    }
    return true;
}

MotionPackageUploadResult Error(const char* code, uint32_t expected_offset = 0,
                                uint32_t value = 0, uint32_t limit = 0) {
    MotionPackageUploadResult result;
    result.ok = false;
    result.code = code;
    result.expected_offset = expected_offset;
    result.value = value;
    result.limit = limit;
    return result;
}

MotionPackageUploadResult Ok() {
    MotionPackageUploadResult result;
    result.ok = true;
    result.code = "ok";
    return result;
}

}  // namespace

uint32_t MotionPackageCrc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

MotionPackageUploadResult MotionPackageUploadSession::Begin(
    const MotionLivePreparedProfile& profile,
    const MotionPackageUploadBegin& begin) {
    if (active_) {
        return Error("upload_active", received_size(), received_size(), expected_size_);
    }
    if (!CurrentUploadProfileMatches(profile)) {
        return Error("profile_not_motion_editor");
    }
    if (!IsSafePackageId(begin.package_id)) {
        return Error("upload_package_id");
    }
    if (!Streq(begin.profile_id, profile.profile_id) ||
        !Streq(begin.calibration_id, profile.calibration_id)) {
        return Error("upload_profile_mismatch");
    }
    if (begin.total_size == 0 || begin.total_size > kMotionPackageUploadMaxBytes) {
        return Error("upload_size", 0, begin.total_size,
                     static_cast<uint32_t>(kMotionPackageUploadMaxBytes));
    }

    package_id_ = begin.package_id;
    calibration_id_ = begin.calibration_id;
    expected_size_ = begin.total_size;
    expected_crc32_ = begin.crc32;
    buffer_.clear();
    buffer_.reserve(expected_size_);
    active_ = true;
    return Ok();
}

MotionPackageUploadResult MotionPackageUploadSession::Append(
    const MotionPackageUploadChunk& chunk) {
    if (!active_) {
        return Error("upload_not_started");
    }
    const uint32_t received = received_size();
    if (chunk.offset != received) {
        return Error("upload_offset", received, chunk.offset, expected_size_);
    }
    if (chunk.data == nullptr || chunk.size == 0) {
        return Error("upload_chunk_empty", received);
    }
    if (chunk.size > kMotionPackageUploadMaxChunkBytes) {
        return Error("upload_chunk_size", received,
                     static_cast<uint32_t>(chunk.size),
                     static_cast<uint32_t>(kMotionPackageUploadMaxChunkBytes));
    }
    if (chunk.size > expected_size_ - received) {
        return Error("upload_overflow", received,
                     received + static_cast<uint32_t>(chunk.size),
                     expected_size_);
    }
    buffer_.insert(buffer_.end(), chunk.data, chunk.data + chunk.size);
    return Ok();
}

MotionPackageUploadResult MotionPackageUploadSession::Finish(
    std::vector<uint8_t>* payload) {
    if (!active_) {
        return Error("upload_not_started");
    }
    const uint32_t received = received_size();
    if (received != expected_size_) {
        return Error("upload_incomplete", received, received, expected_size_);
    }
    const uint32_t actual_crc = MotionPackageCrc32(buffer_.data(), buffer_.size());
    if (actual_crc != expected_crc32_) {
        Abort();
        return Error("upload_crc32", expected_size_, actual_crc, expected_crc32_);
    }
    if (payload != nullptr) {
        *payload = buffer_;
    }
    Abort();
    return Ok();
}

void MotionPackageUploadSession::Abort() {
    active_ = false;
    package_id_.clear();
    calibration_id_.clear();
    expected_size_ = 0;
    expected_crc32_ = 0;
    buffer_.clear();
    buffer_.shrink_to_fit();
}

}  // namespace gosha::motion_live

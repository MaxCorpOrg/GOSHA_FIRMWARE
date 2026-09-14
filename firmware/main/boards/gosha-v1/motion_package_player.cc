#include "motion_package_player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "motion_package_upload.h"

namespace gosha::motion_live {

namespace {

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

MotionPackagePlayerResult Error(const char* code,
                                int frame_index = -1,
                                const char* joint_id = "") {
    MotionPackagePlayerResult result;
    result.ok = false;
    result.code = code;
    result.frame_index = frame_index;
    result.joint_id = joint_id == nullptr ? "" : joint_id;
    return result;
}

MotionPackagePlayerResult Ok() {
    MotionPackagePlayerResult result;
    result.ok = true;
    result.code = "ok";
    return result;
}

double SmoothStep(double mix) {
    return mix * mix * (3.0 - 2.0 * mix);
}

}  // namespace

void MotionPackagePlayer::Clear() {
    draft_ = MotionPackageOwnedDraft{};
    package_id_.clear();
    loaded_ = false;
}

MotionPackagePlayerResult MotionPackagePlayer::Load(
    const MotionLivePreparedProfile& profile,
    const MotionPackageLoadedRecord& record) {
    Clear();
    if (!Streq(record.profile_id.c_str(), profile.profile_id) ||
        !Streq(record.calibration_id.c_str(), profile.calibration_id)) {
        return Error("package_profile_mismatch");
    }
    if (record.payload.empty() ||
        MotionPackageCrc32(record.payload.data(), record.payload.size()) !=
            record.payload_crc32) {
        return Error("package_crc32");
    }

    MotionPackageOwnedDraft parsed;
    const MotionPackageJsonParseResult parse_result =
        ParseMotionPackageDraftJson(record.payload.data(), record.payload.size(),
                                    &parsed);
    if (!parse_result.ok) {
        return Error(parse_result.code, parse_result.frame_index,
                     parse_result.joint_id);
    }

    const MotionPackageValidationResult validation_result =
        ValidateMotionPackageDraft(profile, parsed.draft);
    if (!validation_result.ok) {
        return Error(validation_result.code, validation_result.frame_index,
                     validation_result.joint_id);
    }

    draft_ = std::move(parsed);
    draft_.BindPointers();
    package_id_ = record.package_id;
    loaded_ = true;
    return Ok();
}

bool MotionPackagePlayer::FrameValue(const MotionPackageKeyframe& frame,
                                     const char* joint_id,
                                     double* value) const {
    if (joint_id == nullptr || value == nullptr) {
        return false;
    }
    for (int i = 0; i < frame.target_count; ++i) {
        if (Streq(frame.targets[i].id, joint_id)) {
            *value = frame.targets[i].relative_degrees;
            return true;
        }
    }
    return false;
}

MotionPackagePlayerResult MotionPackagePlayer::Sample(
    uint32_t elapsed_ms,
    MotionPackageSample* sample) const {
    if (!loaded_ || sample == nullptr || draft_.draft.keyframes == nullptr ||
        draft_.draft.keyframe_count <= 0) {
        return Error("package_not_loaded");
    }

    MotionPackageSample next;
    next.package_id = package_id_.c_str();
    next.elapsed_ms = std::min(elapsed_ms, draft_.draft.duration_ms);
    next.finished = elapsed_ms >= draft_.draft.duration_ms;

    const MotionPackageKeyframe* before = &draft_.draft.keyframes[0];
    const MotionPackageKeyframe* after = before;
    for (int i = 1; i < draft_.draft.keyframe_count; ++i) {
        const MotionPackageKeyframe* frame = &draft_.draft.keyframes[i];
        if (frame->time_ms <= next.elapsed_ms) {
            before = frame;
            after = frame;
            continue;
        }
        after = frame;
        break;
    }

    double mix = 0.0;
    if (after != before && after->time_ms > before->time_ms) {
        mix = static_cast<double>(next.elapsed_ms - before->time_ms) /
              static_cast<double>(after->time_ms - before->time_ms);
        mix = std::clamp(mix, 0.0, 1.0);
        if (Streq(draft_.draft.interpolation, "smooth")) {
            mix = SmoothStep(mix);
        }
    }

    next.target.present.fill(false);
    next.target.relative_degrees.fill(0.0);
    next.target.has_unknown_joint = false;
    for (int i = 0; i < draft_.draft.active_joint_count; ++i) {
        const char* joint_id = draft_.draft.active_joints[i];
        const int joint_index = FindJointIndexById(joint_id);
        if (joint_index < 0) {
            return Error("package_joint_set", -1, joint_id);
        }
        double before_value = 0.0;
        double after_value = 0.0;
        if (!FrameValue(*before, joint_id, &before_value) ||
            !FrameValue(*after, joint_id, &after_value)) {
            return Error("frame_target_set", -1, joint_id);
        }
        next.target.present[joint_index] = true;
        next.target.relative_degrees[joint_index] =
            before_value + (after_value - before_value) * mix;
    }

    *sample = next;
    return Ok();
}

}  // namespace gosha::motion_live

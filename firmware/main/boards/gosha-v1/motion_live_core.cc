#include "motion_live_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace gosha::motion_live {

namespace {

constexpr const char* kOk = "ok";
constexpr const char* kNoMotionProfile = "no_motion_profile";
constexpr const char* kLiveProfileUnprepared = "live_profile_unprepared";
constexpr const char* kProfileMismatch = "profile_mismatch";
constexpr const char* kRuntimeNotSafeNeutral = "runtime_not_safe_neutral";
constexpr const char* kAuthFailed = "auth_failed";
constexpr const char* kSessionNotOwner = "session_not_owner";
constexpr const char* kSessionBusy = "session_busy";
constexpr const char* kBadSeq = "bad_seq";
constexpr const char* kBadTarget = "bad_target";
constexpr const char* kLimitViolation = "limit_violation";
constexpr const char* kRateLimit = "rate_limit";
constexpr const char* kWatchdogTimeout = "watchdog_timeout";
constexpr const char* kWatchdogUnavailable = "watchdog_unavailable";
constexpr const char* kHardwareApplyFailed = "hardware_apply_failed";

bool Streq(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

bool IsHex64(const char* value) {
    if (value == nullptr) {
        return false;
    }
    for (int i = 0; i < 64; ++i) {
        const char c = value[i];
        const bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!is_hex) {
            return false;
        }
    }
    return value[64] == '\0';
}

bool IsFinite(double value) {
    return std::isfinite(value);
}

bool IsLowerBodyServoIndex(int servo_index) {
    return servo_index >= 0 && servo_index < kActiveJointCount;
}

int ServoIndexForKey(const char* servo_key) {
    if (Streq(servo_key, "left_leg")) {
        return static_cast<int>(ServoSlot::kLeftLeg);
    }
    if (Streq(servo_key, "right_leg")) {
        return static_cast<int>(ServoSlot::kRightLeg);
    }
    if (Streq(servo_key, "left_foot")) {
        return static_cast<int>(ServoSlot::kLeftFoot);
    }
    if (Streq(servo_key, "right_foot")) {
        return static_cast<int>(ServoSlot::kRightFoot);
    }
    return -1;
}

const char* ServoGroupForKey(const char* servo_key) {
    if (Streq(servo_key, "left_leg") || Streq(servo_key, "right_leg")) {
        return "leg";
    }
    if (Streq(servo_key, "left_foot") || Streq(servo_key, "right_foot")) {
        return "foot";
    }
    return "";
}

int ExpectedPinForServoIndex(int servo_index) {
    switch (static_cast<ServoSlot>(servo_index)) {
        case ServoSlot::kLeftLeg:
            return 17;
        case ServoSlot::kRightLeg:
            return 39;
        case ServoSlot::kLeftFoot:
            return 18;
        case ServoSlot::kRightFoot:
            return 38;
        default:
            return -1;
    }
}

int RoundServoDegree(double value) {
    return static_cast<int>(std::lround(value));
}

}  // namespace

const std::array<JointSpec, kPoseJointCount> kJointSpecs = {{
    {"arm_negative_x", "arm", -70, 70, false},
    {"arm_positive_x", "arm", -70, 70, false},
    {"leg_negative_x", "leg", -35, 35, true},
    {"leg_positive_x", "leg", -35, 35, true},
    {"foot_negative_x", "foot", -30, 30, true},
    {"foot_positive_x", "foot", -30, 30, true},
}};

int FindJointIndexById(const char* id) {
    if (id == nullptr) {
        return -1;
    }
    for (int i = 0; i < kPoseJointCount; ++i) {
        if (Streq(kJointSpecs[i].id, id)) {
            return i;
        }
    }
    return -1;
}

MotionLiveCore::MotionLiveCore() {
    commanded_pose_.relative_degrees.fill(0.0);
    fractional_pose_.relative_degrees.fill(0.0);
    target_pose_.relative_degrees.fill(0.0);
    commanded_servo_degrees_.fill(kNeutralDegrees);
    for (auto& runtime_joint : runtime_.joints) {
        runtime_joint.commanded_degrees = kNeutralDegrees;
    }
}

void MotionLiveCore::SetLocalOptInEnabled(bool enabled) {
    local_opt_in_enabled_ = enabled;
}

void MotionLiveCore::SetPreparedProfile(const MotionLivePreparedProfile* profile) {
    profile_ = profile;
}

void MotionLiveCore::SetRuntimeConfig(const MotionLiveRuntimeConfig& runtime) {
    runtime_ = runtime;
}

void MotionLiveCore::SetHardwareApplier(MotionLiveHardwareApplier applier) {
    hardware_applier_ = std::move(applier);
}

bool MotionLiveCore::ValidatePreparedProfile(const char** reason) const {
    if (!local_opt_in_enabled_ || profile_ == nullptr) {
        *reason = kLiveProfileUnprepared;
        return false;
    }
    if (!Streq(profile_->profile_id, kModelProfileId) ||
        !IsHex64(profile_->calibration_id) ||
        !IsHex64(profile_->access_key_sha256) ||
        profile_->watchdog_ms != kWatchdogMs ||
        profile_->max_rate_hz < kMinMaxRateHz ||
        profile_->max_rate_hz > kMaxMaxRateHz ||
        1000 / profile_->max_rate_hz > profile_->watchdog_ms / 2) {
        *reason = kProfileMismatch;
        return false;
    }

    std::array<bool, kActiveJointCount> seen_servos{};
    std::array<bool, kPoseJointCount> seen_joints{};
    seen_servos.fill(false);
    seen_joints.fill(false);
    for (const auto& joint : profile_->joints) {
        const int joint_index = FindJointIndexById(joint.id);
        if (joint_index < 0 || !kJointSpecs[joint_index].active) {
            *reason = kProfileMismatch;
            return false;
        }
        const JointSpec& spec = kJointSpecs[joint_index];
        const int expected_servo_index = ServoIndexForKey(joint.servo_key);
        if (seen_joints[joint_index] ||
            expected_servo_index < 0 ||
            joint.servo_index != expected_servo_index ||
            !Streq(ServoGroupForKey(joint.servo_key), spec.servo_group) ||
            !IsLowerBodyServoIndex(joint.servo_index) ||
            seen_servos[joint.servo_index] ||
            joint.pin != ExpectedPinForServoIndex(joint.servo_index) ||
            joint.neutral_degrees != kNeutralDegrees ||
            (joint.direction != 1 && joint.direction != -1) ||
            !IsFinite(joint.min_relative_degrees) ||
            !IsFinite(joint.max_relative_degrees) ||
            joint.min_relative_degrees >= joint.max_relative_degrees ||
            joint.min_relative_degrees < spec.ui_min ||
            joint.max_relative_degrees > spec.ui_max ||
            joint.min_relative_degrees > 0.0 ||
            joint.max_relative_degrees < 0.0 ||
            joint.min_servo_degrees < 0 ||
            joint.max_servo_degrees > 180 ||
            joint.min_servo_degrees >= joint.max_servo_degrees ||
            joint.max_speed_dps <= 0.0 ||
            joint.max_speed_dps > kMaxServoRateDps) {
            *reason = kProfileMismatch;
            return false;
        }

        const double servo_at_min =
            joint.neutral_degrees + joint.direction * joint.min_relative_degrees;
        const double servo_at_max =
            joint.neutral_degrees + joint.direction * joint.max_relative_degrees;
        const double lowest = std::min(servo_at_min, servo_at_max);
        const double highest = std::max(servo_at_min, servo_at_max);
        if (lowest < joint.min_servo_degrees || highest > joint.max_servo_degrees ||
            joint.neutral_degrees < joint.min_servo_degrees ||
            joint.neutral_degrees > joint.max_servo_degrees) {
            *reason = kProfileMismatch;
            return false;
        }
        seen_servos[joint.servo_index] = true;
        seen_joints[joint_index] = true;
    }

    if (!std::all_of(seen_servos.begin(), seen_servos.end(), [](bool value) { return value; }) ||
        !seen_joints[static_cast<int>(JointIndex::kLegNegativeX)] ||
        !seen_joints[static_cast<int>(JointIndex::kLegPositiveX)] ||
        !seen_joints[static_cast<int>(JointIndex::kFootNegativeX)] ||
        !seen_joints[static_cast<int>(JointIndex::kFootPositiveX)]) {
        *reason = kProfileMismatch;
        return false;
    }
    return true;
}

bool MotionLiveCore::ValidateRuntimeAgainstProfile(const char** reason) const {
    if (!runtime_.board_is_gosha_v1 ||
        !runtime_.no_motion_safe_profile ||
        !runtime_.safe_neutral_boot_profile) {
        *reason = kNoMotionProfile;
        return false;
    }
    if (!runtime_.safe_neutral_commanded || !runtime_.lower_body_attached) {
        *reason = kRuntimeNotSafeNeutral;
        return false;
    }
    if (!runtime_.watchdog_tick_ready) {
        *reason = kWatchdogUnavailable;
        return false;
    }

    const auto& left_hand =
        runtime_.joints[static_cast<int>(ServoSlot::kLeftHand)];
    const auto& right_hand =
        runtime_.joints[static_cast<int>(ServoSlot::kRightHand)];
    if (left_hand.pin != -1 || right_hand.pin != -1 ||
        left_hand.attached || right_hand.attached) {
        *reason = kRuntimeNotSafeNeutral;
        return false;
    }

    for (const auto& profile_joint : profile_->joints) {
        if (!IsLowerBodyServoIndex(profile_joint.servo_index)) {
            *reason = kProfileMismatch;
            return false;
        }
        const auto& runtime_joint = runtime_.joints[profile_joint.servo_index];
        if (runtime_joint.pin != profile_joint.pin ||
            runtime_joint.trim != profile_joint.trim ||
            runtime_joint.commanded_degrees != profile_joint.neutral_degrees ||
            !runtime_joint.attached) {
            *reason = kRuntimeNotSafeNeutral;
            return false;
        }
    }
    return true;
}

const char* MotionLiveCore::EvaluateSafety() const {
    const char* reason = kOk;
    if (!ValidatePreparedProfile(&reason)) {
        return reason;
    }
    if (!ValidateRuntimeAgainstProfile(&reason)) {
        return reason;
    }
    return kOk;
}

MotionLiveCapabilities MotionLiveCore::GetCapabilities() const {
    MotionLiveCapabilities caps;
    caps.commanded_pose = commanded_pose_;

    const char* reason = EvaluateSafety();
    if (!Streq(reason, kOk)) {
        caps.motion_allowed = false;
        caps.reason = reason;
        return caps;
    }

    caps.motion_allowed = true;
    caps.reason = kOk;
    caps.calibrated = true;
    caps.profile_id = profile_->profile_id;
    caps.calibration_id = profile_->calibration_id;
    caps.watchdog_ms = profile_->watchdog_ms;
    caps.max_rate_hz = profile_->max_rate_hz;
    for (int i = 0; i < kActiveJointCount; ++i) {
        caps.joint_limits[i] = {
            profile_->joints[i].id,
            profile_->joints[i].min_relative_degrees,
            profile_->joints[i].max_relative_degrees,
            profile_->joints[i].max_speed_dps,
        };
    }
    return caps;
}

MotionLiveResult MotionLiveCore::MakeError(const char* code, const char* message) const {
    MotionLiveResult result;
    result.ok = false;
    result.code = code;
    result.message = message;
    result.session_id = session_id_;
    result.seq = last_seq_;
    result.commanded_pose = commanded_pose_;
    result.servo_degrees = commanded_servo_degrees_;
    return result;
}

MotionLiveResult MotionLiveCore::MakeAck(uint32_t seq, bool should_apply) const {
    MotionLiveResult result;
    result.ok = true;
    result.code = kOk;
    result.message = kOk;
    result.session_id = session_id_;
    result.seq = seq;
    result.commanded_pose = commanded_pose_;
    result.servo_degrees = commanded_servo_degrees_;
    result.should_apply = should_apply;
    return result;
}

MotionLiveResult MotionLiveCore::DisarmWithError(const char* code, const char* message,
                                                  bool stopped) {
    MotionLiveResult result = MakeError(code, message);
    result.stopped = stopped;
    armed_ = false;
    owner_socket_ = -1;
    session_id_.clear();
    last_seq_ = 0;
    last_command_ms_ = 0;
    return result;
}

bool MotionLiveCore::ValidateOwnerSession(int owner_socket, const std::string& session_id,
                                           const char** reason) const {
    if (!armed_ || owner_socket != owner_socket_ || session_id.empty() ||
        session_id != session_id_) {
        *reason = kSessionNotOwner;
        return false;
    }
    return true;
}

bool MotionLiveCore::ValidateNextSeq(uint32_t seq, const char** reason) const {
    if (seq == 0 || seq != last_seq_ + 1) {
        *reason = kBadSeq;
        return false;
    }
    return true;
}

int MotionLiveCore::ActiveProfileIndexForJoint(int joint_index) const {
    if (profile_ == nullptr || joint_index < 0 || joint_index >= kPoseJointCount) {
        return -1;
    }
    const JointSpec& spec = kJointSpecs[joint_index];
    if (!spec.active) {
        return -1;
    }
    for (int i = 0; i < kActiveJointCount; ++i) {
        if (Streq(profile_->joints[i].id, spec.id)) {
            return i;
        }
    }
    return -1;
}

bool MotionLiveCore::BuildServoDegrees(const MotionLiveTarget& target,
                                        std::array<int, kActiveJointCount>* servo_degrees,
                                        const char** reason) const {
    if (target.has_unknown_joint || servo_degrees == nullptr) {
        *reason = kBadTarget;
        return false;
    }

    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        const bool present = target.present[joint_index];
        const bool active = kJointSpecs[joint_index].active;
        if (present && !active) {
            *reason = kBadTarget;
            return false;
        }
        if (active && !present) {
            *reason = kBadTarget;
            return false;
        }
    }

    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (!kJointSpecs[joint_index].active) {
            continue;
        }
        const int profile_index = ActiveProfileIndexForJoint(joint_index);
        if (profile_index < 0) {
            *reason = kProfileMismatch;
            return false;
        }
        const auto& joint = profile_->joints[profile_index];
        const double relative = target.relative_degrees[joint_index];
        if (!IsFinite(relative) ||
            relative < joint.min_relative_degrees ||
            relative > joint.max_relative_degrees) {
            *reason = kLimitViolation;
            return false;
        }
        const double servo = joint.neutral_degrees + joint.direction * relative;
        if (!IsFinite(servo) ||
            servo < joint.min_servo_degrees ||
            servo > joint.max_servo_degrees ||
            servo < 0.0 ||
            servo > 180.0) {
            *reason = kLimitViolation;
            return false;
        }
        (*servo_degrees)[joint.servo_index] = RoundServoDegree(servo);
    }
    return true;
}

bool MotionLiveCore::BuildServoDegreesForPose(
    const MotionLivePose& pose,
    std::array<int, kActiveJointCount>* servo_degrees,
    const char** reason) const {
    if (servo_degrees == nullptr) {
        *reason = kBadTarget;
        return false;
    }
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (!kJointSpecs[joint_index].active) {
            continue;
        }
        const int profile_index = ActiveProfileIndexForJoint(joint_index);
        if (profile_index < 0) {
            *reason = kProfileMismatch;
            return false;
        }
        const auto& joint = profile_->joints[profile_index];
        const double relative = pose.relative_degrees[joint_index];
        if (!IsFinite(relative) ||
            relative < joint.min_relative_degrees ||
            relative > joint.max_relative_degrees) {
            *reason = kLimitViolation;
            return false;
        }
        const double servo = joint.neutral_degrees + joint.direction * relative;
        if (!IsFinite(servo) ||
            servo < joint.min_servo_degrees ||
            servo > joint.max_servo_degrees ||
            servo < 0.0 ||
            servo > 180.0) {
            *reason = kLimitViolation;
            return false;
        }
        (*servo_degrees)[joint.servo_index] = RoundServoDegree(servo);
    }
    return true;
}

MotionLivePose MotionLiveCore::PoseFromServoDegrees(
    const std::array<int, kActiveJointCount>& servo_degrees) const {
    MotionLivePose pose;
    pose.relative_degrees.fill(0.0);
    if (profile_ == nullptr) {
        return pose;
    }
    for (const auto& joint : profile_->joints) {
        const int joint_index = FindJointIndexById(joint.id);
        if (joint_index >= 0 && IsLowerBodyServoIndex(joint.servo_index)) {
            pose.relative_degrees[joint_index] =
                joint.direction * (servo_degrees[joint.servo_index] - joint.neutral_degrees);
        }
    }
    return pose;
}

bool MotionLiveCore::LeaseExpired(uint64_t now_ms) const {
    return armed_ && now_ms > last_command_ms_ &&
           now_ms - last_command_ms_ > static_cast<uint64_t>(kWatchdogMs);
}

bool MotionLiveCore::StepTowardTarget(uint64_t now_ms, const char** reason,
                                      bool* hardware_changed) {
    if (hardware_changed != nullptr) {
        *hardware_changed = false;
    }
    if (current_speed_dps_ <= 0.0) {
        last_motion_step_ms_ = now_ms;
        return true;
    }
    if (now_ms <= last_motion_step_ms_) {
        return true;
    }

    const double max_delta =
        current_speed_dps_ * static_cast<double>(now_ms - last_motion_step_ms_) / 1000.0;
    last_motion_step_ms_ = now_ms;
    if (max_delta <= 0.0) {
        return true;
    }

    MotionLivePose next_fractional_pose = fractional_pose_;
    bool logical_changed = false;
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (!kJointSpecs[joint_index].active) {
            continue;
        }
        const double current = fractional_pose_.relative_degrees[joint_index];
        const double target = target_pose_.relative_degrees[joint_index];
        const double delta = target - current;
        if (std::abs(delta) <= max_delta) {
            next_fractional_pose.relative_degrees[joint_index] = target;
        } else {
            next_fractional_pose.relative_degrees[joint_index] =
                current + (delta < 0.0 ? -max_delta : max_delta);
        }
        if (next_fractional_pose.relative_degrees[joint_index] != current) {
            logical_changed = true;
        }
    }

    if (!logical_changed) {
        return true;
    }

    std::array<int, kActiveJointCount> next_servo_degrees{};
    if (!BuildServoDegreesForPose(next_fractional_pose, &next_servo_degrees, reason)) {
        return false;
    }

    for (const auto& joint : profile_->joints) {
        const int slot = joint.servo_index;
        const double raw_servo =
            joint.neutral_degrees +
            joint.direction *
                next_fractional_pose.relative_degrees[FindJointIndexById(joint.id)];
        const int current_servo = commanded_servo_degrees_[slot];
        if (raw_servo > current_servo && next_servo_degrees[slot] > current_servo) {
            next_servo_degrees[slot] = std::min(next_servo_degrees[slot], static_cast<int>(std::floor(raw_servo)));
        } else if (raw_servo < current_servo && next_servo_degrees[slot] < current_servo) {
            next_servo_degrees[slot] = std::max(next_servo_degrees[slot], static_cast<int>(std::ceil(raw_servo)));
        } else {
            next_servo_degrees[slot] = current_servo;
        }
    }

    const bool pwm_changed = next_servo_degrees != commanded_servo_degrees_;
    if (pwm_changed && !ApplyHardware(next_servo_degrees)) {
        *reason = kHardwareApplyFailed;
        return false;
    }

    fractional_pose_ = next_fractional_pose;
    commanded_servo_degrees_ = next_servo_degrees;
    commanded_pose_ = PoseFromServoDegrees(commanded_servo_degrees_);
    if (hardware_changed != nullptr) {
        *hardware_changed = pwm_changed;
    }
    return true;
}

bool MotionLiveCore::ApplyHardware(const std::array<int, kActiveJointCount>& servo_degrees) const {
    if (!hardware_applier_) {
        return false;
    }
    return hardware_applier_(servo_degrees);
}

MotionLiveResult MotionLiveCore::Arm(int owner_socket,
                                     const std::string& request_calibration_id,
                                     bool access_key_valid,
                                     const std::string& new_session_id,
                                     uint64_t now_ms) {
    if (armed_) {
        return MakeError(kSessionBusy, "Another Live session is already armed");
    }
    const char* reason = EvaluateSafety();
    if (!Streq(reason, kOk)) {
        return MakeError(reason, "Live motion is not enabled for this runtime/profile");
    }
    if (request_calibration_id != profile_->calibration_id) {
        return MakeError(kProfileMismatch, "Calibration id does not match the prepared profile");
    }
    if (!access_key_valid) {
        return MakeError(kAuthFailed, "Access key was rejected");
    }
    if (new_session_id.size() < 16 || new_session_id.size() > 128) {
        return MakeError(kProfileMismatch, "Generated session id is invalid");
    }

    armed_ = true;
    owner_socket_ = owner_socket;
    session_id_ = new_session_id;
    last_seq_ = 0;
    last_command_ms_ = now_ms;
    last_motion_step_ms_ = now_ms;
    current_speed_dps_ = 0.0;
    fractional_pose_ = commanded_pose_;
    target_pose_ = commanded_pose_;

    MotionLiveResult result = MakeAck(0, false);
    result.session_id = session_id_;
    return result;
}

MotionLiveResult MotionLiveCore::Pose(int owner_socket,
                                      const std::string& session_id,
                                      uint32_t seq,
                                      const MotionLiveTarget& target,
                                      double speed_dps,
                                      uint64_t now_ms) {
    const char* reason = nullptr;
    if (!ValidateOwnerSession(owner_socket, session_id, &reason)) {
        return MakeError(reason, "Socket does not own the Live session");
    }
    if (LeaseExpired(now_ms)) {
        return DisarmWithError(kWatchdogTimeout, "Live watchdog timed out before command", true);
    }
    if (!ValidateNextSeq(seq, &reason)) {
        return DisarmWithError(reason, "Sequence number is not the next command", true);
    }
    if (!IsFinite(speed_dps) || speed_dps <= 0.0 || speed_dps > kMaxServoRateDps) {
        return DisarmWithError(kRateLimit, "Requested speed is outside the live safety limit", true);
    }
    for (const auto& joint : profile_->joints) {
        if (speed_dps > joint.max_speed_dps) {
            return DisarmWithError(kRateLimit, "Requested speed exceeds the prepared joint profile", true);
        }
    }

    std::array<int, kActiveJointCount> servo_degrees{};
    if (!BuildServoDegrees(target, &servo_degrees, &reason)) {
        return DisarmWithError(reason, "Target pose is outside the prepared profile", true);
    }

    MotionLivePose next_target = target_pose_;
    for (int joint_index = 0; joint_index < kPoseJointCount; ++joint_index) {
        if (target.present[joint_index]) {
            next_target.relative_degrees[joint_index] = target.relative_degrees[joint_index];
        }
    }
    target_pose_ = next_target;
    current_speed_dps_ = speed_dps;

    bool hardware_changed = false;
    if (!StepTowardTarget(now_ms, &reason, &hardware_changed)) {
        return DisarmWithError(reason, "Live hardware apply callback failed", true);
    }

    last_seq_ = seq;
    last_command_ms_ = now_ms;
    return MakeAck(seq, hardware_changed);
}

MotionLiveResult MotionLiveCore::Keepalive(int owner_socket,
                                           const std::string& session_id,
                                           uint32_t seq,
                                           uint64_t now_ms) {
    const char* reason = nullptr;
    if (!ValidateOwnerSession(owner_socket, session_id, &reason)) {
        return MakeError(reason, "Socket does not own the Live session");
    }
    if (LeaseExpired(now_ms)) {
        return DisarmWithError(kWatchdogTimeout, "Live watchdog timed out before keepalive", true);
    }
    if (!ValidateNextSeq(seq, &reason)) {
        return DisarmWithError(reason, "Sequence number is not the next command", true);
    }
    bool hardware_changed = false;
    if (!StepTowardTarget(now_ms, &reason, &hardware_changed)) {
        return DisarmWithError(reason, "Live hardware apply callback failed", true);
    }
    last_seq_ = seq;
    last_command_ms_ = now_ms;
    return MakeAck(seq, hardware_changed);
}

MotionLiveResult MotionLiveCore::Stop(int owner_socket,
                                      const std::string& session_id,
                                      uint32_t seq) {
    const char* reason = nullptr;
    if (!ValidateOwnerSession(owner_socket, session_id, &reason)) {
        return MakeError(reason, "Socket does not own the Live session");
    }

    const bool seq_ok = ValidateNextSeq(seq, &reason);
    MotionLiveResult result = MakeAck(seq_ok ? seq : last_seq_, false);
    result.stopped = true;
    if (!seq_ok) {
        result.ok = false;
        result.code = kBadSeq;
        result.message = "Sequence number is not the next command";
    }

    armed_ = false;
    owner_socket_ = -1;
    session_id_.clear();
    last_seq_ = 0;
    last_command_ms_ = 0;
    current_speed_dps_ = 0.0;
    fractional_pose_ = commanded_pose_;
    target_pose_ = commanded_pose_;
    return result;
}

MotionLiveResult MotionLiveCore::Tick(uint64_t now_ms) {
    if (!armed_) {
        return MakeError(kOk, kOk);
    }
    if (now_ms <= last_command_ms_ ||
        now_ms - last_command_ms_ <= static_cast<uint64_t>(kWatchdogMs)) {
        const char* reason = nullptr;
        bool hardware_changed = false;
        if (!StepTowardTarget(now_ms, &reason, &hardware_changed)) {
            return DisarmWithError(reason, "Live hardware apply callback failed", true);
        }
        MotionLiveResult result = MakeAck(last_seq_, hardware_changed);
        result.ok = false;
        result.code = kOk;
        result.message = kOk;
        return result;
    }
    return DisarmWithError(kWatchdogTimeout, "Live watchdog timed out", true);
}

void MotionLiveCore::OnSocketClosed(int owner_socket) {
    if (armed_ && owner_socket == owner_socket_) {
        armed_ = false;
        owner_socket_ = -1;
        session_id_.clear();
        last_seq_ = 0;
        last_command_ms_ = 0;
        current_speed_dps_ = 0.0;
        fractional_pose_ = commanded_pose_;
        target_pose_ = commanded_pose_;
    }
}

}  // namespace gosha::motion_live

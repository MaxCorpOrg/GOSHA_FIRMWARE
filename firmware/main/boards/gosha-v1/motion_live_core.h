#ifndef GOSHA_V1_MOTION_LIVE_CORE_H_
#define GOSHA_V1_MOTION_LIVE_CORE_H_

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace gosha::motion_live {

constexpr const char* kProtocol = "gosha.motion.live.v1";
constexpr const char* kModelProfileId = "gosha-preview-v1";
constexpr int kPoseJointCount = 6;
constexpr int kActiveJointCount = 4;
constexpr int kWatchdogMs = 300;
constexpr int kMinMaxRateHz = 5;
constexpr int kMaxMaxRateHz = 20;
constexpr int kMaxServoRateDps = 30;
constexpr int kNeutralDegrees = 90;

enum class JointIndex : int {
    kArmNegativeX = 0,
    kArmPositiveX = 1,
    kLegNegativeX = 2,
    kLegPositiveX = 3,
    kFootNegativeX = 4,
    kFootPositiveX = 5,
};

enum class ServoSlot : int {
    kLeftLeg = 0,
    kRightLeg = 1,
    kLeftFoot = 2,
    kRightFoot = 3,
    kLeftHand = 4,
    kRightHand = 5,
};

struct JointSpec {
    const char* id;
    const char* servo_group;
    int ui_min;
    int ui_max;
    bool active;
};

extern const std::array<JointSpec, kPoseJointCount> kJointSpecs;

struct MotionLivePose {
    std::array<double, kPoseJointCount> relative_degrees{};
};

struct MotionLiveTarget {
    std::array<bool, kPoseJointCount> present{};
    std::array<double, kPoseJointCount> relative_degrees{};
    bool has_unknown_joint = false;
};

struct MotionLiveJointLimit {
    const char* id = "";
    double min_relative_degrees = 0.0;
    double max_relative_degrees = 0.0;
    double max_speed_dps = 0.0;
};

struct MotionLiveJointProfile {
    const char* id = "";
    const char* servo_key = "";
    int servo_index = -1;
    int pin = -1;
    int trim = 0;
    int neutral_degrees = kNeutralDegrees;
    int direction = 0;
    double min_relative_degrees = 0.0;
    double max_relative_degrees = 0.0;
    int min_servo_degrees = 0;
    int max_servo_degrees = 180;
    double max_speed_dps = 0.0;
};

struct MotionLivePreparedProfile {
    const char* profile_id = "";
    const char* calibration_id = "";
    const char* access_key_sha256 = "";
    int watchdog_ms = kWatchdogMs;
    int max_rate_hz = 0;
    std::array<MotionLiveJointProfile, kActiveJointCount> joints{};
};

struct MotionLiveRuntimeJoint {
    int pin = -1;
    int trim = 0;
    int commanded_degrees = kNeutralDegrees;
    bool attached = false;
};

struct MotionLiveRuntimeConfig {
    bool board_is_gosha_v1 = false;
    bool no_motion_safe_profile = false;
    bool safe_neutral_boot_profile = false;
    bool safe_neutral_commanded = false;
    bool lower_body_attached = false;
    bool watchdog_tick_ready = false;
    std::array<MotionLiveRuntimeJoint, kPoseJointCount> joints{};
};

struct MotionLiveCapabilities {
    bool motion_allowed = false;
    const char* reason = "live_profile_unprepared";
    bool calibrated = false;
    const char* profile_id = kModelProfileId;
    const char* calibration_id = "";
    int watchdog_ms = kWatchdogMs;
    int max_rate_hz = 0;
    bool auth_required = true;
    const char* stop_mode = "hold_setpoint";
    std::array<MotionLiveJointLimit, kActiveJointCount> joint_limits{};
    MotionLivePose commanded_pose{};
};

struct MotionLiveResult {
    bool ok = false;
    const char* code = "internal_error";
    const char* message = "Live command rejected";
    std::string session_id;
    uint32_t seq = 0;
    MotionLivePose commanded_pose{};
    std::array<int, kActiveJointCount> servo_degrees{};
    bool should_apply = false;
    bool stopped = false;
};

using MotionLiveHardwareApplier = std::function<bool(const std::array<int, kActiveJointCount>&)>;

int FindJointIndexById(const char* id);

class MotionLiveCore {
public:
    MotionLiveCore();

    void SetLocalOptInEnabled(bool enabled);
    void SetPreparedProfile(const MotionLivePreparedProfile* profile);
    void SetRuntimeConfig(const MotionLiveRuntimeConfig& runtime);
    void SetHardwareApplier(MotionLiveHardwareApplier applier);

    MotionLiveCapabilities GetCapabilities() const;
    const char* EvaluateSafety() const;

    MotionLiveResult Arm(int owner_socket, const std::string& request_calibration_id,
                         bool access_key_valid, const std::string& new_session_id,
                         uint64_t now_ms);
    MotionLiveResult Pose(int owner_socket, const std::string& session_id, uint32_t seq,
                          const MotionLiveTarget& target, double speed_dps, uint64_t now_ms);
    MotionLiveResult Keepalive(int owner_socket, const std::string& session_id, uint32_t seq,
                               uint64_t now_ms);
    MotionLiveResult Stop(int owner_socket, const std::string& session_id, uint32_t seq);
    MotionLiveResult Tick(uint64_t now_ms);
    void OnSocketClosed(int owner_socket);

    bool IsArmed() const { return armed_; }
    const MotionLivePose& CommandedPose() const { return commanded_pose_; }

private:
    bool ValidatePreparedProfile(const char** reason) const;
    bool ValidateRuntimeAgainstProfile(const char** reason) const;
    bool ValidateOwnerSession(int owner_socket, const std::string& session_id,
                              const char** reason) const;
    bool ValidateNextSeq(uint32_t seq, const char** reason) const;
    MotionLiveResult MakeError(const char* code, const char* message) const;
    MotionLiveResult MakeAck(uint32_t seq, bool should_apply) const;
    MotionLiveResult DisarmWithError(const char* code, const char* message, bool stopped);
    int ActiveProfileIndexForJoint(int joint_index) const;
    bool BuildServoDegrees(const MotionLiveTarget& target,
                           std::array<int, kActiveJointCount>* servo_degrees,
                           const char** reason) const;
    bool BuildServoDegreesForPose(const MotionLivePose& pose,
                                  std::array<int, kActiveJointCount>* servo_degrees,
                                  const char** reason) const;
    MotionLivePose PoseFromServoDegrees(const std::array<int, kActiveJointCount>& servo_degrees) const;
    bool LeaseExpired(uint64_t now_ms) const;
    bool StepTowardTarget(uint64_t now_ms, const char** reason, bool* hardware_changed);
    bool ApplyHardware(const std::array<int, kActiveJointCount>& servo_degrees) const;

    bool local_opt_in_enabled_ = false;
    const MotionLivePreparedProfile* profile_ = nullptr;
    MotionLiveRuntimeConfig runtime_{};
    MotionLiveHardwareApplier hardware_applier_;

    bool armed_ = false;
    int owner_socket_ = -1;
    std::string session_id_;
    uint32_t last_seq_ = 0;
    uint64_t last_command_ms_ = 0;
    uint64_t last_motion_step_ms_ = 0;
    double current_speed_dps_ = 0.0;
    MotionLivePose commanded_pose_{};
    MotionLivePose fractional_pose_{};
    MotionLivePose target_pose_{};
    std::array<int, kActiveJointCount> commanded_servo_degrees_{};
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_LIVE_CORE_H_

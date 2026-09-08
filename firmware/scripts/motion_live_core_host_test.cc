#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../main/boards/gosha-v1/motion_live_auth.h"
#include "../main/boards/gosha-v1/motion_live_core.h"

using gosha::motion_live::JointIndex;
using gosha::motion_live::MotionLiveCore;
using gosha::motion_live::MotionLivePwmDiagnostics;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionLiveRuntimeConfig;
using gosha::motion_live::MotionLiveTarget;
using gosha::motion_live::ServoSlot;
using gosha::motion_live::kPoseJointCount;
using gosha::motion_live::kMotionLiveUsbOwnerId;
using gosha::motion_live::kRightArmHomeDegrees;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n";   \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

int Joint(JointIndex joint) {
    return static_cast<int>(joint);
}

int Slot(ServoSlot slot) {
    return static_cast<int>(slot);
}

MotionLivePreparedProfile ValidProfile(bool swapped_sides = false) {
    const char* neg_leg_key = swapped_sides ? "right_leg" : "left_leg";
    const char* pos_leg_key = swapped_sides ? "left_leg" : "right_leg";
    const int neg_leg_index = swapped_sides ? 1 : 0;
    const int pos_leg_index = swapped_sides ? 0 : 1;
    const int neg_leg_pin = swapped_sides ? 39 : 17;
    const int pos_leg_pin = swapped_sides ? 17 : 39;
    return {
        "gosha-preview-v1",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", neg_leg_key, neg_leg_index, neg_leg_pin, 0, 90, 1, -10.0, 10.0, 80, 100, 15.0},
            {"leg_positive_x", pos_leg_key, pos_leg_index, pos_leg_pin, 0, 90, -1, -10.0, 10.0, 80, 100, 15.0},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -8.0, 8.0, 82, 98, 12.0},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -8.0, 8.0, 82, 98, 12.0},
        }},
        "verified",
        4,
    };
}

MotionLivePreparedProfile CommissioningProfile() {
    return {
        "gosha-preview-v1",
        "123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
        "bbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, 17, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"leg_positive_x", "right_leg", 1, 39, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
        }},
        "commissioning",
        4,
    };
}

MotionLivePreparedProfile RightArmCommissioningProfile(double right_arm_min = -5.0,
                                                       double right_arm_max = 5.0,
                                                       int right_arm_servo_min = 130,
                                                       int right_arm_servo_max = 140) {
    return {
        "gosha-preview-v1",
        "223456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
        "cbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, 17, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"leg_positive_x", "right_leg", 1, 39, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
            {"arm_positive_x", "right_hand", 5, 12, 0, 135, 1,
             right_arm_min, right_arm_max, right_arm_servo_min, right_arm_servo_max, 1.0},
        }},
        "commissioning_right_arm",
        5,
    };
}

MotionLivePreparedProfile RightArmSymmetricCommissioningProfile(double right_arm_extent) {
    const int right_arm_extent_int = static_cast<int>(right_arm_extent);
    return RightArmCommissioningProfile(
        -right_arm_extent, right_arm_extent,
        kRightArmHomeDegrees - right_arm_extent_int,
        kRightArmHomeDegrees + right_arm_extent_int);
}

MotionLivePreparedProfile MotionEditorProfile(double right_arm_min = -70.0,
                                              double right_arm_max = 55.0,
                                              int right_arm_neutral = 125,
                                              double max_speed_dps = 5.0) {
    const int right_servo_min = static_cast<int>(
        std::min(right_arm_neutral + right_arm_min, right_arm_neutral + right_arm_max));
    const int right_servo_max = static_cast<int>(
        std::max(right_arm_neutral + right_arm_min, right_arm_neutral + right_arm_max));
    return {
        "gosha-preview-v1",
        "323456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
        "dbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, 17, 0, 90, 1, -35.0, 35.0, 55, 125, max_speed_dps},
            {"leg_positive_x", "right_leg", 1, 39, 0, 90, -1, -35.0, 35.0, 55, 125, max_speed_dps},
            {"foot_negative_x", "left_foot", 2, 18, 0, 90, 1, -30.0, 30.0, 60, 120, max_speed_dps},
            {"foot_positive_x", "right_foot", 3, 38, 0, 90, -1, -30.0, 30.0, 60, 120, max_speed_dps},
            {"arm_positive_x", "right_hand", 5, 12, 0, right_arm_neutral, 1,
             right_arm_min, right_arm_max, right_servo_min, right_servo_max, max_speed_dps},
        }},
        "motion_editor",
        5,
    };
}

MotionLiveRuntimeConfig ValidRuntime(bool right_arm_available = false,
                                     bool right_arm_attached = false,
                                     int right_arm_neutral = kRightArmHomeDegrees) {
    MotionLiveRuntimeConfig runtime;
    runtime.board_is_gosha_v1 = true;
    runtime.no_motion_safe_profile = true;
    runtime.safe_neutral_boot_profile = true;
    runtime.safe_neutral_commanded = true;
    runtime.lower_body_attached = true;
    runtime.watchdog_tick_ready = true;
    runtime.joints[Slot(ServoSlot::kLeftLeg)] = {17, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kRightLeg)] = {39, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kLeftFoot)] = {18, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kRightFoot)] = {38, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kLeftHand)] = {-1, 0, 45, false};
    runtime.joints[Slot(ServoSlot::kRightHand)] =
        {right_arm_available ? 12 : -1, 0, right_arm_neutral, right_arm_attached};
    return runtime;
}

MotionLiveTarget Target(double left_leg, double right_leg, double left_foot, double right_foot) {
    MotionLiveTarget target;
    target.present[Joint(JointIndex::kLegNegativeX)] = true;
    target.present[Joint(JointIndex::kLegPositiveX)] = true;
    target.present[Joint(JointIndex::kFootNegativeX)] = true;
    target.present[Joint(JointIndex::kFootPositiveX)] = true;
    target.relative_degrees[Joint(JointIndex::kLegNegativeX)] = left_leg;
    target.relative_degrees[Joint(JointIndex::kLegPositiveX)] = right_leg;
    target.relative_degrees[Joint(JointIndex::kFootNegativeX)] = left_foot;
    target.relative_degrees[Joint(JointIndex::kFootPositiveX)] = right_foot;
    return target;
}

MotionLiveTarget RightArmTarget(double left_leg, double right_leg, double left_foot,
                                double right_foot, double right_arm) {
    MotionLiveTarget target = Target(left_leg, right_leg, left_foot, right_foot);
    target.present[Joint(JointIndex::kArmPositiveX)] = true;
    target.relative_degrees[Joint(JointIndex::kArmPositiveX)] = right_arm;
    return target;
}

MotionLiveCore ConfiguredCore(const MotionLivePreparedProfile* profile,
                              const MotionLiveRuntimeConfig& runtime) {
    MotionLiveCore core;
    core.SetLocalOptInEnabled(true);
    core.SetPreparedProfile(profile);
    core.SetRuntimeConfig(runtime);
    return core;
}

}  // namespace

int main() {
    const std::string good_hash(64, 'a');
    CHECK(gosha::motion_live::ConstantTimeEquals64(good_hash.c_str(), good_hash.c_str()));
    CHECK(!gosha::motion_live::ConstantTimeEquals64(nullptr, good_hash.c_str()));
    CHECK(!gosha::motion_live::ConstantTimeEquals64(good_hash.c_str(), nullptr));
    for (int size = 0; size < 64; ++size) {
        const std::string short_hash(size, 'a');
        CHECK(!gosha::motion_live::ConstantTimeEquals64(good_hash.c_str(), short_hash.c_str()));
        CHECK(!gosha::motion_live::ConstantTimeEquals64(short_hash.c_str(), good_hash.c_str()));
    }
    for (const auto& malformed : {std::string(65, 'a'), std::string(64, 'z'), std::string(64, 'A')}) {
        CHECK(!gosha::motion_live::ConstantTimeEquals64(good_hash.c_str(), malformed.c_str()));
    }
    CHECK(!gosha::motion_live::ConstantTimeEquals64(good_hash.c_str(), std::string(64, 'b').c_str()));

    {
        MotionLiveCore core;
        CHECK(!core.GetCapabilities().motion_allowed);
        CHECK(std::string(core.GetCapabilities().reason) == "live_profile_unprepared");
        CHECK(!core.Arm(1, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                        true, "session_live_0001", 1000).ok);
    }

    MotionLivePreparedProfile profile = ValidProfile();
    MotionLiveRuntimeConfig runtime = ValidRuntime();
    MotionLiveCore core = ConfiguredCore(&profile, runtime);
    int apply_count = 0;
    std::array<int, kPoseJointCount> last_apply{};
    MotionLivePwmDiagnostics diagnostic_snapshot;
    diagnostic_snapshot.servos[Slot(ServoSlot::kLeftLeg)] = {
        "left_leg", "left_leg", nullptr, true, true, 17, 2, true, 50, true, 614,
        true, 90, 90, 90, 614, 5000, true, false};
    core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
        ++apply_count;
        last_apply = target;
        return true;
    });
    int diagnostics_read_count = 0;
    core.SetPwmDiagnosticsProvider([&]() {
        ++diagnostics_read_count;
        return diagnostic_snapshot;
    });

    auto idle_tick = core.Tick(900);
    CHECK(!idle_tick.stopped);
    CHECK(diagnostics_read_count == 0);

    auto caps = core.GetCapabilities();
    CHECK(diagnostics_read_count == 1);
    CHECK(caps.motion_allowed);
    CHECK(caps.calibrated);
    CHECK(!caps.initialization_required);
    CHECK(!caps.right_arm_available);
    CHECK(caps.joint_limit_count == 4);
    CHECK(caps.max_rate_hz == 20);
    CHECK(std::string(caps.joint_limits[0].id) == "leg_negative_x");
    CHECK(caps.servo_degrees[Slot(ServoSlot::kLeftLeg)] == 90);
    CHECK(std::string(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].id) ==
          "left_leg");
    CHECK(std::string(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].servo_key) ==
          "left_leg");
    CHECK(std::string(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].joint_id) ==
          "leg_negative_x");
    CHECK(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].attached);
    CHECK(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].frequency_hz == 50);
    CHECK(caps.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].duty == 614);

    MotionLivePreparedProfile swapped_profile = ValidProfile(true);
    MotionLiveCore swapped_core = ConfiguredCore(&swapped_profile, ValidRuntime());
    std::array<int, kPoseJointCount> swapped_apply{};
    swapped_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
        swapped_apply = target;
        return true;
    });
    CHECK(swapped_core.GetCapabilities().motion_allowed);
    auto swapped_arm = swapped_core.Arm(20, swapped_profile.calibration_id, true,
                                       "session_live_swap", 1000);
    CHECK(swapped_arm.ok);
    auto swapped_pose = swapped_core.Pose(20, swapped_arm.session_id, 1,
                                         Target(5, 0, 0, 0), 10, 1250);
    CHECK(swapped_pose.ok);
    CHECK(swapped_apply[Slot(ServoSlot::kLeftLeg)] == 90);
    CHECK(swapped_apply[Slot(ServoSlot::kRightLeg)] == 92);

    auto wrong_key = core.Arm(10, profile.calibration_id, false, "session_live_0001", 1000);
    CHECK(!wrong_key.ok);
    CHECK(std::string(wrong_key.code) == "auth_failed");
    CHECK(!core.IsArmed());

    auto armed = core.Arm(10, profile.calibration_id, true, "session_live_0001", 1000);
    CHECK(armed.ok);
    CHECK(core.IsArmed());

    auto busy = core.Arm(11, profile.calibration_id, true, "session_live_9999", 1005);
    CHECK(!busy.ok);
    CHECK(std::string(busy.code) == "session_busy");
    CHECK(core.IsArmed());

    auto foreign = core.Pose(11, armed.session_id, 1, Target(1, 1, 1, 1), 10, 1010);
    CHECK(!foreign.ok);
    CHECK(std::string(foreign.code) == "session_not_owner");
    CHECK(core.IsArmed());
    CHECK(apply_count == 0);

    auto keepalive = core.Keepalive(10, armed.session_id, 1, 1020);
    CHECK(keepalive.ok);
    CHECK(keepalive.seq == 1);

    auto bad_seq = core.Pose(10, armed.session_id, 3, Target(1, 1, 1, 1), 10, 1030);
    CHECK(!bad_seq.ok);
    CHECK(bad_seq.stopped);
    CHECK(std::string(bad_seq.code) == "bad_seq");
    CHECK(!core.IsArmed());
    CHECK(apply_count == 0);

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0002", 2000);
    CHECK(armed.ok);
    MotionLiveTarget hand_target = Target(1, 1, 1, 1);
    hand_target.present[Joint(JointIndex::kArmNegativeX)] = true;
    hand_target.relative_degrees[Joint(JointIndex::kArmNegativeX)] = 5;
    auto hand_reject = core.Pose(10, armed.session_id, 1, hand_target, 10, 2010);
    CHECK(!hand_reject.ok);
    CHECK(std::string(hand_reject.code) == "bad_target");
    CHECK(!core.IsArmed());
    CHECK(apply_count == 0);

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0003", 3000);
    CHECK(armed.ok);
    auto too_fast = core.Pose(10, armed.session_id, 1, Target(1, 1, 1, 1), 31, 3010);
    CHECK(!too_fast.ok);
    CHECK(std::string(too_fast.code) == "rate_limit");
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0004", 4000);
    CHECK(armed.ok);
    auto out_of_range = core.Pose(10, armed.session_id, 1, Target(11, 1, 1, 1), 10, 4010);
    CHECK(!out_of_range.ok);
    CHECK(std::string(out_of_range.code) == "limit_violation");
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0005", 5000);
    CHECK(armed.ok);
    auto pose = core.Pose(10, armed.session_id, 1, Target(5, 5, -3, -3), 12, 5250);
    CHECK(pose.ok);
    CHECK(pose.should_apply);
    CHECK(apply_count == 1);
    CHECK(last_apply[0] == 93);
    CHECK(last_apply[1] == 87);
    CHECK(last_apply[2] == 87);
    CHECK(last_apply[3] == 93);
    CHECK(last_apply[Slot(ServoSlot::kLeftHand)] == 90);
    CHECK(last_apply[Slot(ServoSlot::kRightHand)] == 90);
    auto pose_reached = core.Keepalive(10, armed.session_id, 2, 5500);
    CHECK(pose_reached.ok);
    CHECK(pose_reached.should_apply);
    CHECK(last_apply[0] == 95);
    CHECK(last_apply[1] == 85);
    CHECK(pose_reached.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(pose.commanded_pose.relative_degrees[Joint(JointIndex::kArmNegativeX)] == 0);
    CHECK(pose.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == 0);
    auto stop = core.Stop(10, armed.session_id, 3);
    CHECK(stop.stopped);
    CHECK(stop.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0006", 6000);
    CHECK(armed.ok);
    auto slow_pose = core.Pose(10, armed.session_id, 1, Target(10, -10, 0, 0), 5, 6100);
    CHECK(slow_pose.ok);
    CHECK(slow_pose.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(slow_pose.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] < 10.0);
    auto slow_tick = core.Tick(6250);
    CHECK(!slow_tick.stopped);
    CHECK(slow_tick.hardware_changed);
    CHECK(core.CommandedPose().relative_degrees[Joint(JointIndex::kLegNegativeX)] < 10.0);
    stop = core.Stop(10, armed.session_id, 2);
    CHECK(stop.stopped);
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0007", 7000);
    CHECK(armed.ok);
    auto nonzero = core.Pose(10, armed.session_id, 1, Target(10, -10, 0, 0), 10, 7250);
    CHECK(nonzero.ok);
    auto nonzero_reached = core.Keepalive(10, armed.session_id, 2, 7500);
    CHECK(nonzero_reached.ok);
    CHECK(nonzero_reached.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] == 10.0);
    stop = core.Stop(10, armed.session_id, 3);
    CHECK(stop.stopped);
    armed = core.Arm(10, profile.calibration_id, true, "session_live_0008", 8010);
    CHECK(armed.ok);
    auto retained = core.Keepalive(10, armed.session_id, 1, 8020);
    CHECK(retained.ok);
    CHECK(retained.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] == 10.0);
    stop = core.Stop(10, armed.session_id, 2);
    CHECK(stop.stopped);

    {
        MotionLiveCore watchdog_core = ConfiguredCore(&profile, runtime);
        int watchdog_diagnostics_calls = 0;
        int watchdog_apply_count = 0;
        std::array<int, kPoseJointCount> watchdog_last_apply{};
        watchdog_core.SetPwmDiagnosticsProvider([&]() {
            ++watchdog_diagnostics_calls;
            return MotionLivePwmDiagnostics{};
        });
        watchdog_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++watchdog_apply_count;
            watchdog_last_apply = target;
            return true;
        });

        auto unarmed_tick = watchdog_core.Tick(8500);
        CHECK(!unarmed_tick.stopped);
        CHECK(!unarmed_tick.hardware_changed);
        CHECK(watchdog_diagnostics_calls == 0);

        auto watchdog_armed = watchdog_core.Arm(
            12, profile.calibration_id, true, "session_periodic_watchdog", 9000);
        CHECK(watchdog_armed.ok);
        watchdog_diagnostics_calls = 0;
        auto armed_tick = watchdog_core.Tick(9200);
        CHECK(!armed_tick.stopped);
        CHECK(!armed_tick.hardware_changed);
        CHECK(watchdog_diagnostics_calls == 0);
        CHECK(watchdog_apply_count == 0);

        auto legacy_pose = watchdog_core.Pose(
            12, watchdog_armed.session_id, 1, Target(10, 0, 0, 0), 10, 9250);
        CHECK(legacy_pose.ok);
        const double before_periodic_step =
            watchdog_core.CommandedPose().relative_degrees[Joint(JointIndex::kLegNegativeX)];
        const int apply_count_before_periodic_step = watchdog_apply_count;
        watchdog_diagnostics_calls = 0;
        auto legacy_tick = watchdog_core.Tick(9500);
        CHECK(!legacy_tick.stopped);
        CHECK(legacy_tick.hardware_changed);
        CHECK(watchdog_diagnostics_calls == 0);
        CHECK(watchdog_apply_count > apply_count_before_periodic_step);
        CHECK(watchdog_core.CommandedPose().relative_degrees[Joint(JointIndex::kLegNegativeX)] >
              before_periodic_step);
        CHECK(watchdog_core.CommandedPose().relative_degrees[Joint(JointIndex::kLegNegativeX)] <=
              10.0);
        CHECK(watchdog_last_apply[Slot(ServoSlot::kLeftLeg)] >= 90);

        auto timeout = watchdog_core.Tick(9551);
        CHECK(timeout.stopped);
        CHECK(std::string(timeout.code) == "watchdog_timeout");
        CHECK(watchdog_diagnostics_calls == 0);
        CHECK(!watchdog_core.IsArmed());
    }

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0009", 9000);
    CHECK(armed.ok);
    CHECK(!core.Tick(9300).stopped);
    auto timeout = core.Tick(9301);
    CHECK(timeout.stopped);
    CHECK(std::string(timeout.code) == "watchdog_timeout");
    CHECK(!core.IsArmed());

    const int apply_count_before_late_packet = apply_count;
    armed = core.Arm(10, profile.calibration_id, true, "session_live_0010", 10000);
    CHECK(armed.ok);
    auto late_packet = core.Pose(10, armed.session_id, 1, Target(0, 0, 0, 0), 10, 10301);
    CHECK(!late_packet.ok);
    CHECK(late_packet.stopped);
    CHECK(std::string(late_packet.code) == "watchdog_timeout");
    CHECK(apply_count == apply_count_before_late_packet);
    CHECK(!core.IsArmed());

    MotionLiveRuntimeConfig no_watchdog = ValidRuntime();
    no_watchdog.watchdog_tick_ready = false;
    core.SetRuntimeConfig(no_watchdog);
    CHECK(!core.GetCapabilities().motion_allowed);
    CHECK(std::string(core.GetCapabilities().reason) == "watchdog_unavailable");
    core.SetRuntimeConfig(ValidRuntime());

    {
        MotionLivePreparedProfile commissioning_profile = CommissioningProfile();
        MotionLiveCore commissioning_core = ConfiguredCore(&commissioning_profile, ValidRuntime());
        int commissioning_apply_count = 0;
        std::array<int, kPoseJointCount> commissioning_last_apply{};
        commissioning_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++commissioning_apply_count;
            commissioning_last_apply = target;
            return true;
        });

        auto commissioning_caps = commissioning_core.GetCapabilities();
        CHECK(commissioning_caps.motion_allowed);
        CHECK(!commissioning_caps.calibrated);
        CHECK(commissioning_caps.commissioning);
        CHECK(commissioning_caps.joint_limit_count == 4);
        CHECK(std::string(commissioning_caps.mode) == "commissioning");
        for (int i = 0; i < commissioning_caps.joint_limit_count; ++i) {
            const auto& limit = commissioning_caps.joint_limits[i];
            CHECK(limit.min_relative_degrees == -1.0);
            CHECK(limit.max_relative_degrees == 1.0);
            CHECK(limit.max_speed_dps == 1.0);
        }

        auto commissioning_armed = commissioning_core.Arm(
            30, commissioning_profile.calibration_id, true, "session_commissioning_1", 12000);
        CHECK(commissioning_armed.ok);
        auto commissioning_fast = commissioning_core.Pose(
            30, commissioning_armed.session_id, 1, Target(1, 0, 0, 0), 2, 12010);
        CHECK(!commissioning_fast.ok);
        CHECK(std::string(commissioning_fast.code) == "rate_limit");
        CHECK(!commissioning_core.IsArmed());
        CHECK(commissioning_apply_count == 0);

        commissioning_armed = commissioning_core.Arm(
            30, commissioning_profile.calibration_id, true, "session_commissioning_2", 13000);
        CHECK(commissioning_armed.ok);
        auto commissioning_two_joints = commissioning_core.Pose(
            30, commissioning_armed.session_id, 1, Target(1, 1, 0, 0), 1, 13010);
        CHECK(!commissioning_two_joints.ok);
        CHECK(std::string(commissioning_two_joints.code) == "commissioning_single_joint");
        CHECK(!commissioning_core.IsArmed());
        CHECK(commissioning_apply_count == 0);

        commissioning_armed = commissioning_core.Arm(
            30, commissioning_profile.calibration_id, true, "session_commissioning_3", 15000);
        CHECK(commissioning_armed.ok);
        auto commissioning_one_joint = commissioning_core.Pose(
            30, commissioning_armed.session_id, 1, Target(1, 0, 0, 0), 1, 15010);
        CHECK(commissioning_one_joint.ok);
        CHECK(!commissioning_one_joint.should_apply);
        for (uint32_t seq = 2; seq <= 5; ++seq) {
            commissioning_one_joint = commissioning_core.Keepalive(
                30, commissioning_armed.session_id, seq, 15010 + (seq - 1) * 250);
            CHECK(commissioning_one_joint.ok);
        }
        CHECK(commissioning_one_joint.should_apply);
        CHECK(commissioning_apply_count == 1);
        CHECK(commissioning_last_apply[Slot(ServoSlot::kLeftLeg)] == 91);
        auto commissioning_back_to_baseline = commissioning_core.Pose(
            30, commissioning_armed.session_id, 6, Target(0, 0, 0, 0), 1, 16010);
        CHECK(commissioning_back_to_baseline.ok);
        for (uint32_t seq = 7; seq <= 10; ++seq) {
            commissioning_back_to_baseline = commissioning_core.Keepalive(
                30, commissioning_armed.session_id, seq, 16010 + (seq - 6) * 250);
            CHECK(commissioning_back_to_baseline.ok);
        }
        CHECK(commissioning_apply_count == 2);
        auto commissioning_switch_joint = commissioning_core.Pose(
            30, commissioning_armed.session_id, 11, Target(0, 0, 1, 0), 1, 17020);
        CHECK(!commissioning_switch_joint.ok);
        CHECK(std::string(commissioning_switch_joint.code) == "commissioning_single_joint");
        CHECK(!commissioning_core.IsArmed());

        commissioning_armed = commissioning_core.Arm(
            30, commissioning_profile.calibration_id, true, "session_commissioning_4", 19000);
        CHECK(commissioning_armed.ok);
        auto commissioning_plus_one = commissioning_core.Pose(
            30, commissioning_armed.session_id, 1, Target(1, 0, 0, 0), 1, 19010);
        CHECK(commissioning_plus_one.ok);
        for (uint32_t seq = 2; seq <= 5; ++seq) {
            commissioning_plus_one = commissioning_core.Keepalive(
                30, commissioning_armed.session_id, seq, 19010 + (seq - 1) * 250);
            CHECK(commissioning_plus_one.ok);
        }
        CHECK(commissioning_plus_one.should_apply);
        auto commissioning_stop = commissioning_core.Stop(30, commissioning_armed.session_id, 6);
        CHECK(commissioning_stop.stopped);
        commissioning_armed = commissioning_core.Arm(
            30, commissioning_profile.calibration_id, true, "session_commissioning_5", 21000);
        CHECK(commissioning_armed.ok);
        auto commissioning_two_degrees_from_baseline = commissioning_core.Pose(
            30, commissioning_armed.session_id, 1, Target(-1, 0, 0, 0), 1, 21010);
        CHECK(!commissioning_two_degrees_from_baseline.ok);
        CHECK(std::string(commissioning_two_degrees_from_baseline.code) ==
              "commissioning_single_joint");
        CHECK(!commissioning_core.IsArmed());

        MotionLivePreparedProfile wide_commissioning = CommissioningProfile();
        wide_commissioning.joints[0].max_relative_degrees = 2.0;
        MotionLiveCore wide_commissioning_core = ConfiguredCore(&wide_commissioning, ValidRuntime());
        CHECK(!wide_commissioning_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wide_commissioning_core.GetCapabilities().reason) == "profile_mismatch");
    }

    {
        MotionLivePreparedProfile right_profile = RightArmCommissioningProfile();
        MotionLiveCore right_core = ConfiguredCore(&right_profile, ValidRuntime(true, false));
        int init_count = 0;
        int last_init_home = 0;
        int right_apply_count = 0;
        std::array<int, kPoseJointCount> right_last_apply{};
        right_core.SetRightArmInitializer([&](int home_degrees) {
            ++init_count;
            last_init_home = home_degrees;
            return true;
        });
        right_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++right_apply_count;
            right_last_apply = target;
            return true;
        });

        auto right_caps = right_core.GetCapabilities();
        CHECK(!right_caps.motion_allowed);
        CHECK(std::string(right_caps.reason) == "right_arm_initialization_required");
        CHECK(right_caps.initialization_required);
        CHECK(right_caps.right_arm_available);
        CHECK(!right_caps.right_arm_initialized);
        CHECK(std::string(right_caps.initialization_op) == "initialize_right_arm");
        CHECK(right_caps.commissioning);
        CHECK(!right_caps.calibrated);
        CHECK(right_caps.joint_limit_count == 5);
        CHECK(std::string(right_caps.mode) == "commissioning_right_arm");
        CHECK(std::string(right_caps.joint_limits[4].id) == "arm_positive_x");
        CHECK(right_caps.joint_limits[4].min_relative_degrees == -5.0);
        CHECK(right_caps.joint_limits[4].max_relative_degrees == 5.0);
        CHECK(right_caps.joint_limits[4].max_speed_dps == 1.0);
        CHECK(right_caps.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == 0.0);

        MotionLivePreparedProfile right_profile_15 = RightArmSymmetricCommissioningProfile(15.0);
        MotionLiveCore right_caps_15_core =
            ConfiguredCore(&right_profile_15, ValidRuntime(true, false));
        auto right_caps_15 = right_caps_15_core.GetCapabilities();
        CHECK(!right_caps_15.motion_allowed);
        CHECK(std::string(right_caps_15.reason) == "right_arm_initialization_required");
        CHECK(right_caps_15.joint_limit_count == 5);
        CHECK(std::string(right_caps_15.joint_limits[4].id) == "arm_positive_x");
        CHECK(right_caps_15.joint_limits[4].min_relative_degrees == -15.0);
        CHECK(right_caps_15.joint_limits[4].max_relative_degrees == 15.0);
        CHECK(right_caps_15.joint_limits[4].max_speed_dps == 1.0);

        MotionLivePreparedProfile right_profile_70_up =
            RightArmCommissioningProfile(-70.0, 15.0, 65, 150);
        MotionLiveCore right_caps_70_up_core =
            ConfiguredCore(&right_profile_70_up, ValidRuntime(true, false));
        auto right_caps_70_up = right_caps_70_up_core.GetCapabilities();
        CHECK(!right_caps_70_up.motion_allowed);
        CHECK(std::string(right_caps_70_up.reason) == "right_arm_initialization_required");
        CHECK(right_caps_70_up.joint_limit_count == 5);
        CHECK(std::string(right_caps_70_up.joint_limits[4].id) == "arm_positive_x");
        CHECK(right_caps_70_up.joint_limits[4].min_relative_degrees == -70.0);
        CHECK(right_caps_70_up.joint_limits[4].max_relative_degrees == 15.0);
        CHECK(right_caps_70_up.joint_limits[4].max_speed_dps == 1.0);

        auto arm_before_init = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_preinit", 30000);
        CHECK(!arm_before_init.ok);
        CHECK(std::string(arm_before_init.code) == "right_arm_initialization_required");
        CHECK(init_count == 0);

        auto bad_init_key = right_core.InitializeRightArm(40, right_profile.calibration_id, false);
        CHECK(!bad_init_key.ok);
        CHECK(std::string(bad_init_key.code) == "auth_failed");
        CHECK(init_count == 0);

        auto bad_init_cal = right_core.InitializeRightArm(40, profile.calibration_id, true);
        CHECK(!bad_init_cal.ok);
        CHECK(std::string(bad_init_cal.code) == "profile_mismatch");
        CHECK(init_count == 0);

        auto init = right_core.InitializeRightArm(40, right_profile.calibration_id, true);
        CHECK(init.ok);
        CHECK(init_count == 1);
        CHECK(last_init_home == kRightArmHomeDegrees);
        right_caps = right_core.GetCapabilities();
        CHECK(right_caps.motion_allowed);
        CHECK(!right_caps.initialization_required);
        CHECK(right_caps.right_arm_initialized);
        CHECK(right_caps.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == 0.0);

        auto second_init = right_core.InitializeRightArm(40, right_profile.calibration_id, true);
        CHECK(second_init.ok);
        CHECK(init_count == 1);

        auto right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_missing_arm", 40000);
        CHECK(right_armed.ok);
        auto missing_arm = right_core.Pose(40, right_armed.session_id, 1, Target(0, 0, 0, 0), 1, 40010);
        CHECK(!missing_arm.ok);
        CHECK(std::string(missing_arm.code) == "bad_target");
        CHECK(!right_core.IsArmed());
        CHECK(right_apply_count == 0);

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_left_arm", 41000);
        CHECK(right_armed.ok);
        MotionLiveTarget left_arm_target = RightArmTarget(0, 0, 0, 0, 0);
        left_arm_target.present[Joint(JointIndex::kArmNegativeX)] = true;
        auto left_arm = right_core.Pose(40, right_armed.session_id, 1, left_arm_target, 1, 41010);
        CHECK(!left_arm.ok);
        CHECK(std::string(left_arm.code) == "bad_target");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_speed", 42000);
        CHECK(right_armed.ok);
        auto right_fast = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -5), 2, 42010);
        CHECK(!right_fast.ok);
        CHECK(std::string(right_fast.code) == "rate_limit");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_range", 43000);
        CHECK(right_armed.ok);
        auto right_too_far = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -6), 1, 43010);
        CHECK(!right_too_far.ok);
        CHECK(std::string(right_too_far.code) == "limit_violation");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_two_joints", 44000);
        CHECK(right_armed.ok);
        auto right_two_joints = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(1, 0, 0, 0, -5), 1, 44010);
        CHECK(!right_two_joints.ok);
        CHECK(std::string(right_two_joints.code) == "commissioning_single_joint");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_step", 50000);
        CHECK(right_armed.ok);
        auto right_step = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -5), 1, 50010);
        CHECK(right_step.ok);
        CHECK(!right_step.should_apply);
        auto right_keepalive = right_core.Keepalive(40, right_armed.session_id, 2, 50260);
        CHECK(right_keepalive.ok);
        CHECK(!right_keepalive.should_apply);
        CHECK(right_apply_count == 0);
        CHECK(right_keepalive.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == 0.0);
        CHECK(!right_core.Tick(50300).hardware_changed);
        CHECK(right_apply_count == 0);
        for (uint32_t seq = 3; seq <= 22; ++seq) {
            right_step = right_core.Pose(
                40, right_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, -5), 1,
                50010 + (seq - 1) * 250);
            CHECK(right_step.ok);
        }
        CHECK(right_apply_count > 0);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftHand)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightHand)] == 130);
        CHECK(right_step.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == -5.0);
        CHECK(right_step.commanded_pose.relative_degrees[Joint(JointIndex::kArmNegativeX)] == 0.0);
        auto right_stop = right_core.Stop(40, right_armed.session_id, 23);
        CHECK(right_stop.stopped);
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_ten_degree_jump", 55500);
        CHECK(right_armed.ok);
        auto ten_degree_jump = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, 5), 1, 55510);
        CHECK(!ten_degree_jump.ok);
        CHECK(std::string(ten_degree_jump.code) == "commissioning_single_joint");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            40, right_profile.calibration_id, true, "session_right_socket", 56000);
        CHECK(right_armed.ok);
        right_core.OnSocketClosed(40);
        CHECK(!right_core.IsArmed());
        auto after_close = right_core.Pose(
            40, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -5), 1, 56010);
        CHECK(!after_close.ok);
        CHECK(std::string(after_close.code) == "session_not_owner");

        CHECK(kMotionLiveUsbOwnerId < 0);
        right_armed = right_core.Arm(
            kMotionLiveUsbOwnerId, right_profile.calibration_id, true,
            "session_right_usb_owner", 56500);
        CHECK(right_armed.ok);
        auto ws_keepalive = right_core.Keepalive(40, right_armed.session_id, 1, 56510);
        CHECK(!ws_keepalive.ok);
        CHECK(std::string(ws_keepalive.code) == "session_not_owner");
        CHECK(right_core.IsArmed());
        right_core.OnTransportClosed(40);
        CHECK(right_core.IsArmed());
        auto ws_stop = right_core.Stop(40, right_armed.session_id, 1);
        CHECK(!ws_stop.stopped);
        CHECK(std::string(ws_stop.code) == "session_not_owner");
        CHECK(right_core.IsArmed());
        auto usb_stop = right_core.Stop(kMotionLiveUsbOwnerId, right_armed.session_id, 1);
        CHECK(usb_stop.stopped);
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            kMotionLiveUsbOwnerId, right_profile.calibration_id, true,
            "session_right_usb_watchdog", 57000);
        CHECK(right_armed.ok);
        CHECK(!right_core.Tick(57300).stopped);
        auto usb_timeout = right_core.Tick(57301);
        CHECK(usb_timeout.stopped);
        CHECK(std::string(usb_timeout.code) == "watchdog_timeout");
        CHECK(!right_core.IsArmed());
        auto late_usb_stop = right_core.Stop(kMotionLiveUsbOwnerId, right_armed.session_id, 1);
        CHECK(!late_usb_stop.stopped);
        CHECK(std::string(late_usb_stop.code) == "session_not_owner");

        MotionLivePreparedProfile wrong_direction = RightArmCommissioningProfile();
        wrong_direction.joints[4].direction = -1;
        MotionLiveCore wrong_direction_core = ConfiguredCore(&wrong_direction, ValidRuntime(true, false));
        CHECK(!wrong_direction_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wrong_direction_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile wrong_slot = RightArmCommissioningProfile();
        wrong_slot.joints[4].servo_key = "left_hand";
        wrong_slot.joints[4].servo_index = 4;
        wrong_slot.joints[4].pin = 8;
        wrong_slot.joints[4].neutral_degrees = 45;
        MotionLiveCore wrong_slot_core = ConfiguredCore(&wrong_slot, ValidRuntime(true, false));
        CHECK(!wrong_slot_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wrong_slot_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile wrong_pin = RightArmCommissioningProfile();
        wrong_pin.joints[4].pin = 8;
        MotionLiveCore wrong_pin_core = ConfiguredCore(&wrong_pin, ValidRuntime(true, false));
        CHECK(!wrong_pin_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wrong_pin_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile old_five_joint = CommissioningProfile();
        old_five_joint.joints[4] = RightArmCommissioningProfile().joints[4];
        old_five_joint.joint_count = 5;
        MotionLiveCore old_five_core = ConfiguredCore(&old_five_joint, ValidRuntime(true, false));
        CHECK(!old_five_core.GetCapabilities().motion_allowed);
        CHECK(std::string(old_five_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile ten_degree_profile = RightArmSymmetricCommissioningProfile(10.0);
        MotionLiveCore ten_degree_core =
            ConfiguredCore(&ten_degree_profile, ValidRuntime(true, false));
        CHECK(!ten_degree_core.GetCapabilities().motion_allowed);
        CHECK(std::string(ten_degree_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile too_wide_profile = RightArmSymmetricCommissioningProfile(16.0);
        MotionLiveCore too_wide_core =
            ConfiguredCore(&too_wide_profile, ValidRuntime(true, false));
        CHECK(!too_wide_core.GetCapabilities().motion_allowed);
        CHECK(std::string(too_wide_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile asymmetric_profile = RightArmSymmetricCommissioningProfile(15.0);
        asymmetric_profile.joints[4].min_relative_degrees = -5.0;
        asymmetric_profile.joints[4].min_servo_degrees = 130;
        MotionLiveCore asymmetric_core =
            ConfiguredCore(&asymmetric_profile, ValidRuntime(true, false));
        CHECK(!asymmetric_core.GetCapabilities().motion_allowed);
        CHECK(std::string(asymmetric_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile wrong_bounds_15 = RightArmSymmetricCommissioningProfile(15.0);
        wrong_bounds_15.joints[4].min_servo_degrees = 121;
        MotionLiveCore wrong_bounds_15_core =
            ConfiguredCore(&wrong_bounds_15, ValidRuntime(true, false));
        CHECK(!wrong_bounds_15_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wrong_bounds_15_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile seventy_down_profile =
            RightArmCommissioningProfile(-70.0, 70.0, 65, 180);
        MotionLiveCore seventy_down_core =
            ConfiguredCore(&seventy_down_profile, ValidRuntime(true, false));
        CHECK(!seventy_down_core.GetCapabilities().motion_allowed);
        CHECK(std::string(seventy_down_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile max_servo_profile =
            RightArmCommissioningProfile(-70.0, 45.0, 65, 180);
        MotionLiveCore max_servo_core =
            ConfiguredCore(&max_servo_profile, ValidRuntime(true, false));
        CHECK(!max_servo_core.GetCapabilities().motion_allowed);
        CHECK(std::string(max_servo_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile almost_seventy_up =
            RightArmCommissioningProfile(-69.0, 15.0, 66, 150);
        MotionLiveCore almost_seventy_up_core =
            ConfiguredCore(&almost_seventy_up, ValidRuntime(true, false));
        CHECK(!almost_seventy_up_core.GetCapabilities().motion_allowed);
        CHECK(std::string(almost_seventy_up_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile wrong_bounds_70_up =
            RightArmCommissioningProfile(-70.0, 15.0, 64, 150);
        MotionLiveCore wrong_bounds_70_up_core =
            ConfiguredCore(&wrong_bounds_70_up, ValidRuntime(true, false));
        CHECK(!wrong_bounds_70_up_core.GetCapabilities().motion_allowed);
        CHECK(std::string(wrong_bounds_70_up_core.GetCapabilities().reason) == "profile_mismatch");
    }

    {
        MotionLivePreparedProfile right_profile = RightArmSymmetricCommissioningProfile(15.0);
        MotionLiveCore right_core = ConfiguredCore(&right_profile, ValidRuntime(true, false));
        int init_count = 0;
        int right_apply_count = 0;
        std::array<int, kPoseJointCount> right_last_apply{};
        right_core.SetRightArmInitializer([&](int home_degrees) {
            ++init_count;
            return home_degrees == kRightArmHomeDegrees;
        });
        right_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++right_apply_count;
            right_last_apply = target;
            return true;
        });
        CHECK(right_core.InitializeRightArm(43, right_profile.calibration_id, true).ok);
        CHECK(init_count == 1);

        auto right_armed = right_core.Arm(
            43, right_profile.calibration_id, true, "session_right_15_speed", 80000);
        CHECK(right_armed.ok);
        auto right_fast = right_core.Pose(
            43, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -15), 2, 80010);
        CHECK(!right_fast.ok);
        CHECK(std::string(right_fast.code) == "rate_limit");
        CHECK(!right_core.IsArmed());
        CHECK(right_apply_count == 0);

        right_armed = right_core.Arm(
            43, right_profile.calibration_id, true, "session_right_15_single", 81000);
        CHECK(right_armed.ok);
        auto right_two_joints = right_core.Pose(
            43, right_armed.session_id, 1, RightArmTarget(1, 0, 0, 0, -15), 1, 81010);
        CHECK(!right_two_joints.ok);
        CHECK(std::string(right_two_joints.code) == "commissioning_single_joint");
        CHECK(!right_core.IsArmed());
        CHECK(right_apply_count == 0);

        right_armed = right_core.Arm(
            43, right_profile.calibration_id, true, "session_right_15_step", 82000);
        CHECK(right_armed.ok);
        auto right_step = right_core.Pose(
            43, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -15), 1, 82010);
        CHECK(right_step.ok);
        CHECK(!right_step.should_apply);
        for (uint32_t seq = 2; seq <= 62; ++seq) {
            right_step = right_core.Pose(
                43, right_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, -15), 1,
                82010 + (seq - 1) * 250);
            CHECK(right_step.ok);
        }
        CHECK(right_apply_count > 0);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftHand)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightHand)] == 120);
        CHECK(right_step.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
              -15.0);
        const int apply_count_before_stop = right_apply_count;
        auto right_stop = right_core.Stop(43, right_armed.session_id, 63);
        CHECK(right_stop.stopped);
        CHECK(right_apply_count == apply_count_before_stop);
        CHECK(!right_core.IsArmed());
    }

    {
        MotionLivePreparedProfile right_profile =
            RightArmCommissioningProfile(-70.0, 15.0, 65, 150);
        MotionLiveCore right_core = ConfiguredCore(&right_profile, ValidRuntime(true, false));
        int init_count = 0;
        int right_apply_count = 0;
        std::array<int, kPoseJointCount> right_last_apply{};
        right_core.SetRightArmInitializer([&](int home_degrees) {
            ++init_count;
            return home_degrees == kRightArmHomeDegrees;
        });
        right_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++right_apply_count;
            right_last_apply = target;
            return true;
        });
        CHECK(right_core.InitializeRightArm(44, right_profile.calibration_id, true).ok);
        CHECK(init_count == 1);

        auto right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_speed", 90000);
        CHECK(right_armed.ok);
        auto right_fast = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -70), 2, 90010);
        CHECK(!right_fast.ok);
        CHECK(std::string(right_fast.code) == "rate_limit");
        CHECK(!right_core.IsArmed());
        CHECK(right_apply_count == 0);

        right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_single", 91000);
        CHECK(right_armed.ok);
        auto right_two_joints = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(1, 0, 0, 0, -70), 1, 91010);
        CHECK(!right_two_joints.ok);
        CHECK(std::string(right_two_joints.code) == "commissioning_single_joint");
        CHECK(!right_core.IsArmed());
        CHECK(right_apply_count == 0);

        right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_up", 92000);
        CHECK(right_armed.ok);
        auto right_step = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -70), 1, 92010);
        CHECK(right_step.ok);
        CHECK(!right_step.should_apply);
        for (uint32_t seq = 2; seq <= 282; ++seq) {
            right_step = right_core.Pose(
                44, right_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, -70), 1,
                92010 + (seq - 1) * 250);
            CHECK(right_step.ok);
        }
        CHECK(right_apply_count > 0);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightLeg)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightFoot)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kLeftHand)] == 90);
        CHECK(right_last_apply[Slot(ServoSlot::kRightHand)] == 65);
        CHECK(right_step.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
              -70.0);
        const int apply_count_before_stop = right_apply_count;
        auto right_stop = right_core.Stop(44, right_armed.session_id, 283);
        CHECK(right_stop.stopped);
        CHECK(right_apply_count == apply_count_before_stop);
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_to_down", 93000);
        CHECK(right_armed.ok);
        auto too_large_from_minus_70 = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, 15), 1, 93010);
        CHECK(!too_large_from_minus_70.ok);
        CHECK(std::string(too_large_from_minus_70.code) == "commissioning_single_joint");
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_return", 94000);
        CHECK(right_armed.ok);
        auto right_return = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, 0), 1, 94010);
        CHECK(right_return.ok);
        for (uint32_t seq = 2; seq <= 282; ++seq) {
            right_return = right_core.Pose(
                44, right_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, 0), 1,
                94010 + (seq - 1) * 250);
            CHECK(right_return.ok);
        }
        CHECK(right_last_apply[Slot(ServoSlot::kRightHand)] == kRightArmHomeDegrees);
        CHECK(right_return.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
              0.0);
        right_stop = right_core.Stop(44, right_armed.session_id, 283);
        CHECK(right_stop.stopped);
        CHECK(!right_core.IsArmed());

        right_armed = right_core.Arm(
            44, right_profile.calibration_id, true, "session_right_70_down15", 95000);
        CHECK(right_armed.ok);
        auto right_down = right_core.Pose(
            44, right_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, 15), 1, 95010);
        CHECK(right_down.ok);
        for (uint32_t seq = 2; seq <= 62; ++seq) {
            right_down = right_core.Pose(
                44, right_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, 15), 1,
                95010 + (seq - 1) * 250);
            CHECK(right_down.ok);
        }
        CHECK(right_last_apply[Slot(ServoSlot::kRightHand)] == 150);
        CHECK(right_down.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
              15.0);
        auto down_too_far = right_core.Pose(
            44, right_armed.session_id, 63, RightArmTarget(0, 0, 0, 0, 16), 1, 110260);
        CHECK(!down_too_far.ok);
        CHECK(std::string(down_too_far.code) == "limit_violation");
        CHECK(!right_core.IsArmed());
    }

    {
        MotionLivePreparedProfile impossible_production_editor =
            MotionEditorProfile(-70.0, 55.0, kRightArmHomeDegrees);
        MotionLiveCore impossible_core = ConfiguredCore(
            &impossible_production_editor, ValidRuntime(true, false, kRightArmHomeDegrees));
        auto impossible_caps = impossible_core.GetCapabilities();
        CHECK(!impossible_caps.motion_allowed);
        CHECK(std::string(impossible_caps.reason) == "profile_mismatch");

        MotionLivePreparedProfile editor_profile = MotionEditorProfile();
        MotionLiveCore editor_core =
            ConfiguredCore(&editor_profile, ValidRuntime(true, false, kRightArmHomeDegrees));
        int editor_init_count = 0;
        int editor_apply_count = 0;
        std::array<int, kPoseJointCount> editor_last_apply{};
        editor_core.SetRightArmInitializer([&](int home_degrees) {
            ++editor_init_count;
            return home_degrees == 125;
        });
        editor_core.SetHardwareApplier([&](const std::array<int, kPoseJointCount>& target) {
            ++editor_apply_count;
            editor_last_apply = target;
            return true;
        });

        auto editor_caps = editor_core.GetCapabilities();
        CHECK(!editor_caps.motion_allowed);
        CHECK(std::string(editor_caps.reason) == "right_arm_initialization_required");
        CHECK(!editor_caps.commissioning);
        CHECK(!editor_caps.calibrated);
        CHECK(editor_caps.right_arm_available);
        CHECK(!editor_caps.right_arm_initialized);
        CHECK(editor_caps.initialization_required);
        CHECK(std::string(editor_caps.initialization_op) == "initialize_right_arm");
        CHECK(editor_caps.joint_limit_count == 5);
        CHECK(std::string(editor_caps.joint_limits[0].id) == "leg_negative_x");
        CHECK(editor_caps.joint_limits[0].min_relative_degrees == -35.0);
        CHECK(editor_caps.joint_limits[0].max_relative_degrees == 35.0);
        CHECK(editor_caps.joint_limits[0].max_speed_dps == 5.0);
        CHECK(std::string(editor_caps.joint_limits[4].id) == "arm_positive_x");
        CHECK(editor_caps.joint_limits[4].min_relative_degrees == -70.0);
        CHECK(editor_caps.joint_limits[4].max_relative_degrees == 55.0);
        CHECK(editor_caps.joint_limits[4].max_speed_dps == 5.0);

        auto editor_init = editor_core.InitializeRightArm(
            45, editor_profile.calibration_id, true);
        CHECK(editor_init.ok);
        CHECK(editor_init_count == 1);
        editor_caps = editor_core.GetCapabilities();
        CHECK(editor_caps.motion_allowed);
        CHECK(!editor_caps.commissioning);
        CHECK(!editor_caps.calibrated);
        CHECK(editor_caps.right_arm_initialized);
        CHECK(!editor_caps.initialization_required);

        auto editor_armed = editor_core.Arm(
            45, editor_profile.calibration_id, true, "session_motion_editor_all", 120000);
        CHECK(editor_armed.ok);
        auto editor_pose = editor_core.Pose(
            45, editor_armed.session_id, 1, RightArmTarget(35, -35, 30, -30, 55), 5,
            120100);
        CHECK(editor_pose.ok);
        CHECK(!editor_pose.should_apply);
        CHECK(editor_core.IsArmed());
        CHECK(editor_apply_count == 0);
        CHECK(editor_pose.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
              0.0);

        for (uint32_t seq = 2; seq <= 12; ++seq) {
            auto editor_keepalive = editor_core.Keepalive(
                45, editor_armed.session_id, seq, 120100 + (seq - 1) * 250);
            CHECK(editor_keepalive.ok);
            CHECK(!editor_keepalive.should_apply);
            CHECK(editor_apply_count == 0);
        }

        editor_pose = editor_core.Pose(
            45, editor_armed.session_id, 13, RightArmTarget(35, -35, 30, -30, 55), 5,
            123100);
        CHECK(editor_pose.ok);
        CHECK(editor_pose.should_apply);
        CHECK(editor_apply_count == 1);
        CHECK(editor_last_apply[Slot(ServoSlot::kLeftHand)] == 90);
        CHECK(editor_last_apply[Slot(ServoSlot::kRightHand)] <= 127);
        CHECK(editor_pose.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] <=
              2.0);
        CHECK(editor_pose.commanded_pose.relative_degrees[Joint(JointIndex::kLegNegativeX)] <=
              2.0);

        editor_pose = editor_core.Pose(
            45, editor_armed.session_id, 14, RightArmTarget(-35, 35, -30, 30, -70), 5,
            123350);
        CHECK(editor_pose.ok);
        CHECK(editor_core.IsArmed());
        auto editor_stop = editor_core.Stop(45, editor_armed.session_id, 15);
        CHECK(editor_stop.stopped);
        CHECK(!editor_core.IsArmed());

        MotionLivePreparedProfile narrowed_editor =
            MotionEditorProfile(-70.0, 45.0, kRightArmHomeDegrees);
        MotionLiveCore narrowed_core = ConfiguredCore(
            &narrowed_editor, ValidRuntime(true, false, kRightArmHomeDegrees));
        narrowed_core.SetRightArmInitializer([](int home_degrees) {
            return home_degrees == kRightArmHomeDegrees;
        });
        narrowed_core.SetHardwareApplier([](const std::array<int, kPoseJointCount>&) {
            return true;
        });
        auto narrowed_caps = narrowed_core.GetCapabilities();
        CHECK(!narrowed_caps.motion_allowed);
        CHECK(std::string(narrowed_caps.reason) == "right_arm_initialization_required");
        CHECK(narrowed_caps.joint_limits[4].max_relative_degrees == 45.0);
        CHECK(narrowed_core.InitializeRightArm(46, narrowed_editor.calibration_id, true).ok);
        CHECK(narrowed_core.GetCapabilities().motion_allowed);

        MotionLivePreparedProfile bad_speed_editor = MotionEditorProfile(-70.0, 55.0, 125, 11.0);
        MotionLiveCore bad_speed_core =
            ConfiguredCore(&bad_speed_editor, ValidRuntime(true, false, kRightArmHomeDegrees));
        CHECK(!bad_speed_core.GetCapabilities().motion_allowed);
        CHECK(std::string(bad_speed_core.GetCapabilities().reason) == "profile_mismatch");

        MotionLivePreparedProfile bad_lower_editor = MotionEditorProfile();
        bad_lower_editor.joints[0].max_relative_degrees = 34.0;
        bad_lower_editor.joints[0].max_servo_degrees = 124;
        MotionLiveCore bad_lower_core =
            ConfiguredCore(&bad_lower_editor, ValidRuntime(true, false, kRightArmHomeDegrees));
        CHECK(!bad_lower_core.GetCapabilities().motion_allowed);
        CHECK(std::string(bad_lower_core.GetCapabilities().reason) == "profile_mismatch");
    }

    {
        MotionLivePreparedProfile right_profile = RightArmCommissioningProfile();
        MotionLiveCore fail_core = ConfiguredCore(&right_profile, ValidRuntime(true, false));
        int init_count = 0;
        fail_core.SetRightArmInitializer([&](int) {
            ++init_count;
            return false;
        });
        fail_core.SetHardwareApplier([](const std::array<int, kPoseJointCount>&) {
            return true;
        });
        auto failed_init = fail_core.InitializeRightArm(41, right_profile.calibration_id, true);
        CHECK(!failed_init.ok);
        CHECK(std::string(failed_init.code) == "right_arm_initialization_failed");
        CHECK(init_count == 1);
        auto caps_after_failure = fail_core.GetCapabilities();
        CHECK(!caps_after_failure.motion_allowed);
        CHECK(std::string(caps_after_failure.reason) == "right_arm_initialization_failed");
        auto retry_init = fail_core.InitializeRightArm(41, right_profile.calibration_id, true);
        CHECK(!retry_init.ok);
        CHECK(std::string(retry_init.code) == "right_arm_initialization_failed");
        CHECK(init_count == 1);
        auto arm_after_fail = fail_core.Arm(
            41, right_profile.calibration_id, true, "session_right_after_fail", 60000);
        CHECK(!arm_after_fail.ok);
        CHECK(std::string(arm_after_fail.code) == "right_arm_initialization_failed");
    }

    {
        MotionLivePreparedProfile right_profile = RightArmCommissioningProfile();
        MotionLiveCore rollback_core = ConfiguredCore(&right_profile, ValidRuntime(true, false));
        rollback_core.SetRightArmInitializer([](int home_degrees) {
            return home_degrees == kRightArmHomeDegrees;
        });
        rollback_core.SetHardwareApplier([](const std::array<int, kPoseJointCount>&) {
            return false;
        });
        CHECK(rollback_core.InitializeRightArm(42, right_profile.calibration_id, true).ok);
        auto rollback_armed = rollback_core.Arm(
            42, right_profile.calibration_id, true, "session_right_apply_fail", 70000);
        CHECK(rollback_armed.ok);
        auto rollback_pose = rollback_core.Pose(
            42, rollback_armed.session_id, 1, RightArmTarget(0, 0, 0, 0, -5), 1, 70010);
        CHECK(rollback_pose.ok);
        bool saw_apply_failure = false;
        for (uint32_t seq = 2; seq <= 7; ++seq) {
            rollback_pose = rollback_core.Pose(
                42, rollback_armed.session_id, seq, RightArmTarget(0, 0, 0, 0, -5), 1,
                70010 + (seq - 1) * 250);
            if (!rollback_pose.ok) {
                saw_apply_failure = true;
                break;
            }
        }
        CHECK(saw_apply_failure);
        CHECK(rollback_pose.stopped);
        CHECK(std::string(rollback_pose.code) == "hardware_apply_failed");
        CHECK(!rollback_core.IsArmed());
        CHECK(rollback_pose.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == 0.0);
    }

    MotionLiveRuntimeConfig wrong_runtime = ValidRuntime();
    wrong_runtime.joints[Slot(ServoSlot::kLeftLeg)].trim = 1;
    core.SetRuntimeConfig(wrong_runtime);
    CHECK(!core.GetCapabilities().motion_allowed);
    CHECK(std::string(core.GetCapabilities().reason) == "runtime_not_safe_neutral");

    std::cout << "motion_live_core_host_test: PASS\n";
    return 0;
}

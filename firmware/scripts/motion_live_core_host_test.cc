#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../main/boards/gosha-v1/motion_live_core.h"
#include "../main/boards/gosha-v1/motion_live_auth.h"

using gosha::motion_live::JointIndex;
using gosha::motion_live::MotionLiveCore;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionLiveRuntimeConfig;
using gosha::motion_live::MotionLiveTarget;
using gosha::motion_live::ServoSlot;
using gosha::motion_live::kActiveJointCount;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n";   \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

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
    };
}

MotionLiveRuntimeConfig ValidRuntime() {
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
    runtime.joints[static_cast<int>(ServoSlot::kRightHand)] = {-1, 0, 90, false};
    return runtime;
}

MotionLiveTarget Target(double left_leg, double right_leg, double left_foot, double right_foot) {
    MotionLiveTarget target;
    target.present[static_cast<int>(JointIndex::kLegNegativeX)] = true;
    target.present[static_cast<int>(JointIndex::kLegPositiveX)] = true;
    target.present[static_cast<int>(JointIndex::kFootNegativeX)] = true;
    target.present[static_cast<int>(JointIndex::kFootPositiveX)] = true;
    target.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] = left_leg;
    target.relative_degrees[static_cast<int>(JointIndex::kLegPositiveX)] = right_leg;
    target.relative_degrees[static_cast<int>(JointIndex::kFootNegativeX)] = left_foot;
    target.relative_degrees[static_cast<int>(JointIndex::kFootPositiveX)] = right_foot;
    return target;
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
    MotionLiveCore core;
    int apply_count = 0;
    std::array<int, kActiveJointCount> last_apply{};
    core.SetLocalOptInEnabled(true);
    core.SetPreparedProfile(&profile);
    core.SetRuntimeConfig(runtime);
    core.SetHardwareApplier([&](const std::array<int, kActiveJointCount>& target) {
        ++apply_count;
        last_apply = target;
        return true;
    });

    auto caps = core.GetCapabilities();
    CHECK(caps.motion_allowed);
    CHECK(caps.calibrated);
    CHECK(caps.max_rate_hz == 20);
    CHECK(std::string(caps.joint_limits[0].id) == "leg_negative_x");

    MotionLivePreparedProfile swapped_profile = ValidProfile(true);
    MotionLiveCore swapped_core;
    swapped_core.SetLocalOptInEnabled(true);
    swapped_core.SetPreparedProfile(&swapped_profile);
    swapped_core.SetRuntimeConfig(ValidRuntime());
    std::array<int, kActiveJointCount> swapped_apply{};
    swapped_core.SetHardwareApplier([&](const std::array<int, kActiveJointCount>& target) {
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
    CHECK(swapped_apply[static_cast<int>(ServoSlot::kLeftLeg)] == 90);
    CHECK(swapped_apply[static_cast<int>(ServoSlot::kRightLeg)] == 92);

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
    hand_target.present[static_cast<int>(JointIndex::kArmNegativeX)] = true;
    hand_target.relative_degrees[static_cast<int>(JointIndex::kArmNegativeX)] = 5;
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
    auto pose_reached = core.Keepalive(10, armed.session_id, 2, 5500);
    CHECK(pose_reached.ok);
    CHECK(pose_reached.should_apply);
    CHECK(last_apply[0] == 95);
    CHECK(last_apply[1] == 85);
    CHECK(pose_reached.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(pose.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kArmNegativeX)] == 0);
    CHECK(pose.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kArmPositiveX)] == 0);
    auto stop = core.Stop(10, armed.session_id, 3);
    CHECK(stop.stopped);
    CHECK(stop.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0006", 6000);
    CHECK(armed.ok);
    auto slow_pose = core.Pose(10, armed.session_id, 1, Target(10, -10, 0, 0), 5, 6100);
    CHECK(slow_pose.ok);
    CHECK(slow_pose.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] == 5.0);
    CHECK(slow_pose.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] < 10.0);
    auto slow_tick = core.Tick(6250);
    CHECK(!slow_tick.stopped);
    CHECK(slow_tick.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] < 10.0);
    stop = core.Stop(10, armed.session_id, 2);
    CHECK(stop.stopped);
    CHECK(!core.IsArmed());

    armed = core.Arm(10, profile.calibration_id, true, "session_live_0007", 7000);
    CHECK(armed.ok);
    auto nonzero = core.Pose(10, armed.session_id, 1, Target(10, -10, 0, 0), 10, 7250);
    CHECK(nonzero.ok);
    auto nonzero_reached = core.Keepalive(10, armed.session_id, 2, 7500);
    CHECK(nonzero_reached.ok);
    CHECK(nonzero_reached.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] == 10.0);
    stop = core.Stop(10, armed.session_id, 3);
    CHECK(stop.stopped);
    armed = core.Arm(10, profile.calibration_id, true, "session_live_0008", 8010);
    CHECK(armed.ok);
    auto retained = core.Keepalive(10, armed.session_id, 1, 8020);
    CHECK(retained.ok);
    CHECK(retained.commanded_pose.relative_degrees[static_cast<int>(JointIndex::kLegNegativeX)] == 10.0);
    stop = core.Stop(10, armed.session_id, 2);
    CHECK(stop.stopped);

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

    MotionLiveRuntimeConfig wrong_runtime = ValidRuntime();
    wrong_runtime.joints[static_cast<int>(ServoSlot::kLeftLeg)].trim = 1;
    core.SetRuntimeConfig(wrong_runtime);
    CHECK(!core.GetCapabilities().motion_allowed);
    CHECK(std::string(core.GetCapabilities().reason) == "runtime_not_safe_neutral");

    std::cout << "motion_live_core_host_test: PASS\n";
    return 0;
}

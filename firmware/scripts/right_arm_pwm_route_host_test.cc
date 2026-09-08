#include <array>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../main/boards/gosha-v1/motion_live_core.h"
#include "../main/boards/gosha-v1/otto_movements.h"
#include "host_stubs_motion_pwm/ledc_recorder.h"

using gosha::motion_live::JointIndex;
using gosha::motion_live::MotionLiveCore;
using gosha::motion_live::MotionLivePwmDiagnostics;
using gosha::motion_live::MotionLivePreparedProfile;
using gosha::motion_live::MotionLiveRuntimeConfig;
using gosha::motion_live::MotionLiveTarget;
using gosha::motion_live::ServoSlot;
using gosha::motion_live::kPoseJointCount;
using gosha::motion_live::kRightArmHomeDegrees;
using gosha::motion_live::kServoSlotKeys;
using gosha::motion_pwm_host::LedcChannelState;
using gosha::motion_pwm_host::LedcEvent;
using gosha::motion_pwm_host::LedcEventKind;

#define CHECK(condition)                                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n"; \
            return 1;                                                                    \
        }                                                                                \
    } while (false)

namespace {

constexpr int kLeftLegPin = 17;
constexpr int kRightLegPin = 39;
constexpr int kLeftFootPin = 18;
constexpr int kRightFootPin = 38;
constexpr int kLeftHandPin = 8;
constexpr int kRightHandPin = 12;
constexpr uint32_t kDuty90 = 614;
constexpr uint32_t kDuty130 = 796;
constexpr uint32_t kDuty135 = 819;

int Joint(JointIndex joint) {
    return static_cast<int>(joint);
}

int Slot(ServoSlot slot) {
    return static_cast<int>(slot);
}

MotionLivePreparedProfile RightArmCommissioningProfile() {
    return {
        "gosha-preview-v1",
        "223456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0",
        "cbcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        300,
        20,
        {{
            {"leg_negative_x", "left_leg", 0, kLeftLegPin, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"leg_positive_x", "right_leg", 1, kRightLegPin, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_negative_x", "left_foot", 2, kLeftFootPin, 0, 90, 1, -1.0, 1.0, 89, 91, 1.0},
            {"foot_positive_x", "right_foot", 3, kRightFootPin, 0, 90, -1, -1.0, 1.0, 89, 91, 1.0},
            {"arm_positive_x", "right_hand", 5, kRightHandPin, 0, kRightArmHomeDegrees,
             1, -5.0, 5.0, 130, 140, 1.0},
        }},
        "commissioning_right_arm",
        5,
    };
}

MotionLiveRuntimeConfig RuntimeBeforeRightArmInit() {
    MotionLiveRuntimeConfig runtime;
    runtime.board_is_gosha_v1 = true;
    runtime.no_motion_safe_profile = true;
    runtime.safe_neutral_boot_profile = true;
    runtime.safe_neutral_commanded = true;
    runtime.lower_body_attached = true;
    runtime.watchdog_tick_ready = true;
    runtime.joints[Slot(ServoSlot::kLeftLeg)] = {kLeftLegPin, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kRightLeg)] = {kRightLegPin, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kLeftFoot)] = {kLeftFootPin, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kRightFoot)] = {kRightFootPin, 0, 90, true};
    runtime.joints[Slot(ServoSlot::kLeftHand)] = {-1, 0, 45, false};
    runtime.joints[Slot(ServoSlot::kRightHand)] = {kRightHandPin, 0, kRightArmHomeDegrees,
                                                   false};
    return runtime;
}

MotionLiveTarget RightArmTarget(double right_arm_relative_degrees) {
    MotionLiveTarget target;
    target.present[Joint(JointIndex::kLegNegativeX)] = true;
    target.present[Joint(JointIndex::kLegPositiveX)] = true;
    target.present[Joint(JointIndex::kFootNegativeX)] = true;
    target.present[Joint(JointIndex::kFootPositiveX)] = true;
    target.present[Joint(JointIndex::kArmPositiveX)] = true;
    target.relative_degrees[Joint(JointIndex::kLegNegativeX)] = 0.0;
    target.relative_degrees[Joint(JointIndex::kLegPositiveX)] = 0.0;
    target.relative_degrees[Joint(JointIndex::kFootNegativeX)] = 0.0;
    target.relative_degrees[Joint(JointIndex::kFootPositiveX)] = 0.0;
    target.relative_degrees[Joint(JointIndex::kArmPositiveX)] = right_arm_relative_degrees;
    return target;
}

bool SawGpioInEvents(const std::vector<LedcEvent>& events, int gpio_num) {
    for (const auto& event : events) {
        if (event.gpio_num == gpio_num) {
            return true;
        }
    }
    return false;
}

bool AllConfiguredChannelsDistinct(const std::vector<int>& gpios) {
    std::set<int> channels;
    for (const int gpio : gpios) {
        const int channel = gosha::motion_pwm_host::ChannelForGpio(gpio);
        if (channel < 0 || !channels.insert(channel).second) {
            return false;
        }
    }
    return true;
}

bool LatestDutyEquals(int gpio_num, uint32_t expected_duty) {
    LedcChannelState state;
    if (!gosha::motion_pwm_host::LatestStateForGpio(gpio_num, &state)) {
        return false;
    }
    return state.duty == expected_duty;
}

bool EverySetDutyHasImmediateUpdateForGpio(int gpio_num) {
    const auto& events = gosha::motion_pwm_host::Events();
    for (size_t index = 0; index < events.size(); ++index) {
        const auto& event = events[index];
        if (event.kind != LedcEventKind::kSetDuty || event.gpio_num != gpio_num) {
            continue;
        }
        if (index + 1 >= events.size()) {
            return false;
        }
        const auto& update = events[index + 1];
        if (update.kind != LedcEventKind::kUpdateDuty ||
            update.gpio_num != event.gpio_num ||
            update.channel != event.channel ||
            update.duty != event.duty) {
            return false;
        }
    }
    return true;
}

bool EveryLegDutyStayedNeutral() {
    const auto set_duty_events = gosha::motion_pwm_host::EventsOfKind(LedcEventKind::kSetDuty);
    for (const auto& event : set_duty_events) {
        if (event.gpio_num == kLeftLegPin || event.gpio_num == kRightLegPin ||
            event.gpio_num == kLeftFootPin || event.gpio_num == kRightFootPin) {
            if (event.duty != kDuty90) {
                return false;
            }
        }
    }
    return true;
}

MotionLivePwmDiagnostics DiagnosticsFromOtto(const Otto& otto) {
    MotionLivePwmDiagnostics diagnostics;
    for (int servo_index = 0; servo_index < SERVO_COUNT; ++servo_index) {
        const auto source = otto.GetLiveServoDiagnostics(servo_index);
        auto& target = diagnostics.servos[servo_index];
        target.id = kServoSlotKeys[servo_index];
        target.servo_key = kServoSlotKeys[servo_index];
        target.joint_id = nullptr;
        target.available = source.available;
        target.attached = source.attached;
        target.pin = source.pin;
        target.channel = source.channel;
        target.frequency_available = source.frequency_available;
        target.frequency_hz = source.frequency_hz;
        target.duty_available = source.duty_available;
        target.duty = source.duty;
        target.last_write_available = source.last_write_available;
        target.requested_angle_degrees = source.requested_angle_degrees;
        target.software_angle_degrees = source.software_angle_degrees;
        target.applied_angle_degrees = source.applied_angle_degrees;
        target.applied_duty = source.applied_duty;
        target.last_write_ms = source.last_write_ms;
        target.last_write_ok = source.last_write_ok;
        target.skipped_unattached = source.skipped_unattached;
    }
    return diagnostics;
}

void ConfigureCoreWithOtto(MotionLiveCore* core, MotionLivePreparedProfile* profile,
                           Otto* otto) {
    core->SetLocalOptInEnabled(true);
    core->SetPreparedProfile(profile);
    core->SetRuntimeConfig(RuntimeBeforeRightArmInit());
    core->SetRightArmInitializer([otto](int home_degrees) {
        return otto->AttachRightHandAtHome(home_degrees);
    });
    core->SetHardwareApplier([otto](const std::array<int, kPoseJointCount>& target) {
        int servo_target[SERVO_COUNT] = {
            target[Slot(ServoSlot::kLeftLeg)],
            target[Slot(ServoSlot::kRightLeg)],
            target[Slot(ServoSlot::kLeftFoot)],
            target[Slot(ServoSlot::kRightFoot)],
            target[Slot(ServoSlot::kLeftHand)],
            target[Slot(ServoSlot::kRightHand)],
        };
        return otto->ApplyLiveServoPositions(servo_target);
    });
    core->SetPwmDiagnosticsProvider([otto]() {
        return DiagnosticsFromOtto(*otto);
    });
}

int ExerciseRightArmDutyRoute() {
    gosha::motion_pwm_host::ResetRecorder();
    gosha::motion_pwm_host::SetTimeMs(0);

    Otto otto;
    otto.Init(kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin, -1,
              kRightHandPin, false);
    otto.SetTrims(0, 0, 0, 0, 0, 0);
    CHECK(gosha::motion_pwm_host::EventsOfKind(LedcEventKind::kChannelConfig).empty());

    otto.AttachLegsFeetServos();
    const auto lower_body_attach_events =
        gosha::motion_pwm_host::EventsOfKind(LedcEventKind::kChannelConfig);
    CHECK(lower_body_attach_events.size() == 4);
    CHECK(SawGpioInEvents(lower_body_attach_events, kLeftLegPin));
    CHECK(SawGpioInEvents(lower_body_attach_events, kRightLegPin));
    CHECK(SawGpioInEvents(lower_body_attach_events, kLeftFootPin));
    CHECK(SawGpioInEvents(lower_body_attach_events, kRightFootPin));
    CHECK(!SawGpioInEvents(lower_body_attach_events, kLeftHandPin));
    CHECK(!SawGpioInEvents(lower_body_attach_events, kRightHandPin));
    CHECK(AllConfiguredChannelsDistinct(
        {kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin}));

    otto.HoldLegsFeetAtNeutral();
    CHECK(LatestDutyEquals(kLeftLegPin, kDuty90));
    CHECK(LatestDutyEquals(kRightLegPin, kDuty90));
    CHECK(LatestDutyEquals(kLeftFootPin, kDuty90));
    CHECK(LatestDutyEquals(kRightFootPin, kDuty90));
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kLeftHandPin));
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kRightHandPin));

    MotionLivePreparedProfile profile = RightArmCommissioningProfile();
    MotionLiveCore core;
    ConfigureCoreWithOtto(&core, &profile, &otto);

    const auto caps_before_init = core.GetCapabilities();
    CHECK(!caps_before_init.motion_allowed);
    CHECK(std::string(caps_before_init.reason) == "right_arm_initialization_required");
    CHECK(caps_before_init.right_arm_available);
    CHECK(!caps_before_init.right_arm_initialized);

    const size_t event_count_before_right_init = gosha::motion_pwm_host::Events().size();
    const auto init = core.InitializeRightArm(70, profile.calibration_id, true);
    CHECK(init.ok);
    CHECK(!init.should_apply);
    CHECK(gosha::motion_pwm_host::Events().size() > event_count_before_right_init);
    CHECK(gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kChannelConfig,
                                                kRightHandPin).size() == 1);
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kLeftHandPin));
    const auto right_updates_after_init =
        gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty, kRightHandPin);
    CHECK(right_updates_after_init.size() == 1);
    CHECK(right_updates_after_init.back().duty == kDuty135);
    CHECK(EverySetDutyHasImmediateUpdateForGpio(kRightHandPin));
    CHECK(LatestDutyEquals(kRightHandPin, kDuty135));
    CHECK(AllConfiguredChannelsDistinct(
        {kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin, kRightHandPin}));
    const auto right_init_diagnostics = otto.GetLiveServoDiagnostics(RIGHT_HAND);
    CHECK(right_init_diagnostics.available);
    CHECK(right_init_diagnostics.attached);
    CHECK(right_init_diagnostics.pin == kRightHandPin);
    CHECK(right_init_diagnostics.channel ==
          gosha::motion_pwm_host::ChannelForGpio(kRightHandPin));
    CHECK(right_init_diagnostics.frequency_available);
    CHECK(right_init_diagnostics.frequency_hz == 50);
    CHECK(right_init_diagnostics.duty_available);
    CHECK(right_init_diagnostics.duty == kDuty135);
    CHECK(right_init_diagnostics.last_write_available);
    CHECK(right_init_diagnostics.requested_angle_degrees == kRightArmHomeDegrees);
    CHECK(right_init_diagnostics.software_angle_degrees == kRightArmHomeDegrees);
    CHECK(right_init_diagnostics.applied_angle_degrees == kRightArmHomeDegrees);
    CHECK(right_init_diagnostics.applied_duty == kDuty135);
    CHECK(right_init_diagnostics.last_write_ok);
    CHECK(!right_init_diagnostics.skipped_unattached);

    const auto caps_after_init = core.GetCapabilities();
    CHECK(caps_after_init.motion_allowed);
    CHECK(caps_after_init.right_arm_initialized);
    CHECK(caps_after_init.servo_degrees[Slot(ServoSlot::kRightHand)] == kRightArmHomeDegrees);
    CHECK(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].attached);
    CHECK(std::string(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].id) ==
          "right_hand");
    CHECK(std::string(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].servo_key) ==
          "right_hand");
    CHECK(std::string(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].joint_id) ==
          "arm_positive_x");
    CHECK(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].duty_available);
    CHECK(caps_after_init.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].duty == kDuty135);

    const auto armed = core.Arm(70, profile.calibration_id, true,
                                "session_right_pwm_route_0001", 100000);
    CHECK(armed.ok);

    const size_t set_duty_count_before_pose =
        gosha::motion_pwm_host::EventsOfKind(LedcEventKind::kSetDuty).size();
    const size_t right_update_count_before_pose =
        gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty,
                                              kRightHandPin).size();
    int first_changed_seq = -1;
    uint32_t first_changed_duty = 0;
    auto result = armed;
    for (uint32_t seq = 1; seq <= 20; ++seq) {
        const uint64_t now_ms = 100000 + seq * 250;
        gosha::motion_pwm_host::SetTimeMs(static_cast<int64_t>(now_ms));
        result = core.Pose(70, armed.session_id, seq, RightArmTarget(-5.0), 1.0, now_ms);
        CHECK(result.ok);
        if (seq == 1) {
            CHECK(!result.should_apply);
            CHECK(result.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] ==
                  0.0);
        }
        CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kLeftHandPin));
        CHECK(EveryLegDutyStayedNeutral());
        CHECK(EverySetDutyHasImmediateUpdateForGpio(kRightHandPin));
        CHECK(LatestDutyEquals(kLeftLegPin, kDuty90));
        CHECK(LatestDutyEquals(kRightLegPin, kDuty90));
        CHECK(LatestDutyEquals(kLeftFootPin, kDuty90));
        CHECK(LatestDutyEquals(kRightFootPin, kDuty90));
        const uint32_t right_duty = gosha::motion_pwm_host::LatestDutyForGpio(kRightHandPin);
        if (first_changed_seq < 0 && right_duty != kDuty135) {
            first_changed_seq = static_cast<int>(seq);
            first_changed_duty = right_duty;
        }
    }

    CHECK(first_changed_seq == 4);
    CHECK(first_changed_duty < kDuty135);
    CHECK(first_changed_duty > kDuty130);
    CHECK(gosha::motion_pwm_host::EventsOfKind(LedcEventKind::kSetDuty).size() >
          set_duty_count_before_pose);
    const auto right_updates_after_pose =
        gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty, kRightHandPin);
    CHECK(right_updates_after_pose.size() > right_update_count_before_pose);
    CHECK(right_updates_after_pose.front().duty == kDuty135);
    CHECK(right_updates_after_pose.back().duty == kDuty130);
    for (size_t index = 1; index < right_updates_after_pose.size(); ++index) {
        CHECK(right_updates_after_pose[index].duty <= right_updates_after_pose[index - 1].duty);
    }
    CHECK(result.should_apply);
    CHECK(result.servo_degrees[Slot(ServoSlot::kRightHand)] == 130);
    CHECK(result.servo_degrees[Slot(ServoSlot::kLeftHand)] == 90);
    CHECK(result.commanded_pose.relative_degrees[Joint(JointIndex::kArmPositiveX)] == -5.0);
    CHECK(result.commanded_pose.relative_degrees[Joint(JointIndex::kArmNegativeX)] == 0.0);
    CHECK(LatestDutyEquals(kRightHandPin, kDuty130));
    const auto right_final_diagnostics = otto.GetLiveServoDiagnostics(RIGHT_HAND);
    CHECK(right_final_diagnostics.frequency_hz == 50);
    CHECK(right_final_diagnostics.duty == kDuty130);
    CHECK(right_final_diagnostics.requested_angle_degrees == 130);
    CHECK(right_final_diagnostics.software_angle_degrees == 130);
    CHECK(right_final_diagnostics.applied_angle_degrees == 130);
    CHECK(right_final_diagnostics.applied_duty == kDuty130);
    CHECK(result.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].last_write_ok);
    CHECK(result.pwm_diagnostics.servos[Slot(ServoSlot::kRightHand)].duty == kDuty130);

    const size_t event_count_before_stop = gosha::motion_pwm_host::Events().size();
    const size_t right_update_count_before_stop =
        gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty,
                                              kRightHandPin).size();
    const auto stop = core.Stop(70, armed.session_id, 21);
    CHECK(stop.stopped);
    CHECK(!core.IsArmed());
    CHECK(gosha::motion_pwm_host::Events().size() == event_count_before_stop);
    CHECK(gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty,
                                                kRightHandPin).size() ==
          right_update_count_before_stop);
    CHECK(LatestDutyEquals(kRightHandPin, kDuty130));
    CHECK(EveryLegDutyStayedNeutral());

    std::cout << "right-arm route: lower-body attach=4, right GPIO12 init duty="
              << kDuty135
              << ", channels LL/RL/LF/RF/RH="
              << gosha::motion_pwm_host::ChannelForGpio(kLeftLegPin) << "/"
              << gosha::motion_pwm_host::ChannelForGpio(kRightLegPin) << "/"
              << gosha::motion_pwm_host::ChannelForGpio(kLeftFootPin) << "/"
              << gosha::motion_pwm_host::ChannelForGpio(kRightFootPin) << "/"
              << gosha::motion_pwm_host::ChannelForGpio(kRightHandPin)
              << ", first -5deg duty change at seq=" << first_changed_seq
              << ", final commanded servo=130\n";
    return 0;
}

int ExerciseWatchdogNoExtraPwm() {
    gosha::motion_pwm_host::ResetRecorder();
    gosha::motion_pwm_host::SetTimeMs(0);

    Otto otto;
    otto.Init(kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin, -1,
              kRightHandPin, false);
    otto.SetTrims(0, 0, 0, 0, 0, 0);
    otto.AttachLegsFeetServos();
    otto.HoldLegsFeetAtNeutral();

    MotionLivePreparedProfile profile = RightArmCommissioningProfile();
    MotionLiveCore core;
    ConfigureCoreWithOtto(&core, &profile, &otto);
    CHECK(core.InitializeRightArm(71, profile.calibration_id, true).ok);

    const auto armed = core.Arm(71, profile.calibration_id, true,
                                "session_right_pwm_watchdog", 200000);
    CHECK(armed.ok);
    gosha::motion_pwm_host::SetTimeMs(200010);
    const auto pose = core.Pose(71, armed.session_id, 1, RightArmTarget(-5.0), 1.0, 200010);
    CHECK(pose.ok);
    CHECK(!pose.should_apply);
    CHECK(LatestDutyEquals(kRightHandPin, kDuty135));

    const size_t event_count_before_watchdog = gosha::motion_pwm_host::Events().size();
    const size_t right_update_count_before_watchdog =
        gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty,
                                              kRightHandPin).size();
    const auto watchdog = core.Tick(200311);
    CHECK(watchdog.stopped);
    CHECK(std::string(watchdog.code) == "watchdog_timeout");
    CHECK(!watchdog.hardware_changed);
    CHECK(!core.IsArmed());
    CHECK(gosha::motion_pwm_host::Events().size() == event_count_before_watchdog);
    CHECK(gosha::motion_pwm_host::EventsForGpio(LedcEventKind::kUpdateDuty,
                                                kRightHandPin).size() ==
          right_update_count_before_watchdog);
    CHECK(LatestDutyEquals(kRightHandPin, kDuty135));
    CHECK(EveryLegDutyStayedNeutral());
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kLeftHandPin));

    std::cout << "watchdog route: pending right-arm target timed out without extra PWM\n";
    return 0;
}

int ExerciseLiveDetachedFailsClosed() {
    gosha::motion_pwm_host::ResetRecorder();
    gosha::motion_pwm_host::SetTimeMs(0);

    Otto otto;
    otto.Init(kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin, -1,
              kRightHandPin, false);
    otto.SetTrims(0, 0, 0, 0, 0, 0);

    MotionLivePreparedProfile profile = RightArmCommissioningProfile();
    MotionLiveCore core;
    ConfigureCoreWithOtto(&core, &profile, &otto);
    CHECK(core.InitializeRightArm(72, profile.calibration_id, true).ok);

    const auto armed = core.Arm(72, profile.calibration_id, true,
                                "session_right_pwm_detached", 300000);
    CHECK(armed.ok);
    auto pose = armed;
    for (uint32_t seq = 1; seq <= 3; ++seq) {
        const uint64_t now_ms = 300000 + seq * 250;
        gosha::motion_pwm_host::SetTimeMs(static_cast<int64_t>(now_ms));
        pose = core.Pose(72, armed.session_id, seq, RightArmTarget(-5.0), 1.0, now_ms);
        CHECK(pose.ok);
        CHECK(!pose.should_apply);
    }
    gosha::motion_pwm_host::SetTimeMs(301000);
    pose = core.Pose(72, armed.session_id, 4, RightArmTarget(-5.0), 1.0, 301000);
    CHECK(!pose.ok);
    CHECK(pose.stopped);
    CHECK(std::string(pose.code) == "hardware_apply_failed");
    CHECK(!core.IsArmed());
    const auto left_leg_diagnostics = otto.GetLiveServoDiagnostics(LEFT_LEG);
    CHECK(left_leg_diagnostics.last_write_available);
    CHECK(!left_leg_diagnostics.last_write_ok);
    CHECK(left_leg_diagnostics.skipped_unattached);
    CHECK(!left_leg_diagnostics.attached);
    CHECK(pose.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].skipped_unattached);
    CHECK(!pose.pwm_diagnostics.servos[Slot(ServoSlot::kLeftLeg)].last_write_ok);

    std::cout << "detached route: Live apply fails closed when an oscillator is not attached\n";
    return 0;
}

int ExerciseLeftHandFailClosedGuard() {
    gosha::motion_pwm_host::ResetRecorder();

    Otto bad_otto;
    bad_otto.Init(kLeftLegPin, kRightLegPin, kLeftFootPin, kRightFootPin, kLeftHandPin,
                  kRightHandPin, false);
    int target[SERVO_COUNT] = {90, 90, 90, 90, 45, kRightArmHomeDegrees};
    CHECK(!bad_otto.ApplyLiveServoPositions(target));
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kLeftHandPin));
    CHECK(!gosha::motion_pwm_host::HasAnyEventForGpio(kRightHandPin));

    std::cout << "left-hand guard: ApplyLiveServoPositions rejects left-hand pinset\n";
    return 0;
}

}  // namespace

int main() {
    CHECK(ExerciseRightArmDutyRoute() == 0);
    CHECK(ExerciseWatchdogNoExtraPwm() == 0);
    CHECK(ExerciseLiveDetachedFailsClosed() == 0);
    CHECK(ExerciseLeftHandFailClosedGuard() == 0);
    std::cout << "right_arm_pwm_route_host_test: PASS (host LEDC recorder only; no real PWM measured)\n";
    return 0;
}

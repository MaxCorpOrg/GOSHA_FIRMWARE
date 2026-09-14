#include "legacy_motion_plan.h"
#include <algorithm>
#include <cmath>

namespace gosha::motion_live {
namespace {
constexpr int SERVO_COUNT = 6, LEFT_LEG = 0, RIGHT_LEG = 1;
constexpr int LEFT_FOOT = 2, RIGHT_FOOT = 3, LEFT_HAND = 4, RIGHT_HAND = 5;
constexpr int HAND_HOME_POSITION = 45, LEFT = 1, RIGHT = -1, FORWARD = 1, BACKWARD = -1;
constexpr double kPi = 3.14159265358979323846;
double DEG2RAD(double degrees) { return degrees * kPi / 180.0; }
}

const std::vector<LegacyMotionPlan::Entry>& LegacyMotionPlan::Catalog() {
    static const std::vector<Entry> entries = {
        {"builtin/walk_forward", "Шаги вперёд", true},
        {"builtin/walk_backward", "Шаги назад", true},
        {"builtin/turn_left", "Повернуть влево", true},
        {"builtin/turn_right", "Повернуть вправо", true},
        {"builtin/jump", "Подпрыгнуть", false},
        {"builtin/swing", "Покачаться", false},
        {"builtin/moonwalk_left", "Лунная походка влево", false},
        {"builtin/moonwalk_right", "Лунная походка вправо", false},
        {"builtin/bend_left", "Наклониться влево", false},
        {"builtin/bend_right", "Наклониться вправо", false},
        {"builtin/shake_leg_left", "Потрясти левой ногой", false},
        {"builtin/shake_leg_right", "Потрясти правой ногой", false},
        {"builtin/updown", "Подняться и опуститься", false},
        {"builtin/whirlwind_leg", "Вращение ногой", true},
        {"builtin/sit", "Сесть", false},
        {"builtin/showcase", "Показать набор движений (правая рука)", true},
        {"builtin/home", "Вернуться в нейтральную стойку", false},
        {"builtin/hands_up", "Поднять правую руку и удерживать", true},
        {"builtin/hands_down", "Опустить правую руку", true},
        {"builtin/hand_wave", "Помахать правой рукой", true},
        {"builtin/windmill", "Мельница правой рукой", true},
        {"builtin/takeoff", "Взлёт (правая рука)", true},
        {"builtin/fitness", "Физкультура (правая рука)", true},
        {"builtin/greeting", "Поздороваться правой рукой с наклоном", true},
        {"builtin/shy", "Смущение (правая рука)", true},
        {"builtin/radio_calisthenics", "Зарядка (правая рука)", true},
        {"builtin/magic_circle", "Танец с вращением (правая рука)", true},
    };
    return entries;
}

bool LegacyMotionPlan::Build(const std::string& id, const Pose& initial) {
    segments_.clear(); position_ = initial; left_hold_ = initial[LEFT_HAND];
    duration_ms_ = 0; valid_ = true; is_otto_resting_ = false;
    // Fixed presets use the former MCP defaults: 3 cycles, 700 ms, amplitude 30,
    // arm swing 50. Direction variants are named; remote angles/speeds stay closed.
    if (id == "builtin/walk_forward") Walk(3, 700, FORWARD, 50);
    else if (id == "builtin/walk_backward") Walk(3, 700, BACKWARD, 50);
    else if (id == "builtin/turn_left") Turn(3, 700, LEFT, 50);
    else if (id == "builtin/turn_right") Turn(3, 700, RIGHT, 50);
    else if (id == "builtin/jump") Jump(3, 700);
    else if (id == "builtin/swing") Swing(3, 700, 30);
    else if (id == "builtin/moonwalk_left") Moonwalker(3, 700, 30, LEFT);
    else if (id == "builtin/moonwalk_right") Moonwalker(3, 700, 30, RIGHT);
    else if (id == "builtin/bend_left") Bend(3, 700, LEFT);
    else if (id == "builtin/bend_right") Bend(3, 700, RIGHT);
    else if (id == "builtin/shake_leg_left") ShakeLeg(3, 700, LEFT);
    else if (id == "builtin/shake_leg_right") ShakeLeg(3, 700, RIGHT);
    else if (id == "builtin/updown") UpDown(3, 700, 30);
    else if (id == "builtin/whirlwind_leg") WhirlwindLeg(3, 700, 30);
    else if (id == "builtin/sit") Sit();
    else if (id == "builtin/showcase") Showcase();
    else if (id == "builtin/home") Home(true);
    else if (id == "builtin/hands_up") HandsUp(700, RIGHT);
    else if (id == "builtin/hands_down") HandsDown(700, RIGHT);
    else if (id == "builtin/hand_wave") HandWave(RIGHT);
    else if (id == "builtin/windmill") Windmill(3, 700, 30);
    else if (id == "builtin/takeoff") Takeoff(3, 700, 30);
    else if (id == "builtin/fitness") Fitness(3, 700, 30);
    else if (id == "builtin/greeting") Greeting(RIGHT, 3);
    else if (id == "builtin/shy") Shy(RIGHT, 3);
    else if (id == "builtin/radio_calisthenics") RadioCalisthenics();
    else if (id == "builtin/magic_circle") MagicCircle();
    else return false;
    // Preserve the original ActionTask postamble, including held sit/raised arm.
    if (id != "builtin/sit" && id != "builtin/home") Home(id != "builtin/hands_up");
    return valid_ && duration_ms_ > 0 && duration_ms_ <= 120000;
}

void LegacyMotionPlan::Append(Segment segment) {
    if (segment.duration_ms == 0) return;
    if (segments_.size() >= 128 || segment.duration_ms > 120000 ||
        duration_ms_ + segment.duration_ms > 120000) { valid_ = false; return; }
    segment.start_ms = duration_ms_;
    segment.from[LEFT_HAND] = segment.to[LEFT_HAND] = left_hold_;
    segment.amplitude[LEFT_HAND] = 0;
    duration_ms_ += segment.duration_ms;
    segments_.push_back(segment);
    Sample(duration_ms_, &position_);
}

void LegacyMotionPlan::MoveServos(int time_ms, int target[]) {
    is_otto_resting_ = false;
    Segment segment; segment.duration_ms = std::max(1, time_ms); segment.from = position_;
    for (int i = 0; i < SERVO_COUNT; ++i) segment.to[i] = std::clamp(target[i], 0, 180);
    Append(segment);
}

void LegacyMotionPlan::Hold(int time_ms) {
    Segment segment; segment.duration_ms = std::max(0, time_ms);
    segment.from = segment.to = position_; Append(segment);
}

void LegacyMotionPlan::Execute(int amplitude[], int offset[], int period,
                                double phase[], float steps) {
    int center[SERVO_COUNT];
    for (int i = 0; i < SERVO_COUNT; ++i) center[i] = offset[i] + 90;
    Execute2(amplitude, center, period, phase, steps);
}

void LegacyMotionPlan::Execute2(int amplitude[], int center[], int period,
                                 double phase[], float steps) {
    is_otto_resting_ = false;
    if (period < 100 || steps <= 0) { valid_ = false; return; }
    Segment segment; segment.duration_ms = static_cast<uint32_t>(period * steps);
    segment.period_ms = period;
    for (int i = 0; i < SERVO_COUNT; ++i) {
        segment.to[i] = center[i]; segment.amplitude[i] = amplitude[i]; segment.phase[i] = phase[i];
    }
    Append(segment);
    // Former Execute/Execute2 inserted 10 ms after each full cycle and a final
    // zero-cycle call plus 10 ms. Keep the total scheduling pause explicitly.
    Hold((static_cast<int>(steps) + 2) * 10);
}

bool LegacyMotionPlan::Sample(uint32_t elapsed_ms, Pose* pose) const {
    if (!valid_ || segments_.empty() || !pose) return false;
    const Segment* selected = &segments_.back();
    for (const auto& segment : segments_) {
        if (elapsed_ms < segment.start_ms + segment.duration_ms) { selected = &segment; break; }
    }
    const auto& s = *selected;
    const uint32_t t = std::min(elapsed_ms - std::min(elapsed_ms, s.start_ms), s.duration_ms);
    for (int i = 0; i < SERVO_COUNT; ++i) {
        double value;
        if (s.period_ms) {
            const double phase = 2 * kPi * static_cast<double>(t) / s.period_ms + s.phase[i];
            value = s.to[i] + s.amplitude[i] * std::sin(phase);
        } else {
            value = s.from[i] + (s.to[i] - s.from[i]) * static_cast<double>(t) / s.duration_ms;
        }
        (*pose)[i] = std::clamp(static_cast<int>(std::lround(value)), 0, 180);
    }
    (*pose)[LEFT_HAND] = left_hold_;
    return true;
}

#include "legacy_motion_routines.inc"
}  // namespace gosha::motion_live

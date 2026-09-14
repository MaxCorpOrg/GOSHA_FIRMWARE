#ifndef GOSHA_LEGACY_MOTION_PLAN_H_
#define GOSHA_LEGACY_MOTION_PLAN_H_
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gosha::motion_live {

// Trusted firmware choreography only. This class never parses a network pose.
class LegacyMotionPlan {
public:
    using Pose = std::array<int, 6>;  // Otto servo slots; left hand is always held.
    struct Entry { const char* id; const char* name; bool uses_right_arm; };
    static const std::vector<Entry>& Catalog();
    bool Build(const std::string& id, const Pose& initial);
    bool Sample(uint32_t elapsed_ms, Pose* pose) const;
    uint32_t duration_ms() const { return duration_ms_; }

private:
    struct Segment {
        uint32_t start_ms = 0, duration_ms = 0;
        int period_ms = 0;
        Pose from{}, to{}, amplitude{};
        std::array<double, 6> phase{};
    };
    void Append(Segment segment);
    void MoveServos(int time_ms, int target[]);
    void Hold(int time_ms);
    void Execute(int amplitude[], int offset[], int period,
                 double phase[], float steps);
    void Execute2(int amplitude[], int center[], int period,
                  double phase[], float steps);
    int Position(int slot) const { return position_[slot]; }
    bool GetRestState() const { return is_otto_resting_; }
    void SetRestState(bool value) { is_otto_resting_ = value; }
    void Home(bool hands_down = true);
    void Jump(float steps, int period);
    void Walk(float steps, int period, int dir, int amount);
    void Turn(float steps, int period, int dir, int amount);
    void Bend(int steps, int period, int dir);
    void ShakeLeg(int steps, int period, int dir);
    void Sit();
    void UpDown(float steps, int period, int height);
    void Swing(float steps, int period, int height);
    void Moonwalker(float steps, int period, int height, int dir);
    void WhirlwindLeg(float steps, int period, int amplitude);
    void HandsUp(int period, int dir);
    void HandsDown(int period, int dir);
    void HandWave(int dir);
    void Windmill(float steps, int period, int amplitude);
    void Takeoff(float steps, int period, int amplitude);
    void Fitness(float steps, int period, int amplitude);
    void Greeting(int dir, float steps);
    void Shy(int dir, float steps);
    void RadioCalisthenics();
    void MagicCircle();
    void Showcase();
    std::vector<Segment> segments_;
    Pose position_{};
    int left_hold_ = 90;
    uint32_t duration_ms_ = 0;
    bool valid_ = true;
    bool is_otto_resting_ = false;
    const bool right_arm_available_ = true;
};
}  // namespace gosha::motion_live
#endif

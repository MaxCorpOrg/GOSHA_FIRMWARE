#ifndef GOSHA_HOST_STUBS_MOTION_PWM_LEDC_RECORDER_H_
#define GOSHA_HOST_STUBS_MOTION_PWM_LEDC_RECORDER_H_

#include <array>
#include <cstdint>
#include <vector>

namespace gosha::motion_pwm_host {

enum class LedcEventKind {
    kTimerConfig,
    kChannelConfig,
    kSetDuty,
    kUpdateDuty,
    kStop,
    kDelay,
};

struct LedcEvent {
    LedcEventKind kind = LedcEventKind::kTimerConfig;
    int gpio_num = -999;
    int channel = -1;
    uint32_t duty = 0;
    uint32_t value = 0;
    int64_t time_us = 0;
};

struct LedcChannelState {
    bool configured = false;
    int gpio_num = -999;
    uint32_t duty = 0;
    uint32_t update_count = 0;
    uint32_t stop_count = 0;
};

void ResetRecorder();
void SetTimeMs(int64_t now_ms);
void AdvanceTimeMs(uint32_t delta_ms);
int64_t NowUs();

const std::vector<LedcEvent>& Events();
std::vector<LedcEvent> EventsOfKind(LedcEventKind kind);
std::vector<LedcEvent> EventsForGpio(LedcEventKind kind, int gpio_num);
bool LatestStateForGpio(int gpio_num, LedcChannelState* state);
int ChannelForGpio(int gpio_num);
bool HasAnyEventForGpio(int gpio_num);
uint32_t LatestDutyForGpio(int gpio_num);

}  // namespace gosha::motion_pwm_host

#endif  // GOSHA_HOST_STUBS_MOTION_PWM_LEDC_RECORDER_H_

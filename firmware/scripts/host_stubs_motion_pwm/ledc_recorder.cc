#include "ledc_recorder.h"

#include <algorithm>

#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace gosha::motion_pwm_host {

namespace {

std::vector<LedcEvent>& MutableEvents() {
    static std::vector<LedcEvent> events;
    return events;
}

std::array<LedcChannelState, 8>& MutableChannels() {
    static std::array<LedcChannelState, 8> channels;
    return channels;
}

int64_t& MutableNowUs() {
    static int64_t now_us = 0;
    return now_us;
}

void Record(LedcEventKind kind, int gpio_num, int channel, uint32_t duty,
            uint32_t value = 0) {
    MutableEvents().push_back({kind, gpio_num, channel, duty, value, MutableNowUs()});
}

}  // namespace

void ResetRecorder() {
    MutableEvents().clear();
    for (auto& channel : MutableChannels()) {
        channel = {};
    }
    MutableNowUs() = 0;
}

void SetTimeMs(int64_t now_ms) {
    MutableNowUs() = now_ms * 1000;
}

void AdvanceTimeMs(uint32_t delta_ms) {
    MutableNowUs() += static_cast<int64_t>(delta_ms) * 1000;
}

int64_t NowUs() {
    return MutableNowUs();
}

const std::vector<LedcEvent>& Events() {
    return MutableEvents();
}

std::vector<LedcEvent> EventsOfKind(LedcEventKind kind) {
    std::vector<LedcEvent> matches;
    for (const auto& event : MutableEvents()) {
        if (event.kind == kind) {
            matches.push_back(event);
        }
    }
    return matches;
}

std::vector<LedcEvent> EventsForGpio(LedcEventKind kind, int gpio_num) {
    std::vector<LedcEvent> matches;
    for (const auto& event : MutableEvents()) {
        if (event.kind == kind && event.gpio_num == gpio_num) {
            matches.push_back(event);
        }
    }
    return matches;
}

bool LatestStateForGpio(int gpio_num, LedcChannelState* state) {
    for (const auto& channel : MutableChannels()) {
        if (channel.configured && channel.gpio_num == gpio_num) {
            if (state != nullptr) {
                *state = channel;
            }
            return true;
        }
    }
    return false;
}

int ChannelForGpio(int gpio_num) {
    for (int channel = 0; channel < static_cast<int>(MutableChannels().size()); ++channel) {
        if (MutableChannels()[channel].configured &&
            MutableChannels()[channel].gpio_num == gpio_num) {
            return channel;
        }
    }
    return -1;
}

bool HasAnyEventForGpio(int gpio_num) {
    return std::any_of(MutableEvents().begin(), MutableEvents().end(),
                       [gpio_num](const LedcEvent& event) {
                           return event.gpio_num == gpio_num;
                       });
}

uint32_t LatestDutyForGpio(int gpio_num) {
    LedcChannelState state;
    return LatestStateForGpio(gpio_num, &state) ? state.duty : 0;
}

}  // namespace gosha::motion_pwm_host

extern "C" esp_err_t ledc_timer_config(const ledc_timer_config_t* config) {
    if (config == nullptr) {
        return ESP_FAIL;
    }
    gosha::motion_pwm_host::MutableEvents();
    gosha::motion_pwm_host::Events();
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kTimerConfig,
                                   -999, -1, 0, config->freq_hz);
    return ESP_OK;
}

extern "C" esp_err_t ledc_channel_config(const ledc_channel_config_t* config) {
    if (config == nullptr) {
        return ESP_FAIL;
    }
    const int channel = static_cast<int>(config->channel);
    auto& channels = gosha::motion_pwm_host::MutableChannels();
    if (channel < 0 || channel >= static_cast<int>(channels.size())) {
        return ESP_FAIL;
    }
    channels[channel].configured = true;
    channels[channel].gpio_num = config->gpio_num;
    channels[channel].duty = config->duty;
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kChannelConfig,
                                   config->gpio_num, channel, config->duty);
    return ESP_OK;
}

extern "C" esp_err_t ledc_set_duty(ledc_mode_t, ledc_channel_t channel, uint32_t duty) {
    const int channel_index = static_cast<int>(channel);
    auto& channels = gosha::motion_pwm_host::MutableChannels();
    if (channel_index < 0 || channel_index >= static_cast<int>(channels.size()) ||
        !channels[channel_index].configured) {
        return ESP_FAIL;
    }
    channels[channel_index].duty = duty;
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kSetDuty,
                                   channels[channel_index].gpio_num, channel_index, duty);
    return ESP_OK;
}

extern "C" esp_err_t ledc_update_duty(ledc_mode_t, ledc_channel_t channel) {
    const int channel_index = static_cast<int>(channel);
    auto& channels = gosha::motion_pwm_host::MutableChannels();
    if (channel_index < 0 || channel_index >= static_cast<int>(channels.size()) ||
        !channels[channel_index].configured) {
        return ESP_FAIL;
    }
    ++channels[channel_index].update_count;
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kUpdateDuty,
                                   channels[channel_index].gpio_num, channel_index,
                                   channels[channel_index].duty);
    return ESP_OK;
}

extern "C" esp_err_t ledc_stop(ledc_mode_t, ledc_channel_t channel, uint32_t idle_level) {
    const int channel_index = static_cast<int>(channel);
    auto& channels = gosha::motion_pwm_host::MutableChannels();
    if (channel_index < 0 || channel_index >= static_cast<int>(channels.size())) {
        return ESP_FAIL;
    }
    ++channels[channel_index].stop_count;
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kStop,
                                   channels[channel_index].gpio_num, channel_index,
                                   channels[channel_index].duty, idle_level);
    return ESP_OK;
}

extern "C" int64_t esp_timer_get_time(void) {
    return gosha::motion_pwm_host::NowUs();
}

extern "C" void vTaskDelay(TickType_t ticks) {
    gosha::motion_pwm_host::Record(gosha::motion_pwm_host::LedcEventKind::kDelay,
                                   -999, -1, 0, ticks);
    gosha::motion_pwm_host::AdvanceTimeMs(ticks);
}

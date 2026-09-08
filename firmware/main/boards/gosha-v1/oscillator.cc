//--------------------------------------------------------------
//-- Oscillator.pde
//-- Generate sinusoidal oscillations in the servos
//--------------------------------------------------------------
//-- (c) Juan Gonzalez-Gomez (Obijuan), Dec 2011
//-- (c) txp666 for esp32, 202503
//-- GPL license
//--------------------------------------------------------------
#include "oscillator.h"

#include <driver/ledc.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>

static const char* TAG = "Oscillator";

extern unsigned long IRAM_ATTR millis();

static ledc_channel_t next_free_channel = LEDC_CHANNEL_0;

Oscillator::Oscillator(int trim) {
    trim_ = trim;
    diff_limit_ = 0;
    is_attached_ = false;

    sampling_period_ = 30;
    period_ = 2000;
    number_samples_ = period_ / sampling_period_;
    inc_ = 2 * M_PI / number_samples_;

    amplitude_ = 45;
    phase_ = 0;
    phase0_ = 0;
    offset_ = 0;
    stop_ = false;
    rev_ = false;

    pos_ = 90;
    pin_ = -1;
    previous_millis_ = 0;
    previous_servo_command_millis_ = 0;
    ledc_channel_ = LEDC_CHANNEL_0;
    ledc_speed_mode_ = LEDC_LOW_SPEED_MODE;
    live_diagnostics_.available = true;
}

Oscillator::~Oscillator() {
    Detach();
}

uint32_t Oscillator::AngleToCompare(int angle) {
    return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) /
               (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) +
           SERVO_MIN_PULSEWIDTH_US;
}

bool Oscillator::NextSample() {
    current_millis_ = millis();

    if (current_millis_ - previous_millis_ > sampling_period_) {
        previous_millis_ = current_millis_;
        return true;
    }

    return false;
}

void Oscillator::Attach(int pin, bool rev) {
    if (is_attached_) {
        Detach();
    }

    pin_ = pin;
    rev_ = rev;
    live_diagnostics_.available = true;
    live_diagnostics_.attached = false;
    live_diagnostics_.pin = pin_;
    live_diagnostics_.channel = -1;
    live_diagnostics_.frequency_available = false;
    live_diagnostics_.duty_available = false;

    ledc_timer_config_t ledc_timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .duty_resolution = LEDC_TIMER_13_BIT,
                                      .timer_num = LEDC_TIMER_1,
                                      .freq_hz = 50,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    static int last_channel = 0;
    last_channel = (last_channel + 1) % 7 + 1;
    ledc_channel_ = (ledc_channel_t)last_channel;

    ledc_channel_config_t ledc_channel = {.gpio_num = pin_,
                                          .speed_mode = LEDC_LOW_SPEED_MODE,
                                          .channel = ledc_channel_,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LEDC_TIMER_1,
                                          .duty = 0,
                                          .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ledc_speed_mode_ = LEDC_LOW_SPEED_MODE;
    live_diagnostics_.channel = static_cast<int>(ledc_channel_);

    // pos_ = 90;
    // Write(pos_);
    previous_servo_command_millis_ = millis();

    is_attached_ = true;
    live_diagnostics_.attached = true;
}

void Oscillator::Detach() {
    if (!is_attached_)
        return;

    ESP_ERROR_CHECK(ledc_stop(ledc_speed_mode_, ledc_channel_, 0));

    is_attached_ = false;
    live_diagnostics_.attached = false;
    live_diagnostics_.frequency_available = false;
    live_diagnostics_.duty_available = false;
}

void Oscillator::SetT(unsigned int T) {
    period_ = T;

    number_samples_ = period_ / sampling_period_;
    inc_ = 2 * M_PI / number_samples_;
}

bool Oscillator::SetPosition(int position) {
    return Write(position);
}

void Oscillator::Refresh() {
    if (NextSample()) {
        if (!stop_) {
            int pos = std::round(amplitude_ * std::sin(phase_ + phase0_) + offset_);
            if (rev_)
                pos = -pos;
            Write(pos + 90);
        }

        phase_ = phase_ + inc_;
    }
}

Oscillator::LiveDiagnostics Oscillator::GetLiveDiagnostics() const {
    LiveDiagnostics diagnostics = live_diagnostics_;
    diagnostics.available = true;
    diagnostics.attached = is_attached_;
    diagnostics.pin = pin_;
    diagnostics.channel = is_attached_ ? static_cast<int>(ledc_channel_) : -1;
    diagnostics.frequency_available = false;
    diagnostics.duty_available = false;
    if (is_attached_) {
        diagnostics.frequency_hz = ledc_get_freq(ledc_speed_mode_, LEDC_TIMER_1);
        diagnostics.frequency_available = diagnostics.frequency_hz != 0;
        const uint32_t duty = ledc_get_duty(ledc_speed_mode_, ledc_channel_);
        if (duty != LEDC_ERR_DUTY) {
            diagnostics.duty = duty;
            diagnostics.duty_available = true;
        }
    }
    return diagnostics;
}

bool Oscillator::Write(int position) {
    live_diagnostics_.available = true;
    live_diagnostics_.pin = pin_;
    live_diagnostics_.attached = is_attached_;
    live_diagnostics_.channel = is_attached_ ? static_cast<int>(ledc_channel_) : -1;
    live_diagnostics_.last_write_available = true;
    live_diagnostics_.requested_angle_degrees = position;
    live_diagnostics_.last_write_ms = millis();
    live_diagnostics_.last_write_ok = false;
    live_diagnostics_.skipped_unattached = !is_attached_;
    live_diagnostics_.frequency_available = false;
    live_diagnostics_.duty_available = false;
    if (!is_attached_) {
        return false;
    }

    long currentMillis = millis();
    if (diff_limit_ > 0) {
        int limit = std::max(
            1, (((int)(currentMillis - previous_servo_command_millis_)) * diff_limit_) / 1000);
        if (abs(position - pos_) > limit) {
            pos_ += position < pos_ ? -limit : limit;
        } else {
            pos_ = position;
        }
    } else {
        pos_ = position;
    }
    previous_servo_command_millis_ = currentMillis;

    int angle = pos_ + trim_;

    angle = std::min(std::max(angle, 0), 180);

    uint32_t duty = (uint32_t)(((angle / 180.0) * 2.0 + 0.5) * 8191 / 20.0);
    live_diagnostics_.software_angle_degrees = pos_;
    live_diagnostics_.applied_angle_degrees = angle;
    live_diagnostics_.applied_duty = duty;
    live_diagnostics_.skipped_unattached = false;

    esp_err_t ret = ledc_set_duty(ledc_speed_mode_, ledc_channel_, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set duty failed for GPIO%d channel%d: %d",
                 pin_, static_cast<int>(ledc_channel_), ret);
        return false;
    }
    ret = ledc_update_duty(ledc_speed_mode_, ledc_channel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC update duty failed for GPIO%d channel%d: %d",
                 pin_, static_cast<int>(ledc_channel_), ret);
        return false;
    }
    live_diagnostics_.last_write_ok = true;
    return true;
}

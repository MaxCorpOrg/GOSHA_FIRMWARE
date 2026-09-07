#ifndef GOSHA_HOST_STUBS_MOTION_PWM_ESP_TIMER_H_
#define GOSHA_HOST_STUBS_MOTION_PWM_ESP_TIMER_H_

#include <cstdint>

#include "esp_err.h"

extern "C" int64_t esp_timer_get_time(void);

#endif  // GOSHA_HOST_STUBS_MOTION_PWM_ESP_TIMER_H_

#ifndef GOSHA_HOST_STUBS_MOTION_PWM_FREERTOS_FREERTOS_H_
#define GOSHA_HOST_STUBS_MOTION_PWM_FREERTOS_FREERTOS_H_

#include <cstdint>

using TickType_t = uint32_t;

#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)

#endif  // GOSHA_HOST_STUBS_MOTION_PWM_FREERTOS_FREERTOS_H_

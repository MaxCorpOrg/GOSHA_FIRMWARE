#ifndef GOSHA_HOST_STUBS_MOTION_PWM_ESP_LOG_H_
#define GOSHA_HOST_STUBS_MOTION_PWM_ESP_LOG_H_

#include "esp_err.h"

#define ESP_LOGI(tag, fmt, ...) \
    do {                        \
        (void)(tag);            \
    } while (false)

#define ESP_LOGW(tag, fmt, ...) \
    do {                        \
        (void)(tag);            \
    } while (false)

#define ESP_LOGE(tag, fmt, ...) \
    do {                        \
        (void)(tag);            \
    } while (false)

#define ESP_LOGD(tag, fmt, ...) \
    do {                        \
        (void)(tag);            \
    } while (false)

#endif  // GOSHA_HOST_STUBS_MOTION_PWM_ESP_LOG_H_

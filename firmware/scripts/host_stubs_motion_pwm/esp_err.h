#ifndef GOSHA_HOST_STUBS_MOTION_PWM_ESP_ERR_H_
#define GOSHA_HOST_STUBS_MOTION_PWM_ESP_ERR_H_

#include <cstdlib>
#include <iostream>

using esp_err_t = int;

constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#define ESP_ERROR_CHECK(expr)                                                        \
    do {                                                                             \
        const esp_err_t gosha_host_stub_err = (expr);                                \
        if (gosha_host_stub_err != ESP_OK) {                                         \
            std::cerr << "ESP_ERROR_CHECK failed: " #expr " -> "                    \
                      << gosha_host_stub_err << "\n";                               \
            std::abort();                                                            \
        }                                                                            \
    } while (false)

#endif  // GOSHA_HOST_STUBS_MOTION_PWM_ESP_ERR_H_

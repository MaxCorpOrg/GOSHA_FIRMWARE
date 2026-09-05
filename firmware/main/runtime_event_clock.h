#ifndef RUNTIME_EVENT_CLOCK_H
#define RUNTIME_EVENT_CLOCK_H

#include <cstdio>
#include <ctime>
#include <string>
#include <sys/time.h>

// Keep the device timestamp's millisecond precision on the wire. An unset
// wall clock cannot provide an event-time measurement.
inline std::string FormatRuntimeEventTime(const timeval& now) {
    if (now.tv_sec <= 1577836800 || now.tv_usec < 0 || now.tv_usec >= 1000000) {
        return {};
    }
    struct tm utc = {};
    if (gmtime_r(&now.tv_sec, &utc) == nullptr) {
        return {};
    }
    char seconds[32];
    if (strftime(seconds, sizeof(seconds), "%Y-%m-%dT%H:%M:%S", &utc) == 0) {
        return {};
    }
    char timestamp[40];
    const int length = snprintf(timestamp, sizeof(timestamp), "%s.%03ldZ", seconds,
                                static_cast<long>(now.tv_usec / 1000));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(timestamp)) {
        return {};
    }
    return timestamp;
}

#endif  // RUNTIME_EVENT_CLOCK_H

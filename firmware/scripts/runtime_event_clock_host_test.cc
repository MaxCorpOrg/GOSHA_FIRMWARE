#include "runtime_event_clock.h"

#include <cstdlib>
#include <iostream>

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(1);
    }
}

int main() {
    Require(FormatRuntimeEventTime({0, 0}).empty(), "unset clock must be unavailable");
    Require(FormatRuntimeEventTime({1788591600, -1}).empty(), "invalid microseconds");
    Require(FormatRuntimeEventTime({1788591600, 1000000}).empty(), "invalid microseconds");
    Require(FormatRuntimeEventTime({1788591600, 0}) == "2026-09-05T07:00:00.000Z",
            "zero milliseconds must be explicit");
    Require(FormatRuntimeEventTime({1788591600, 950999}) == "2026-09-05T07:00:00.950Z",
            "speech end must retain milliseconds without rounding into next second");
    Require(FormatRuntimeEventTime({1788591602, 850001}) == "2026-09-05T07:00:02.850Z",
            "audio output must retain milliseconds");
    Require(FormatRuntimeEventTime({1788591599, 999999}) == "2026-09-05T06:59:59.999Z",
            "second rollover must not corrupt the date");
    std::cout << "runtime event clock host test passed" << std::endl;
}

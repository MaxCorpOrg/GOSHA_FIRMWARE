#include <cstdint>
#include <iostream>
#include <string>

#include "../main/boards/gosha-v1/motion_live_usb_framing.h"

using gosha::motion_live::MotionLiveUsbFrameStatus;
using gosha::motion_live::MotionLiveUsbLineFramer;
using gosha::motion_live::kMotionLiveUsbFramePrefix;
using gosha::motion_live::kMotionLiveUsbFramePrefixLength;
using gosha::motion_live::kMotionLiveUsbMaxJsonBytes;
using gosha::motion_live::kMotionLiveUsbMaxResponseFrameBytes;
using gosha::motion_live::kMotionLiveUsbMaxResponseJsonBytes;
using gosha::motion_live::kMotionLiveUsbPartialTimeoutMs;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition "\n";   \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

MotionLiveUsbFrameStatus FeedString(MotionLiveUsbLineFramer* framer,
                                    const std::string& input,
                                    uint64_t now_ms,
                                    std::string* json) {
    MotionLiveUsbFrameStatus last = MotionLiveUsbFrameStatus::kNone;
    for (char ch : input) {
        last = framer->Feed(static_cast<uint8_t>(ch), now_ms, json);
    }
    return last;
}

}  // namespace

int main() {
    CHECK(kMotionLiveUsbFramePrefixLength == 12);
    CHECK(std::string(kMotionLiveUsbFramePrefix) == "@GOSHA-LIVE:");
    CHECK(kMotionLiveUsbMaxJsonBytes == 4096);
    CHECK(kMotionLiveUsbMaxResponseJsonBytes == 16384);
    CHECK(kMotionLiveUsbMaxResponseFrameBytes ==
          kMotionLiveUsbFramePrefixLength + kMotionLiveUsbMaxResponseJsonBytes + 1);

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        const std::string body = R"({"protocol":"gosha.motion.live.v1","op":"hello"})";
        const auto status = FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + body + "\n",
                                       100, &json);
        CHECK(status == MotionLiveUsbFrameStatus::kReady);
        CHECK(json == body);
        CHECK(!framer.HasPartialLine());
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        const std::string body = R"({"protocol":"gosha.motion.live.v1","op":"hello"})";
        const auto status = FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + body + "\r\n",
                                       100, &json);
        CHECK(status == MotionLiveUsbFrameStatus::kReady);
        CHECK(json == body);
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        CHECK(framer.Feed('E', 100, &json) == MotionLiveUsbFrameStatus::kDiscarded);
        CHECK(FeedString(&framer, "SP-ROM boot log\n", 100, &json) ==
              MotionLiveUsbFrameStatus::kDiscarded);
        CHECK(json.empty());
        CHECK(!framer.HasPartialLine());
        const std::string body = R"({"protocol":"gosha.motion.live.v1","op":"hello"})";
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + body + "\n",
                         110, &json) == MotionLiveUsbFrameStatus::kReady);
        CHECK(json == body);
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        const std::string exact_max(kMotionLiveUsbMaxJsonBytes, 'x');
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + exact_max + "\n",
                         100, &json) == MotionLiveUsbFrameStatus::kReady);
        CHECK(json.size() == kMotionLiveUsbMaxJsonBytes);
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) +
                                      std::string(kMotionLiveUsbMaxJsonBytes, 'x'),
                         100, &json) == MotionLiveUsbFrameStatus::kNone);
        CHECK(framer.Feed('x', 100, &json) == MotionLiveUsbFrameStatus::kOverflow);
        CHECK(FeedString(&framer, "still discarded\n", 100, &json) ==
              MotionLiveUsbFrameStatus::kDiscarded);
        CHECK(!framer.HasPartialLine());
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        CHECK(FeedString(&framer, "@GOSHA-", 100, &json) == MotionLiveUsbFrameStatus::kNone);
        CHECK(framer.PollTimeout(100 + kMotionLiveUsbPartialTimeoutMs) ==
              MotionLiveUsbFrameStatus::kNone);
        CHECK(framer.PollTimeout(101 + kMotionLiveUsbPartialTimeoutMs) ==
              MotionLiveUsbFrameStatus::kTimeout);
        CHECK(!framer.HasPartialLine());
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + "{\"protocol\"",
                         100, &json) == MotionLiveUsbFrameStatus::kNone);
        framer.ResetPartialLine();
        CHECK(!framer.HasPartialLine());
        CHECK(FeedString(&framer, R"(: "gosha.motion.live.v1","op":"arm"})" "\n",
                         120, &json) != MotionLiveUsbFrameStatus::kReady);
        const std::string body = R"({"protocol":"gosha.motion.live.v1","op":"hello"})";
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + body + "\n",
                         130, &json) == MotionLiveUsbFrameStatus::kReady);
        CHECK(json == body);
    }

    {
        MotionLiveUsbLineFramer framer;
        std::string json;
        CHECK(FeedString(&framer, std::string(kMotionLiveUsbFramePrefix) + "{",
                         100, &json) == MotionLiveUsbFrameStatus::kNone);
        CHECK(framer.Feed('\0', 100, &json) == MotionLiveUsbFrameStatus::kDiscarded);
        CHECK(FeedString(&framer, "}\n", 100, &json) ==
              MotionLiveUsbFrameStatus::kDiscarded);
    }

    std::cout << "motion_live_usb_framing_host_test: PASS\n";
    return 0;
}

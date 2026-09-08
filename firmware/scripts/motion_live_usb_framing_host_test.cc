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

std::string WorstCaseDiagnosticResponseJson() {
    std::string json =
        R"({"protocol":"gosha.motion.live.v1","op":"ack","session_id":")" +
        std::string(128, 's') +
        R"(","seq":4294967295,"should_apply":true,)"
        R"("commanded_pose":{"arm_negative_x":-70,"arm_positive_x":70,)"
        R"("leg_negative_x":-35,"leg_positive_x":35,)"
        R"("foot_negative_x":-30,"foot_positive_x":30},)"
        R"("servo_degrees":{"left_leg":180,"right_leg":180,)"
        R"("left_foot":180,"right_foot":180,"left_hand":180,"right_hand":180},)"
        R"("pwm_diagnostics":{"servos":[)";
    const char* servo_keys[] = {
        "left_leg", "right_leg", "left_foot", "right_foot", "left_hand", "right_hand",
    };
    const char* joint_ids[] = {
        "leg_negative_x", "leg_positive_x", "foot_negative_x",
        "foot_positive_x", "arm_negative_x", "arm_positive_x",
    };
    for (int i = 0; i < 6; ++i) {
        if (i > 0) {
            json.push_back(',');
        }
        json += R"({"id":")";
        json += servo_keys[i];
        json += R"(","servo_key":")";
        json += servo_keys[i];
        json += R"(","joint_id":")";
        json += joint_ids[i];
        json +=
            R"(","available":true,"attached":true,"pin":48,"channel":7,)"
            R"("frequency_available":true,"freq_hz":50,)"
            R"("duty_available":true,"duty":4294967295,)"
            R"("last_write_available":true,"requested_angle":180,)"
            R"("software_angle":180,"applied_angle":180,)"
            R"("applied_duty":4294967295,"last_write_ms":18446744073709551615,)"
            R"("last_write_ok":true,"skipped_unattached":false})";
    }
    json += R"(]},"measured_pose":null,"tilt":null})";
    return json;
}

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
    const std::string diagnostic_json = WorstCaseDiagnosticResponseJson();
    CHECK(diagnostic_json.size() < kMotionLiveUsbMaxResponseJsonBytes);
    CHECK(kMotionLiveUsbFramePrefixLength + diagnostic_json.size() + 1 <
          kMotionLiveUsbMaxResponseFrameBytes);

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

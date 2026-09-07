#ifndef GOSHA_V1_MOTION_LIVE_USB_FRAMING_H_
#define GOSHA_V1_MOTION_LIVE_USB_FRAMING_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace gosha::motion_live {

constexpr const char* kMotionLiveUsbFramePrefix = "@GOSHA-LIVE:";
constexpr size_t kMotionLiveUsbFramePrefixLength = 12;
constexpr size_t kMotionLiveUsbMaxJsonBytes = 4096;
constexpr size_t kMotionLiveUsbMaxResponseJsonBytes = 16384;
constexpr size_t kMotionLiveUsbMaxResponseFrameBytes =
    kMotionLiveUsbFramePrefixLength + kMotionLiveUsbMaxResponseJsonBytes + 1;
constexpr size_t kMotionLiveUsbMaxLineBytes =
    kMotionLiveUsbFramePrefixLength + kMotionLiveUsbMaxJsonBytes;
constexpr uint64_t kMotionLiveUsbPartialTimeoutMs = 500;

enum class MotionLiveUsbFrameStatus {
    kNone,
    kReady,
    kDiscarded,
    kOverflow,
    kTimeout,
};

class MotionLiveUsbLineFramer {
public:
    MotionLiveUsbLineFramer() {
        buffer_.reserve(kMotionLiveUsbMaxLineBytes);
    }

    MotionLiveUsbFrameStatus Feed(uint8_t byte, uint64_t now_ms, std::string* json) {
        if (json != nullptr) {
            json->clear();
        }

        if (byte == '\r') {
            return MotionLiveUsbFrameStatus::kNone;
        }
        if (byte == '\0') {
            if (!line_started_) {
                line_started_ = true;
                partial_started_ms_ = now_ms;
            }
            discarding_ = true;
            return MotionLiveUsbFrameStatus::kDiscarded;
        }

        if (!line_started_) {
            if (byte == '\n') {
                return MotionLiveUsbFrameStatus::kDiscarded;
            }
            line_started_ = true;
            partial_started_ms_ = now_ms;
        }

        if (byte == '\n') {
            return FinishLine(json);
        }

        if (discarding_) {
            return MotionLiveUsbFrameStatus::kNone;
        }

        if (buffer_.size() >= kMotionLiveUsbMaxLineBytes) {
            discarding_ = true;
            return MotionLiveUsbFrameStatus::kOverflow;
        }

        buffer_.push_back(static_cast<char>(byte));
        if (!PrefixCanStillMatch()) {
            discarding_ = true;
            return MotionLiveUsbFrameStatus::kDiscarded;
        }
        return MotionLiveUsbFrameStatus::kNone;
    }

    MotionLiveUsbFrameStatus PollTimeout(uint64_t now_ms) {
        if (!line_started_) {
            return MotionLiveUsbFrameStatus::kNone;
        }
        if (now_ms <= partial_started_ms_ ||
            now_ms - partial_started_ms_ <= kMotionLiveUsbPartialTimeoutMs) {
            return MotionLiveUsbFrameStatus::kNone;
        }
        Reset();
        return MotionLiveUsbFrameStatus::kTimeout;
    }

    bool HasPartialLine() const {
        return line_started_;
    }

    void ResetPartialLine() {
        Reset();
    }

private:
    MotionLiveUsbFrameStatus FinishLine(std::string* json) {
        if (discarding_) {
            Reset();
            return MotionLiveUsbFrameStatus::kDiscarded;
        }
        if (buffer_.size() <= kMotionLiveUsbFramePrefixLength ||
            buffer_.compare(0, kMotionLiveUsbFramePrefixLength,
                            kMotionLiveUsbFramePrefix) != 0) {
            Reset();
            return MotionLiveUsbFrameStatus::kDiscarded;
        }
        const size_t json_len = buffer_.size() - kMotionLiveUsbFramePrefixLength;
        if (json_len > kMotionLiveUsbMaxJsonBytes) {
            Reset();
            return MotionLiveUsbFrameStatus::kOverflow;
        }
        if (json != nullptr) {
            *json = buffer_.substr(kMotionLiveUsbFramePrefixLength);
        }
        Reset();
        return MotionLiveUsbFrameStatus::kReady;
    }

    bool PrefixCanStillMatch() const {
        const size_t compare_len =
            buffer_.size() < kMotionLiveUsbFramePrefixLength
                ? buffer_.size()
                : kMotionLiveUsbFramePrefixLength;
        return buffer_.compare(0, compare_len, kMotionLiveUsbFramePrefix,
                               compare_len) == 0;
    }

    void Reset() {
        buffer_.clear();
        line_started_ = false;
        discarding_ = false;
        partial_started_ms_ = 0;
    }

    std::string buffer_;
    bool line_started_ = false;
    bool discarding_ = false;
    uint64_t partial_started_ms_ = 0;
};

}  // namespace gosha::motion_live

#endif  // GOSHA_V1_MOTION_LIVE_USB_FRAMING_H_

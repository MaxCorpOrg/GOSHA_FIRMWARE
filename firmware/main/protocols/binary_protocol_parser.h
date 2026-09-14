#ifndef BINARY_PROTOCOL_PARSER_H_
#define BINARY_PROTOCOL_PARSER_H_

#include "protocol.h"

#include <arpa/inet.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace protocol_binary {

enum class DecodeAudioResult {
    kOk,
    kTruncatedHeader,
    kTruncatedPayload,
    kUnsupportedVersion,
    kUnsupportedType,
};

inline DecodeAudioResult DecodeAudioPacket(int negotiated_version,
                                           const char* data,
                                           size_t len,
                                           int sample_rate,
                                           int frame_duration,
                                           AudioStreamPacket* out) {
    if (data == nullptr || out == nullptr) {
        return DecodeAudioResult::kTruncatedHeader;
    }

    if (negotiated_version == 2) {
        constexpr size_t kHeaderSize = sizeof(BinaryProtocol2);
        if (len < kHeaderSize) {
            return DecodeAudioResult::kTruncatedHeader;
        }

        BinaryProtocol2 header = {};
        std::memcpy(&header, data, kHeaderSize);
        const uint16_t packet_version = ntohs(header.version);
        const uint16_t packet_type = ntohs(header.type);
        const uint32_t timestamp = ntohl(header.timestamp);
        const uint32_t payload_size = ntohl(header.payload_size);

        if (packet_version != 2) {
            return DecodeAudioResult::kUnsupportedVersion;
        }
        if (packet_type != 0) {
            return DecodeAudioResult::kUnsupportedType;
        }
        if (payload_size > len - kHeaderSize) {
            return DecodeAudioResult::kTruncatedPayload;
        }

        const auto* payload = reinterpret_cast<const uint8_t*>(data + kHeaderSize);
        out->sample_rate = sample_rate;
        out->frame_duration = frame_duration;
        out->timestamp = timestamp;
        out->payload.assign(payload, payload + payload_size);
        return DecodeAudioResult::kOk;
    }

    if (negotiated_version == 3) {
        constexpr size_t kHeaderSize = sizeof(BinaryProtocol3);
        if (len < kHeaderSize) {
            return DecodeAudioResult::kTruncatedHeader;
        }

        BinaryProtocol3 header = {};
        std::memcpy(&header, data, kHeaderSize);
        const uint8_t packet_type = header.type;
        const uint16_t payload_size = ntohs(header.payload_size);

        if (packet_type != 0) {
            return DecodeAudioResult::kUnsupportedType;
        }
        if (payload_size > len - kHeaderSize) {
            return DecodeAudioResult::kTruncatedPayload;
        }

        const auto* payload = reinterpret_cast<const uint8_t*>(data + kHeaderSize);
        out->sample_rate = sample_rate;
        out->frame_duration = frame_duration;
        out->timestamp = 0;
        out->payload.assign(payload, payload + payload_size);
        return DecodeAudioResult::kOk;
    }

    out->sample_rate = sample_rate;
    out->frame_duration = frame_duration;
    out->timestamp = 0;
    const auto* payload = reinterpret_cast<const uint8_t*>(data);
    out->payload.assign(payload, payload + len);
    return DecodeAudioResult::kOk;
}

}  // namespace protocol_binary

#endif  // BINARY_PROTOCOL_PARSER_H_

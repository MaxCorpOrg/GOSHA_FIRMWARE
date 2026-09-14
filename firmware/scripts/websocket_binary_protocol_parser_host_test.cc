#include "protocols/binary_protocol_parser.h"

#include <arpa/inet.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

std::vector<uint8_t> MakeV2(uint16_t type, uint32_t timestamp,
                            uint32_t payload_size_field,
                            const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(sizeof(BinaryProtocol2) + payload.size());
    auto* header = reinterpret_cast<BinaryProtocol2*>(frame.data());
    header->version = htons(2);
    header->type = htons(type);
    header->reserved = 0;
    header->timestamp = htonl(timestamp);
    header->payload_size = htonl(payload_size_field);
    std::memcpy(header->payload, payload.data(), payload.size());
    return frame;
}

std::vector<uint8_t> MakeV3(uint8_t type, uint16_t payload_size_field,
                            const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame(sizeof(BinaryProtocol3) + payload.size());
    auto* header = reinterpret_cast<BinaryProtocol3*>(frame.data());
    header->type = type;
    header->reserved = 0;
    header->payload_size = htons(payload_size_field);
    std::memcpy(header->payload, payload.data(), payload.size());
    return frame;
}

void ExpectPayload(const AudioStreamPacket& packet,
                   const std::vector<uint8_t>& expected) {
    assert(packet.payload == expected);
}

}  // namespace

int main() {
    AudioStreamPacket packet;

    const std::vector<uint8_t> v2_payload{0x01, 0x02, 0x03, 0x04};
    const auto v2 = MakeV2(0, 123456789u, v2_payload.size(), v2_payload);
    const auto v2_before = v2;
    auto result = protocol_binary::DecodeAudioPacket(
        2, reinterpret_cast<const char*>(v2.data()), v2.size(), 16000, 60, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kOk);
    assert(packet.sample_rate == 16000);
    assert(packet.frame_duration == 60);
    assert(packet.timestamp == 123456789u);
    ExpectPayload(packet, v2_payload);
    assert(v2 == v2_before);

    result = protocol_binary::DecodeAudioPacket(
        2, reinterpret_cast<const char*>(v2.data()), sizeof(BinaryProtocol2) - 1,
        16000, 60, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kTruncatedHeader);

    const auto v2_oversize = MakeV2(0, 7u, 99u, v2_payload);
    result = protocol_binary::DecodeAudioPacket(
        2, reinterpret_cast<const char*>(v2_oversize.data()), v2_oversize.size(),
        16000, 60, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kTruncatedPayload);

    const auto v2_json_type = MakeV2(1, 7u, v2_payload.size(), v2_payload);
    result = protocol_binary::DecodeAudioPacket(
        2, reinterpret_cast<const char*>(v2_json_type.data()), v2_json_type.size(),
        16000, 60, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kUnsupportedType);

    const std::vector<uint8_t> v3_payload{0x0a, 0x0b, 0x0c};
    const auto v3 = MakeV3(0, v3_payload.size(), v3_payload);
    const auto v3_before = v3;
    result = protocol_binary::DecodeAudioPacket(
        3, reinterpret_cast<const char*>(v3.data()), v3.size(), 24000, 40, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kOk);
    assert(packet.sample_rate == 24000);
    assert(packet.frame_duration == 40);
    assert(packet.timestamp == 0u);
    ExpectPayload(packet, v3_payload);
    assert(v3 == v3_before);

    result = protocol_binary::DecodeAudioPacket(
        3, reinterpret_cast<const char*>(v3.data()), sizeof(BinaryProtocol3) - 1,
        24000, 40, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kTruncatedHeader);

    const auto v3_oversize = MakeV3(0, 50u, v3_payload);
    result = protocol_binary::DecodeAudioPacket(
        3, reinterpret_cast<const char*>(v3_oversize.data()), v3_oversize.size(),
        24000, 40, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kTruncatedPayload);

    const std::vector<uint8_t> raw_payload{0x21, 0x22};
    result = protocol_binary::DecodeAudioPacket(
        1, reinterpret_cast<const char*>(raw_payload.data()), raw_payload.size(),
        16000, 20, &packet);
    assert(result == protocol_binary::DecodeAudioResult::kOk);
    assert(packet.timestamp == 0u);
    ExpectPayload(packet, raw_payload);

    std::cout << "websocket_binary_protocol_parser_host_test: PASS\n";
    return 0;
}

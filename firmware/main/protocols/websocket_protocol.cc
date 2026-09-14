#include "websocket_protocol.h"
#include "binary_protocol_parser.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"
#include "diagnostic_redaction.h"
#include "sdkconfig.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send websocket text frame, bytes=%u",
                 static_cast<unsigned>(text.size()));
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel(bool send_goodbye) {
    (void)send_goodbye;  // Websocket doesn't need to send goodbye message
    const bool had_channel = websocket_ != nullptr;
    if (had_channel) {
        connection_generation_.fetch_add(1, std::memory_order_acq_rel);
    }
    websocket_.reset();
    server_aec_negotiated_.store(false, std::memory_order_release);
    if (had_channel && on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool WebsocketProtocol::OpenAudioChannel() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
#if defined(CONFIG_GOSHA_VOICE_SERVER_AEC_NEGOTIATION)
    // Server-side echo cancellation for full duplex requires BinaryProtocol2 timestamps.
    version_ = 2;
#else
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }
#endif

    error_occurred_ = false;
    server_aec_negotiated_.store(false, std::memory_order_release);

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }
    const uint32_t connection_generation =
        connection_generation_.fetch_add(1, std::memory_order_acq_rel) + 1;

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this, connection_generation](const char* data, size_t len, bool binary) {
        if (connection_generation_.load(std::memory_order_acquire) != connection_generation) {
            return;
        }
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                AudioStreamPacket packet;
                const auto decode_result = protocol_binary::DecodeAudioPacket(
                    version_, data, len, server_sample_rate_, server_frame_duration_, &packet);
                if (decode_result == protocol_binary::DecodeAudioResult::kOk) {
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(std::move(packet)));
                } else {
                    ESP_LOGW(TAG, "Dropped invalid websocket binary audio frame, version=%d, reason=%d, bytes=%u",
                             version_, static_cast<int>(decode_result), static_cast<unsigned>(len));
                }
            }
        } else {
            // Parse JSON data. The WebSocket callback provides an explicit length,
            // so do not assume NUL termination and do not echo payload text in logs.
            auto root = cJSON_ParseWithLengthOpts(data, len, nullptr, false);
            if (root == nullptr) {
                ESP_LOGE(TAG, "Invalid websocket JSON frame, bytes=%u", static_cast<unsigned>(len));
                return;
            }
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Websocket JSON frame missing string type, bytes=%u",
                         static_cast<unsigned>(len));
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this, connection_generation]() {
        uint32_t expected = connection_generation;
        if (!connection_generation_.compare_exchange_strong(
                expected, connection_generation + 1, std::memory_order_acq_rel)) {
            return;
        }
        ESP_LOGI(TAG, "Websocket disconnected");
        server_aec_negotiated_.store(false, std::memory_order_release);
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    const auto diagnostic_url = diagnostic_redaction::RedactUrlForDiagnostics(url);
    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", diagnostic_url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server, code=%d", websocket_->GetLastError());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if defined(CONFIG_GOSHA_VOICE_SERVER_AEC_NEGOTIATION)
    cJSON_AddBoolToObject(features, "live_duplex", true);
    cJSON_AddStringToObject(features, "aec", "server");
#elif CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
#if defined(CONFIG_GOSHA_VOICE_MOTION_LIVE)
    cJSON_AddBoolToObject(features, "motion_live", true);
#endif
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    AddAudioParams(root, OPUS_FRAME_DURATION_MS);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    server_aec_negotiated_.store(false, std::memory_order_release);
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (!cJSON_IsString(transport) || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported websocket server hello transport");
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

#if defined(CONFIG_GOSHA_VOICE_SERVER_AEC_NEGOTIATION)
    auto features = cJSON_GetObjectItem(root, "features");
    auto live_duplex = cJSON_IsObject(features) ? cJSON_GetObjectItem(features, "live_duplex") : nullptr;
    auto aec = cJSON_IsObject(features) ? cJSON_GetObjectItem(features, "aec") : nullptr;
    const bool server_aec_ack =
        cJSON_IsTrue(live_duplex) && cJSON_IsString(aec) &&
        strcmp(aec->valuestring, "server") == 0 && version_ == 2;
    server_aec_negotiated_.store(server_aec_ack, std::memory_order_release);
    ESP_LOGI(TAG, "Live duplex server AEC negotiation: %s",
             server_aec_negotiated_.load(std::memory_order_acquire) ? "enabled" : "disabled");
#endif

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}

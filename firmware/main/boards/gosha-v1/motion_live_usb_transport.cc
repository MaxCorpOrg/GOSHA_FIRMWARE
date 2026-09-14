#include "motion_live_usb_transport.h"

#include "sdkconfig.h"

#if defined(CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN)

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED) || \
    defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG) ||         \
    defined(CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG)
#error "CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN requires UART console and CONFIG_ESP_CONSOLE_SECONDARY_NONE; USB Serial/JTAG console output would mix with @GOSHA-LIVE frames"
#endif

#if !defined(CONFIG_ESP_CONSOLE_UART)
#error "CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN requires a UART console for firmware logs"
#endif

#include <cJSON.h>
#include <driver/usb_serial_jtag.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "motion_live_adapter.h"
#include "motion_live_core.h"
#include "motion_live_usb_framing.h"

extern "C" void usb_serial_jtag_connection_monitor_include(void);

#endif

namespace gosha::motion_live {

#if defined(CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN)
namespace {

constexpr const char* TAG = "MotionLiveUSB";
constexpr size_t kUsbDriverRxBufferBytes = kMotionLiveUsbMaxLineBytes + 1;
constexpr size_t kUsbDriverTxBufferBytes = kMotionLiveUsbMaxResponseFrameBytes;
constexpr size_t kUsbReadChunkBytes = 128;
constexpr TickType_t kUsbReadWaitTicks = pdMS_TO_TICKS(20);
constexpr TickType_t kUsbWriteWaitTicks = pdMS_TO_TICKS(20);
constexpr uint64_t kUsbWriteBudgetMs = 2000;
constexpr uint32_t kUsbTaskStackBytes = 6144;
constexpr UBaseType_t kUsbTaskPriority = 5;

static_assert(kMotionLiveUsbMaxJsonBytes == 4096,
              "USB Live command JSON contract must stay bounded at 4096 bytes");
static_assert(kMotionLiveUsbMaxResponseJsonBytes == 16384,
              "USB Live response JSON contract must stay bounded at 16384 bytes");

TaskHandle_t g_usb_task_handle = nullptr;

uint64_t NowMs() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
}

void DisarmUsbOwner() {
    MotionLiveAdapter::GetInstance().OnTransportClosed(kMotionLiveUsbOwnerId);
}

bool DrainUsbRx() {
    uint8_t discard[kUsbReadChunkBytes] = {};
    size_t drained = 0;
    while (drained < kUsbDriverRxBufferBytes) {
        const int read = usb_serial_jtag_read_bytes(discard, sizeof(discard), 0);
        if (read <= 0) {
            return true;
        }
        drained += static_cast<size_t>(read);
    }
    return false;
}

esp_err_t WriteUsbFrame(cJSON* reply) {
    char* text = cJSON_PrintUnformatted(reply);
    if (text == nullptr) {
        ESP_LOGE(TAG, "Live USB response serialization failed");
        return ESP_ERR_NO_MEM;
    }

    const size_t json_len = std::strlen(text);
    if (json_len > kMotionLiveUsbMaxResponseJsonBytes) {
        cJSON_free(text);
        ESP_LOGE(TAG, "Live USB response exceeded bounded JSON size");
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t frame_len = kMotionLiveUsbFramePrefixLength + json_len + 1;
    if (frame_len > kMotionLiveUsbMaxResponseFrameBytes) {
        cJSON_free(text);
        ESP_LOGE(TAG, "Live USB response exceeded bounded frame size");
        return ESP_ERR_INVALID_SIZE;
    }

    std::string frame;
    frame.reserve(frame_len);
    frame.append(kMotionLiveUsbFramePrefix, kMotionLiveUsbFramePrefixLength);
    frame.append(text, json_len);
    frame.push_back('\n');
    cJSON_free(text);

    const uint64_t deadline_ms = NowMs() + kUsbWriteBudgetMs;
    size_t offset = 0;
    while (offset < frame.size()) {
        if (!usb_serial_jtag_is_connected()) {
            return ESP_FAIL;
        }
        const uint64_t now_ms = NowMs();
        if (now_ms > deadline_ms) {
            return ESP_ERR_TIMEOUT;
        }
        const size_t remaining = frame.size() - offset;
        const int written = usb_serial_jtag_write_bytes(frame.data() + offset,
                                                        remaining,
                                                        kUsbWriteWaitTicks);
        if (written <= 0) {
            return ESP_FAIL;
        }
        offset += static_cast<size_t>(written);
    }
    return ESP_OK;
}

void HandleUsbJsonLine(const std::string& json) {
    if (!usb_serial_jtag_is_connected()) {
        DisarmUsbOwner();
        ESP_LOGW(TAG, "Live USB dispatch skipped while disconnected; session disarmed");
        return;
    }

    const char* parse_end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json.c_str(), json.size() + 1,
                                            &parse_end, true);
    if (!cJSON_IsObject(root) || parse_end != json.c_str() + json.size()) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Live USB malformed or trailing JSON frame discarded");
        return;
    }

    bool write_failed = false;
    MotionLiveJsonSender sender = [&write_failed](cJSON* reply) -> esp_err_t {
        const esp_err_t ret = WriteUsbFrame(reply);
        if (ret != ESP_OK) {
            write_failed = true;
        }
        return ret;
    };

    const bool handled = MotionLiveAdapter::GetInstance().HandleTransportMessage(
        kMotionLiveUsbOwnerId, root, sender);
    cJSON_Delete(root);

    if (write_failed) {
        DisarmUsbOwner();
        ESP_LOGW(TAG, "Live USB write failed; session disarmed");
        return;
    }
    if (!handled) {
        ESP_LOGD(TAG, "Live USB non-Live frame discarded");
    }
}

void HandleFramerStatus(MotionLiveUsbFrameStatus status) {
    switch (status) {
        case MotionLiveUsbFrameStatus::kOverflow:
            DisarmUsbOwner();
            ESP_LOGW(TAG, "Live USB frame overflow; session disarmed");
            break;
        case MotionLiveUsbFrameStatus::kTimeout:
            DisarmUsbOwner();
            ESP_LOGW(TAG, "Live USB partial frame timed out; session disarmed");
            break;
        case MotionLiveUsbFrameStatus::kDiscarded:
        case MotionLiveUsbFrameStatus::kNone:
        case MotionLiveUsbFrameStatus::kReady:
            break;
    }
}

void MotionLiveUsbTask(void*) {
    MotionLiveUsbLineFramer framer;
    uint8_t buffer[kUsbReadChunkBytes] = {};
    bool was_connected = usb_serial_jtag_is_connected();
    bool rx_drain_pending = true;

    while (true) {
        const bool connected = usb_serial_jtag_is_connected();
        if (was_connected && !connected) {
            DisarmUsbOwner();
            framer.ResetPartialLine();
            rx_drain_pending = true;
            ESP_LOGW(TAG, "Live USB connection lost; session disarmed");
        } else if (!was_connected && connected) {
            framer.ResetPartialLine();
            rx_drain_pending = true;
            ESP_LOGI(TAG, "Live USB connection detected; stale RX discarded");
        }
        was_connected = connected;
        if (!connected) {
            vTaskDelay(kUsbReadWaitTicks);
            continue;
        }
        if (rx_drain_pending) {
            // Keep discarding across bounded iterations. No dispatch is allowed
            // until the old RX stream is empty; continuous input must yield.
            rx_drain_pending = !DrainUsbRx();
            if (rx_drain_pending) {
                vTaskDelay(kUsbReadWaitTicks);
                continue;
            }
        }

        std::string json;
        const MotionLiveUsbFrameStatus timeout_status = framer.PollTimeout(NowMs());
        HandleFramerStatus(timeout_status);

        const int read = usb_serial_jtag_read_bytes(buffer, sizeof(buffer),
                                                    kUsbReadWaitTicks);
        if (read <= 0) {
            continue;
        }

        const uint64_t now_ms = NowMs();
        for (int i = 0; i < read; ++i) {
            HandleFramerStatus(framer.PollTimeout(now_ms));
            const MotionLiveUsbFrameStatus status = framer.Feed(buffer[i], now_ms, &json);
            if (status == MotionLiveUsbFrameStatus::kReady) {
                if (usb_serial_jtag_is_connected()) {
                    HandleUsbJsonLine(json);
                } else {
                    DisarmUsbOwner();
                    framer.ResetPartialLine();
                    rx_drain_pending = true;
                    ESP_LOGW(TAG, "Live USB dispatch aborted while disconnected; session disarmed");
                    break;
                }
            } else {
                HandleFramerStatus(status);
            }
        }
    }
}

}  // namespace
#endif  // CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN

void StartMotionLiveUsbTransport() {
#if defined(CONFIG_GOSHA_MOTION_LIVE_USB_LOCAL_OPT_IN)
    if (g_usb_task_handle != nullptr) {
        return;
    }

    usb_serial_jtag_connection_monitor_include();

    if (usb_serial_jtag_is_driver_installed()) {
        ESP_LOGE(TAG, "Live USB transport cannot start: USB Serial/JTAG driver already installed");
        return;
    }

    usb_serial_jtag_driver_config_t usb_config = {};
    usb_config.rx_buffer_size = kUsbDriverRxBufferBytes;
    usb_config.tx_buffer_size = kUsbDriverTxBufferBytes;
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Live USB transport driver install failed: %d", ret);
        return;
    }

    const BaseType_t task_created = xTaskCreate(&MotionLiveUsbTask,
                                                "gosha_live_usb",
                                                kUsbTaskStackBytes,
                                                nullptr,
                                                kUsbTaskPriority,
                                                &g_usb_task_handle);
    if (task_created != pdPASS) {
        g_usb_task_handle = nullptr;
        usb_serial_jtag_driver_uninstall();
        ESP_LOGE(TAG, "Live USB transport task create failed");
        return;
    }
    ESP_LOGI(TAG, "Live USB transport started on @GOSHA-LIVE framed USB Serial/JTAG");
#endif
}

}  // namespace gosha::motion_live

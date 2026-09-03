#include "esp_tcp.h"

#include <esp_log.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

static const char *TAG = "EspTcp";

EspTcp::EspTcp() {
    event_group_ = xEventGroupCreate();
    if (event_group_ != nullptr) {
        xEventGroupSetBits(event_group_, ESP_TCP_EVENT_RECEIVE_TASK_EXIT);
    }
}

EspTcp::~EspTcp() {
    Disconnect();

    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
}

bool EspTcp::Connect(const std::string& host, int port) {
    Disconnect();

    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    // host is domain
    struct hostent *server = gethostbyname(host.c_str());
    if (server == NULL) {
        last_error_ = h_errno;
        ESP_LOGE(TAG, "Failed to get host by name");
        return false;
    }
    memcpy(&server_addr.sin_addr, server->h_addr, server->h_length);

    tcp_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_fd_ < 0) {
        last_error_ = errno;
        ESP_LOGE(TAG, "Failed to create socket");
        return false;
    }

    int ret = connect(tcp_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        last_error_ = errno;
        ESP_LOGE(TAG, "Failed to connect to %s:%d, code=0x%x", host.c_str(), port, last_error_);
        close(tcp_fd_);
        tcp_fd_ = -1;
        return false;
    }

    connected_ = true;

    if (event_group_ == nullptr) {
        last_error_ = ENOMEM;
        connected_ = false;
        close(tcp_fd_);
        tcp_fd_ = -1;
        ESP_LOGE(TAG, "Failed to create TCP receive event group");
        return false;
    }

    xEventGroupClearBits(event_group_, ESP_TCP_EVENT_RECEIVE_TASK_EXIT);
    receive_task_handle_ = nullptr;
    if (xTaskCreate([](void* arg) {
        EspTcp* tcp = (EspTcp*)arg;
        tcp->receive_task_handle_ = xTaskGetCurrentTaskHandle();
        tcp->ReceiveTask();
        xEventGroupSetBits(tcp->event_group_, ESP_TCP_EVENT_RECEIVE_TASK_EXIT);
        vTaskDelete(NULL);
    }, "tcp_receive", 4096, this, 1, &receive_task_handle_) != pdPASS) {
        last_error_ = ENOMEM;
        connected_ = false;
        close(tcp_fd_);
        tcp_fd_ = -1;
        xEventGroupSetBits(event_group_, ESP_TCP_EVENT_RECEIVE_TASK_EXIT);
        ESP_LOGE(TAG, "Failed to create TCP receive task");
        return false;
    }
    return true;
}

void EspTcp::Disconnect() {
    DoDisconnect(true);
}

void EspTcp::DoDisconnect(bool wait_for_task) {
    bool was_connected = connected_;
    connected_ = false;

    if (tcp_fd_ != -1) {
        shutdown(tcp_fd_, SHUT_RDWR);
        close(tcp_fd_);
        tcp_fd_ = -1;
    }

    if (wait_for_task) {
        WaitForReceiveTaskExit();
    }

    if (was_connected && disconnect_callback_) {
        disconnect_callback_();
    }
}

void EspTcp::WaitForReceiveTaskExit() {
    if (event_group_ == nullptr) {
        return;
    }

    auto bits = xEventGroupGetBits(event_group_);
    if (bits & ESP_TCP_EVENT_RECEIVE_TASK_EXIT) {
        return;
    }

    if (receive_task_handle_ != nullptr && xTaskGetCurrentTaskHandle() == receive_task_handle_) {
        return;
    }

    // The task owns callbacks into HttpClient and touches event_group_ once
    // more after ReceiveTask() returns. Do not let the owner destroy either
    // object until that final access is complete.
    xEventGroupWaitBits(event_group_, ESP_TCP_EVENT_RECEIVE_TASK_EXIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
}

int EspTcp::Send(const std::string& data) {
    if (!connected_) {
        ESP_LOGE(TAG, "Not connected");
        return -1;
    }

    size_t total_sent = 0;
    size_t data_size = data.size();
    const char* data_ptr = data.data();

    while (total_sent < data_size) {
        int ret = send(tcp_fd_, data_ptr + total_sent, data_size - total_sent, 0);

        if (ret <= 0) {
            ESP_LOGE(TAG, "Send failed: ret=%d, errno=%d", ret, errno);
            return ret;
        }

        total_sent += ret;
    }

    return total_sent;
}

void EspTcp::ReceiveTask() {
    std::string data;
    while (connected_) {
        data.resize(1500);
        int ret = recv(tcp_fd_, data.data(), data.size(), 0);
        if (ret <= 0) {
            if (ret < 0) {
                ESP_LOGE(TAG, "TCP receive failed: %d", ret);
            }
            // 被动断开，不需要等待接收任务退出（当前就是接收任务）
            DoDisconnect(false);
            break;
        }

        if (stream_callback_) {
            data.resize(ret);
            stream_callback_(data);
        }
    }
}

int EspTcp::GetLastError() {
    return last_error_;
}

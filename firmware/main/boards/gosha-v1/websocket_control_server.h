#ifndef WEBSOCKET_CONTROL_SERVER_H
#define WEBSOCKET_CONTROL_SERVER_H

#include <esp_http_server.h>
#include <string>
#include <set>

class WebSocketControlServer {
public:
    WebSocketControlServer();
    ~WebSocketControlServer();

    bool Start(int port = 8080);
    
    void Stop();

    size_t GetClientCount() const;

private:
    httpd_handle_t server_handle_;
    std::set<int> client_fds_;

    static esp_err_t ws_handler(httpd_req_t *req);
    static void SendAsyncWork(void* arg);
    
    void HandleMessage(int sock_fd, const char* data, size_t len);
    void SendToClient(int sock_fd, const std::string& payload);
    void AddClient(int sock_fd);
    void RemoveClient(int sock_fd);
    static WebSocketControlServer* instance_;
};

#endif // WEBSOCKET_CONTROL_SERVER_H

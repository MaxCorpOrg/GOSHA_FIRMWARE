#ifndef WEBSOCKET_CONTROL_SERVER_H
#define WEBSOCKET_CONTROL_SERVER_H

#include <esp_http_server.h>
#include <cstdint>
#include <string>
#include <map>

class WebSocketControlServer {
public:
    WebSocketControlServer();
    ~WebSocketControlServer();

    bool Start(int port = 8080);
    
    void Stop();

    size_t GetClientCount() const;

private:
    httpd_handle_t server_handle_;
    std::map<int, uint64_t> client_generations_;
    uint64_t next_client_generation_ = 0;

    static esp_err_t ws_handler(httpd_req_t *req);
    static void SendAsyncWork(void* arg);
    
    void HandleMessage(int sock_fd, const char* data, size_t len);
    void SendToClient(int sock_fd, uint64_t session_generation, const std::string& payload);
    void AddClient(int sock_fd);
    void RemoveClient(int sock_fd);
    uint64_t NextClientGeneration();
    bool GetClientGeneration(int sock_fd, uint64_t* generation) const;
    bool IsClientGenerationActive(int sock_fd, uint64_t generation) const;
    static WebSocketControlServer* instance_;
};

#endif // WEBSOCKET_CONTROL_SERVER_H

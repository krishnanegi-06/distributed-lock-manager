#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
using ApiSocket = SOCKET;
constexpr ApiSocket kInvalidApiSocket = INVALID_SOCKET;
#else
using ApiSocket = int;
constexpr ApiSocket kInvalidApiSocket = -1;
#endif

namespace dlm {

struct HttpRequest {
    std::string method;
    std::string rawTarget;
    std::string path;
    std::string query;
    std::unordered_map<std::string, std::string> headers; // lowercased keys
    std::unordered_map<std::string, std::string> queryParams;
    std::string body;
    std::string clientIp;
    std::string requestId;
};

struct HttpResponse {
    int         status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::unordered_map<std::string, std::string> extraHeaders;
};

/// Cross-platform HTTP/1.1 + RFC-6455 WebSocket Server.
class HttpWsServer {
public:
    static constexpr size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr size_t kMaxBodyBytes   = 1024 * 1024; // 1 MiB

    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;
    using WsAuthHandler  = std::function<bool(const HttpRequest&)>;

    HttpWsServer(std::string host, uint16_t port, std::vector<std::string> allowedOrigins);
    ~HttpWsServer();

    void setRequestHandler(RequestHandler handler);
    void setWsAuthHandler(WsAuthHandler handler);

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    uint16_t getPort() const { return port_; }

    /// Broadcast a UTF-8 text message (JSON event) to all connected WebSocket clients.
    void broadcastWsText(const std::string& message);

    /// Sliding-window rate limiter per client IP + bucket. Returns true if allowed.
    bool checkRateLimit(const std::string& clientIp, const std::string& bucket, size_t maxPerMinute);

private:
    void acceptLoop();
    void handleConnection(ApiSocket sock, std::string clientIp);
    void handleWebSocket(ApiSocket sock);
    void reapWorkers();

    static bool recvUntil(ApiSocket sock, std::string& outBuf, const std::string& delim, size_t maxBytes);
    static bool recvExact(ApiSocket sock, std::string& outBuf, size_t needed);
    static bool sendAll(ApiSocket sock, const char* data, size_t len);
    static void closeSock(ApiSocket sock);
    static std::string buildWsTextFrame(const std::string& payload);
    bool isOriginAllowed(const std::string& origin) const;

    std::string host_;
    uint16_t    port_;
    std::vector<std::string> allowedOrigins_;

    RequestHandler requestHandler_;
    WsAuthHandler  wsAuthHandler_;

    std::atomic<bool> running_{false};
    ApiSocket         serverSock_ = kInvalidApiSocket;
    std::thread       acceptThread_;

    struct Worker {
        std::thread thread;
        std::atomic<bool> done{false};
    };
    std::mutex workerMutex_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::unordered_set<ApiSocket> activeSockets_;

    std::mutex wsMutex_;
    std::unordered_set<ApiSocket> wsClients_;

    std::mutex rateMutex_;
    std::unordered_map<std::string, std::deque<uint64_t>> rateBuckets_;
};

} // namespace dlm

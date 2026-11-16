#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "dlm/network/Message.h"

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#   include <arpa/inet.h>
#   include <netinet/in.h>
#   include <sys/socket.h>
#   include <unistd.h>
    using SocketHandle = int;
    constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace dlm {

using MessageHandler = std::function<void(const Message&)>;
using RpcHandler     = std::function<Message(const Message&)>;

class NetworkManager {
public:
    static constexpr uint32_t kMaxMessageSize = Message::kMaxSerializedBytes;

    NetworkManager(NodeId nodeId, uint16_t port);
    ~NetworkManager();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Handler registration (one handler per message type)
    void registerHandler(MessageType type, MessageHandler handler);
    /// Synchronous RPC handler: returns a reply Message written directly back to the caller's socket.
    void registerRpcHandler(MessageType type, RpcHandler handler);

    // One-way sending
    bool sendMessage(NodeId targetNode, const Message& msg,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));
    void broadcast(const Message& msg,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    // Request-Response RPC correlated via Message::requestId
    bool sendRequest(NodeId targetNode, Message req, Message& resp,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Notify pending RPC waiter when a response arrives (called automatically for registered response types,
    // or directly by handlers).
    bool resolveResponse(const Message& resp);

    // Peer management
    void addPeer(NodeId nodeId, const std::string& host, uint16_t port);
    void removePeer(NodeId nodeId);
    std::vector<NodeId> getPeerIds() const;
    bool getPeerEndpoint(NodeId nodeId, std::string& host, uint16_t& port) const;

    NodeId   getNodeId() const { return nodeId_; }
    uint16_t getPort()   const { return port_; }

    /// Helper: connect, send one message, and close (exposed for RPC clients).
    static bool sendToEndpoint(const std::string& host, uint16_t port,
                               const Message& msg,
                               std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    /// Helper: connect, send request, receive single synchronous response on same socket, and close.
    static bool requestEndpoint(const std::string& host, uint16_t port,
                                const Message& req, Message& resp,
                                std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

private:
    void acceptLoop();
    void handleClient(SocketHandle clientSock);
    void reapFinishedThreads();

    static bool recvAll(SocketHandle sock, char* buf, size_t len);
    static bool sendAll(SocketHandle sock, const char* buf, size_t len);
    static void closeSocket(SocketHandle sock);
    static void setSocketTimeouts(SocketHandle sock, std::chrono::milliseconds timeout);
    static SocketHandle connectWithTimeout(const std::string& host, uint16_t port,
                                           std::chrono::milliseconds timeout);

    NodeId   nodeId_;
    uint16_t port_;
    std::atomic<bool> running_{false};

    SocketHandle serverSocket_ = kInvalidSocket;
    std::thread  acceptThread_;

    struct ClientWorker {
        std::thread thread;
        std::atomic<bool> done{false};
    };
    std::mutex clientMutex_;
    std::vector<std::unique_ptr<ClientWorker>> clientWorkers_;
    std::unordered_set<SocketHandle> activeClientSockets_;

    mutable std::mutex handlerMutex_;
    std::unordered_map<MessageType, MessageHandler> handlers_;
    std::unordered_map<MessageType, RpcHandler>     rpcHandlers_;

    struct PeerInfo {
        std::string host;
        uint16_t    port;
    };
    mutable std::mutex peerMutex_;
    std::unordered_map<NodeId, PeerInfo> peers_;

    // Pending RPC requests keyed by requestId
    struct PendingRpc {
        std::mutex              mtx;
        std::condition_variable cv;
        bool                    ready = false;
        Message                 response;
    };
    std::atomic<uint64_t> nextRequestId_{1};
    std::mutex pendingRpcMutex_;
    std::unordered_map<uint64_t, std::shared_ptr<PendingRpc>> pendingRpcs_;

    // Platform socket init/cleanup helpers
    static void platformInit();
    static void platformCleanup();
    static std::atomic<int> platformRefCount_;
};

} // namespace dlm

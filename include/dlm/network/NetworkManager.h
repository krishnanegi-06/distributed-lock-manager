#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstdint>
#include "dlm/network/Message.h"

#ifdef _WIN32
#   include <winsock2.h>
#   include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
    using SocketHandle = int;
    constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace dlm {

using MessageHandler = std::function<void(const Message&)>;

class NetworkManager {
public:
    NetworkManager(NodeId nodeId, uint16_t port);
    ~NetworkManager();

    // Lifecycle
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Handler registration (one handler per message type)
    void registerHandler(MessageType type, MessageHandler handler);

    // Sending
    bool sendMessage(NodeId targetNode, const Message& msg);
    void broadcast(const Message& msg);

    // Peer management
    void addPeer(NodeId nodeId, const std::string& host, uint16_t port);
    void removePeer(NodeId nodeId);
    std::vector<NodeId> getPeerIds() const;

    NodeId   getNodeId() const { return nodeId_; }
    uint16_t getPort()   const { return port_; }

private:
    void acceptLoop();
    void handleClient(SocketHandle clientSock);

    /// Read exactly `len` bytes from socket, return false on failure.
    static bool recvAll(SocketHandle sock, char* buf, size_t len);
    static bool sendAll(SocketHandle sock, const char* buf, size_t len);

    NodeId   nodeId_;
    uint16_t port_;
    std::atomic<bool> running_{false};

    SocketHandle serverSocket_ = kInvalidSocket;
    std::thread  acceptThread_;
    std::vector<std::thread> clientThreads_;

    mutable std::mutex handlerMutex_;
    std::unordered_map<MessageType, MessageHandler> handlers_;

    struct PeerInfo {
        std::string host;
        uint16_t    port;
    };
    mutable std::mutex peerMutex_;
    std::unordered_map<NodeId, PeerInfo> peers_;

    // Winsock init/cleanup helpers
    static void platformInit();
    static void platformCleanup();
    static std::atomic<int> platformRefCount_;
};

} // namespace dlm

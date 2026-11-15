#include "dlm/network/NetworkManager.h"
#include "dlm/common/Logger.h"
#include <algorithm>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#   pragma comment(lib, "ws2_32.lib")
#else
#   include <cerrno>
#   include <fcntl.h>
#   include <netdb.h>
#   include <poll.h>
#endif

namespace dlm {

std::atomic<int> NetworkManager::platformRefCount_{0};

void NetworkManager::platformInit() {
#ifdef _WIN32
    if (platformRefCount_.fetch_add(1) == 0) {
        WSADATA wsaData{};
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
#endif
}

void NetworkManager::platformCleanup() {
#ifdef _WIN32
    if (platformRefCount_.fetch_sub(1) == 1) {
        WSACleanup();
    }
#endif
}

void NetworkManager::closeSocket(SocketHandle sock) {
    if (sock == kInvalidSocket) return;
#ifdef _WIN32
    ::shutdown(sock, SD_BOTH);
    ::closesocket(sock);
#else
    ::shutdown(sock, SHUT_RDWR);
    ::close(sock);
#endif
}

void NetworkManager::setSocketTimeouts(SocketHandle sock, std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) return;
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout.count());
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    struct timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

SocketHandle NetworkManager::connectWithTimeout(const std::string& host, uint16_t port,
                                                  std::chrono::milliseconds timeout) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        return kInvalidSocket;
    }

    SocketHandle sock = kInvalidSocket;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == kInvalidSocket) continue;

        // Set non-blocking for timed connect
#ifdef _WIN32
        u_long nonBlocking = 1;
        ioctlsocket(sock, FIONBIO, &nonBlocking);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0) fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

        int rc = ::connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen));
        bool connected = (rc == 0);
        if (!connected) {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
                fd_set writeSet, errSet;
                FD_ZERO(&writeSet);
                FD_ZERO(&errSet);
                FD_SET(sock, &writeSet);
                FD_SET(sock, &errSet);
                timeval tv{};
                tv.tv_sec = static_cast<long>(timeout.count() / 1000);
                tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
                int sel = select(0, nullptr, &writeSet, &errSet, &tv);
                if (sel > 0 && FD_ISSET(sock, &writeSet) && !FD_ISSET(sock, &errSet)) {
                    int soErr = 0;
                    int len = sizeof(soErr);
                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len) == 0 && soErr == 0) {
                        connected = true;
                    }
                }
            }
#else
            if (errno == EINPROGRESS) {
                struct pollfd pfd{};
                pfd.fd = sock;
                pfd.events = POLLOUT;
                int pr = poll(&pfd, 1, static_cast<int>(timeout.count()));
                if (pr > 0 && (pfd.revents & POLLOUT)) {
                    int soErr = 0;
                    socklen_t len = sizeof(soErr);
                    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &soErr, &len) == 0 && soErr == 0) {
                        connected = true;
                    }
                }
            }
#endif
        }

        if (connected) {
            // Restore blocking mode and apply read/write timeouts
#ifdef _WIN32
            u_long blocking = 0;
            ioctlsocket(sock, FIONBIO, &blocking);
#else
            int flags2 = fcntl(sock, F_GETFL, 0);
            if (flags2 >= 0) fcntl(sock, F_SETFL, flags2 & ~O_NONBLOCK);
#endif
            setSocketTimeouts(sock, timeout);
            break;
        }

        closeSocket(sock);
        sock = kInvalidSocket;
    }

    freeaddrinfo(res);
    return sock;
}

NetworkManager::NetworkManager(NodeId nodeId, uint16_t port)
    : nodeId_(nodeId), port_(port) {
    platformInit();
}

NetworkManager::~NetworkManager() {
    stop();
    platformCleanup();
}

bool NetworkManager::start() {
    if (running_.load()) return true;

    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == kInvalidSocket) {
        Logger::error("NetworkManager: Failed to create server socket.");
        return false;
    }

    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(serverSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        Logger::error("NetworkManager: Bind failed on port " + std::to_string(port_));
        closeSocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
        return false;
    }

    if (::listen(serverSocket_, SOMAXCONN) != 0) {
        Logger::error("NetworkManager: Listen failed on port " + std::to_string(port_));
        closeSocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
        return false;
    }

    running_.store(true);
    acceptThread_ = std::thread(&NetworkManager::acceptLoop, this);
    return true;
}

void NetworkManager::stop() {
    if (!running_.exchange(false)) return;

    if (serverSocket_ != kInvalidSocket) {
        closeSocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
    }

    // Wake any pending RPC callers immediately
    {
        std::lock_guard<std::mutex> lock(pendingRpcMutex_);
        for (auto& [id, rpc] : pendingRpcs_) {
            std::lock_guard<std::mutex> rl(rpc->mtx);
            rpc->cv.notify_all();
        }
    }

    // Close all active client sockets so blocked recv calls unblock
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        for (SocketHandle s : activeClientSockets_) {
            closeSocket(s);
        }
        activeClientSockets_.clear();
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    std::vector<std::unique_ptr<ClientWorker>> workers;
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        workers.swap(clientWorkers_);
    }
    for (auto& w : workers) {
        if (w->thread.joinable()) {
            w->thread.join();
        }
    }
}

void NetworkManager::registerHandler(MessageType type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    handlers_[type] = std::move(handler);
}

void NetworkManager::registerRpcHandler(MessageType type, RpcHandler handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    rpcHandlers_[type] = std::move(handler);
}

void NetworkManager::addPeer(NodeId nodeId, const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(peerMutex_);
    peers_[nodeId] = {host, port};
}

void NetworkManager::removePeer(NodeId nodeId) {
    std::lock_guard<std::mutex> lock(peerMutex_);
    peers_.erase(nodeId);
}

std::vector<NodeId> NetworkManager::getPeerIds() const {
    std::lock_guard<std::mutex> lock(peerMutex_);
    std::vector<NodeId> ids;
    ids.reserve(peers_.size());
    for (const auto& p : peers_) {
        ids.push_back(p.first);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool NetworkManager::getPeerEndpoint(NodeId nodeId, std::string& host, uint16_t& port) const {
    std::lock_guard<std::mutex> lock(peerMutex_);
    auto it = peers_.find(nodeId);
    if (it == peers_.end()) return false;
    host = it->second.host;
    port = it->second.port;
    return true;
}

bool NetworkManager::recvAll(SocketHandle sock, char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int n = ::recv(sock, buf + total, static_cast<int>(len - total), 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

bool NetworkManager::sendAll(SocketHandle sock, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int n = ::send(sock, buf + total, static_cast<int>(len - total), 0);
        if (n <= 0) return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

void NetworkManager::reapFinishedThreads() {
    std::vector<std::unique_ptr<ClientWorker>> finished;
    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        for (auto it = clientWorkers_.begin(); it != clientWorkers_.end(); ) {
            if ((*it)->done.load()) {
                finished.push_back(std::move(*it));
                it = clientWorkers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& w : finished) {
        if (w->thread.joinable()) {
            w->thread.join();
        }
    }
}

void NetworkManager::acceptLoop() {
    while (running_.load()) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int clientLen = sizeof(clientAddr);
#else
        socklen_t clientLen = sizeof(clientAddr);
#endif
        SocketHandle clientSock = ::accept(serverSocket_,
                                           reinterpret_cast<sockaddr*>(&clientAddr),
                                           &clientLen);
        if (clientSock == kInvalidSocket) {
            if (running_.load()) {
                Logger::warn("NetworkManager: accept() failed.");
            }
            break;
        }

        reapFinishedThreads();

        setSocketTimeouts(clientSock, std::chrono::milliseconds(15000));
        auto worker = std::make_unique<ClientWorker>();
        ClientWorker* rawWorker = worker.get();
        {
            std::lock_guard<std::mutex> lock(clientMutex_);
            activeClientSockets_.insert(clientSock);
            worker->thread = std::thread([this, clientSock, rawWorker]() {
                handleClient(clientSock);
                rawWorker->done.store(true);
            });
            clientWorkers_.push_back(std::move(worker));
        }
    }
}

void NetworkManager::handleClient(SocketHandle clientSock) {
    while (running_.load()) {
        uint32_t netLen = 0;
        if (!recvAll(clientSock, reinterpret_cast<char*>(&netLen), sizeof(netLen))) {
            break;
        }
        uint32_t len = ntohl(netLen);
        if (len == 0 || len > kMaxMessageSize) {
            Logger::warn("NetworkManager: rejected invalid frame size " + std::to_string(len));
            break;
        }

        std::string buf(len, '\0');
        if (!recvAll(clientSock, buf.data(), len)) {
            break;
        }

        Message msg;
        if (!Message::deserialize(buf, msg)) {
            Logger::warn("NetworkManager: failed to deserialize inbound message.");
            continue;
        }

        // First try to resolve any pending correlated RPC waiter
        if (msg.requestId != 0 && resolveResponse(msg)) {
            continue;
        }

        RpcHandler rpcHandler;
        MessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handlerMutex_);
            auto rit = rpcHandlers_.find(msg.type);
            if (rit != rpcHandlers_.end()) {
                rpcHandler = rit->second;
            } else {
                auto it = handlers_.find(msg.type);
                if (it != handlers_.end()) {
                    handler = it->second;
                }
            }
        }

        if (rpcHandler) {
            try {
                Message reply = rpcHandler(msg);
                std::string replyData = reply.serialize();
                uint32_t netReplyLen = htonl(static_cast<uint32_t>(replyData.size()));
                sendAll(clientSock, reinterpret_cast<const char*>(&netReplyLen), sizeof(netReplyLen));
                sendAll(clientSock, replyData.data(), replyData.size());
            } catch (const std::exception& e) {
                Logger::error(std::string("NetworkManager: RPC handler exception: ") + e.what());
            }
        } else if (handler) {
            try {
                handler(msg);
            } catch (const std::exception& e) {
                Logger::error(std::string("NetworkManager: handler exception: ") + e.what());
            } catch (...) {
                Logger::error("NetworkManager: unknown handler exception.");
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(clientMutex_);
        activeClientSockets_.erase(clientSock);
    }
    closeSocket(clientSock);
}

bool NetworkManager::resolveResponse(const Message& resp) {
    if (resp.requestId == 0) return false;
    std::shared_ptr<PendingRpc> rpc;
    {
        std::lock_guard<std::mutex> lock(pendingRpcMutex_);
        auto it = pendingRpcs_.find(resp.requestId);
        if (it == pendingRpcs_.end()) return false;
        rpc = it->second;
    }
    {
        std::lock_guard<std::mutex> rlock(rpc->mtx);
        rpc->response = resp;
        rpc->ready = true;
    }
    rpc->cv.notify_all();
    return true;
}

bool NetworkManager::sendToEndpoint(const std::string& host, uint16_t port,
                                    const Message& msg,
                                    std::chrono::milliseconds timeout) {
    std::string data = msg.serialize();
    if (data.size() > kMaxMessageSize) return false;

    SocketHandle sock = connectWithTimeout(host, port, timeout);
    if (sock == kInvalidSocket) return false;

    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    bool ok = sendAll(sock, reinterpret_cast<const char*>(&len), sizeof(len)) &&
              sendAll(sock, data.data(), data.size());
    closeSocket(sock);
    return ok;
}

bool NetworkManager::requestEndpoint(const std::string& host, uint16_t port,
                                     const Message& req, Message& resp,
                                     std::chrono::milliseconds timeout) {
    std::string data = req.serialize();
    if (data.size() > kMaxMessageSize) return false;

    SocketHandle sock = connectWithTimeout(host, port, timeout);
    if (sock == kInvalidSocket) return false;

    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    if (!sendAll(sock, reinterpret_cast<const char*>(&len), sizeof(len)) ||
        !sendAll(sock, data.data(), data.size())) {
        closeSocket(sock);
        return false;
    }

    uint32_t netRespLen = 0;
    if (!recvAll(sock, reinterpret_cast<char*>(&netRespLen), sizeof(netRespLen))) {
        closeSocket(sock);
        return false;
    }
    uint32_t respLen = ntohl(netRespLen);
    if (respLen == 0 || respLen > kMaxMessageSize) {
        closeSocket(sock);
        return false;
    }
    std::string buf(respLen, '\0');
    if (!recvAll(sock, buf.data(), respLen)) {
        closeSocket(sock);
        return false;
    }
    closeSocket(sock);
    return Message::deserialize(buf, resp);
}

bool NetworkManager::sendMessage(NodeId targetNode, const Message& msg,
                                 std::chrono::milliseconds timeout) {
    PeerInfo peer;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = peers_.find(targetNode);
        if (it == peers_.end()) return false;
        peer = it->second;
    }
    return sendToEndpoint(peer.host, peer.port, msg, timeout);
}

void NetworkManager::broadcast(const Message& msg, std::chrono::milliseconds timeout) {
    std::vector<NodeId> peers = getPeerIds();
    for (NodeId p : peers) {
        sendMessage(p, msg, timeout);
    }
}

bool NetworkManager::sendRequest(NodeId targetNode, Message req, Message& resp,
                                 std::chrono::milliseconds timeout) {
    if (req.requestId == 0) {
        req.requestId = nextRequestId_.fetch_add(1);
    }
    req.senderId = nodeId_;

    auto rpc = std::make_shared<PendingRpc>();
    {
        std::lock_guard<std::mutex> lock(pendingRpcMutex_);
        pendingRpcs_[req.requestId] = rpc;
    }

    if (!sendMessage(targetNode, req, timeout)) {
        std::lock_guard<std::mutex> lock(pendingRpcMutex_);
        pendingRpcs_.erase(req.requestId);
        return false;
    }

    std::unique_lock<std::mutex> rlock(rpc->mtx);
    bool got = rpc->cv.wait_for(rlock, timeout, [&]() {
        return rpc->ready || !running_.load();
    });

    {
        std::lock_guard<std::mutex> lock(pendingRpcMutex_);
        pendingRpcs_.erase(req.requestId);
    }

    if (!got || !rpc->ready) {
        return false;
    }
    resp = std::move(rpc->response);
    return true;
}

} // namespace dlm

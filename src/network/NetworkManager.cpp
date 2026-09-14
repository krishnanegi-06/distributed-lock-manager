#include "dlm/network/NetworkManager.h"
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

namespace dlm {

std::atomic<int> NetworkManager::platformRefCount_{0};

void NetworkManager::platformInit() {
    if (platformRefCount_.fetch_add(1) == 0) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
}

void NetworkManager::platformCleanup() {
    if (platformRefCount_.fetch_sub(1) == 1) {
        WSACleanup();
    }
}

NetworkManager::NetworkManager(NodeId nodeId, uint16_t port)
    : nodeId_(nodeId), port_(port)
{
    platformInit();
}

NetworkManager::~NetworkManager() {
    stop();
    platformCleanup();
}

void NetworkManager::start() {
    if (running_.load()) return;
    
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == kInvalidSocket) {
        std::cerr << "Failed to create socket.\n";
        return;
    }

    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(serverSocket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
        return;
    }

    if (listen(serverSocket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
        return;
    }

    running_.store(true);
    acceptThread_ = std::thread(&NetworkManager::acceptLoop, this);
}

void NetworkManager::stop() {
    if (!running_.exchange(false)) return;

    if (serverSocket_ != kInvalidSocket) {
        closesocket(serverSocket_);
        serverSocket_ = kInvalidSocket;
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    for (auto& t : clientThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    clientThreads_.clear();
}

void NetworkManager::registerHandler(MessageType type, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    handlers_[type] = handler;
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
    return ids;
}

bool NetworkManager::recvAll(SocketHandle sock, char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, static_cast<int>(len - total), 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool NetworkManager::sendAll(SocketHandle sock, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        int n = send(sock, buf + total, static_cast<int>(len - total), 0);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

void NetworkManager::acceptLoop() {
    while (running_.load()) {
        sockaddr_in clientAddr;
        int clientLen = sizeof(clientAddr);
        SocketHandle clientSock = accept(serverSocket_, (sockaddr*)&clientAddr, &clientLen);
        
        if (clientSock == kInvalidSocket) {
            if (running_.load()) {
                std::cerr << "Accept failed.\n";
            }
            break;
        }

        clientThreads_.emplace_back(&NetworkManager::handleClient, this, clientSock);
    }
}

void NetworkManager::handleClient(SocketHandle clientSock) {
    while (running_.load()) {
        uint32_t len = 0;
        if (!recvAll(clientSock, (char*)&len, sizeof(len))) {
            break;
        }
        len = ntohl(len);

        std::string buf(len, '\0');
        if (!recvAll(clientSock, buf.data(), len)) {
            break;
        }

        Message msg = Message::deserialize(buf);

        MessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handlerMutex_);
            auto it = handlers_.find(msg.type);
            if (it != handlers_.end()) {
                handler = it->second;
            }
        }

        if (handler) {
            handler(msg);
        }
    }
    closesocket(clientSock);
}

bool NetworkManager::sendMessage(NodeId targetNode, const Message& msg) {
    PeerInfo peer;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        auto it = peers_.find(targetNode);
        if (it == peers_.end()) return false;
        peer = it->second;
    }

    SocketHandle sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer.port);
    inet_pton(AF_INET, peer.host.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    std::string data = msg.serialize();
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));

    if (!sendAll(sock, (const char*)&len, sizeof(len))) {
        closesocket(sock);
        return false;
    }

    if (!sendAll(sock, data.c_str(), data.size())) {
        closesocket(sock);
        return false;
    }

    closesocket(sock);
    return true;
}

void NetworkManager::broadcast(const Message& msg) {
    std::vector<NodeId> peers = getPeerIds();
    for (NodeId p : peers) {
        sendMessage(p, msg);
    }
}

} // namespace dlm

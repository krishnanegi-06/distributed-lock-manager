#include "dlm/network/HeartbeatManager.h"
#include "dlm/network/NetworkManager.h"
#include <iostream>

namespace dlm {

HeartbeatManager::HeartbeatManager(
    NodeId nodeId,
    NetworkManager& network,
    std::chrono::milliseconds interval,
    std::chrono::milliseconds timeout)
    : nodeId_(nodeId), network_(network), interval_(interval), timeout_(timeout)
{
    network_.registerHandler(MessageType::HEARTBEAT, [this](const Message& msg) {
        onHeartbeatReceived(msg.senderId);
        Message ack;
        ack.type = MessageType::HEARTBEAT_ACK;
        ack.senderId = nodeId_;
        network_.sendMessage(msg.senderId, ack);
    });

    network_.registerHandler(MessageType::HEARTBEAT_ACK, [this](const Message& msg) {
        onHeartbeatReceived(msg.senderId);
    });
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&HeartbeatManager::heartbeatLoop, this);
}

void HeartbeatManager::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HeartbeatManager::onHeartbeatReceived(NodeId fromNode) {
    std::lock_guard<std::mutex> lock(livenessMutex_);
    lastSeen_[fromNode] = std::chrono::steady_clock::now();
}

void HeartbeatManager::setFailureCallback(NodeFailureCallback cb) {
    failureCb_ = cb;
}

bool HeartbeatManager::isNodeAlive(NodeId nodeId) const {
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto it = lastSeen_.find(nodeId);
    if (it == lastSeen_.end()) return false;
    auto now = std::chrono::steady_clock::now();
    return (now - it->second) <= timeout_;
}

std::vector<NodeId> HeartbeatManager::getAliveNodes() const {
    std::vector<NodeId> alive;
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto now = std::chrono::steady_clock::now();
    for (const auto& pair : lastSeen_) {
        if ((now - pair.second) <= timeout_) {
            alive.push_back(pair.first);
        }
    }
    return alive;
}

void HeartbeatManager::heartbeatLoop() {
    while (running_.load()) {
        Message msg;
        msg.type = MessageType::HEARTBEAT;
        msg.senderId = nodeId_;
        network_.broadcast(msg);

        std::this_thread::sleep_for(interval_);

        auto now = std::chrono::steady_clock::now();
        std::vector<NodeId> failedNodes;
        {
            std::lock_guard<std::mutex> lock(livenessMutex_);
            for (auto it = lastSeen_.begin(); it != lastSeen_.end(); ) {
                if ((now - it->second) > timeout_) {
                    failedNodes.push_back(it->first);
                    it = lastSeen_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (failureCb_) {
            for (NodeId failed : failedNodes) {
                failureCb_(failed);
            }
        }
    }
}

} // namespace dlm

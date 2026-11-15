#include "dlm/network/HeartbeatManager.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/common/Platform.h"

namespace dlm {

HeartbeatManager::HeartbeatManager(
    NodeId nodeId,
    NetworkManager& network,
    std::chrono::milliseconds interval,
    std::chrono::milliseconds timeout)
    : nodeId_(nodeId), network_(network), interval_(interval), timeout_(timeout) {
    auto nowSteady = std::chrono::steady_clock::now();
    for (NodeId pid : network_.getPeerIds()) {
        PeerTracker pt;
        pt.lastSeen = nowSteady;
        peers_[pid] = pt;
    }

    network_.registerHandler(MessageType::HEARTBEAT, [this](const Message& msg) {
        onHeartbeatReceived(msg.senderId);
        Message ack;
        ack.type = MessageType::HEARTBEAT_ACK;
        ack.senderId = nodeId_;
        network_.sendMessage(msg.senderId, ack, std::chrono::milliseconds(1000));
    });

    network_.registerHandler(MessageType::HEARTBEAT_ACK, [this](const Message& msg) {
        onHeartbeatReceived(msg.senderId);
    });

    network_.registerHandler(MessageType::NODE_LEAVE, [this](const Message& msg) {
        onNodeLeaveReceived(msg.senderId);
    });
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    if (running_.exchange(true)) return;
    {
        std::lock_guard<std::mutex> lock(livenessMutex_);
        auto nowSteady = std::chrono::steady_clock::now();
        for (NodeId pid : network_.getPeerIds()) {
            if (peers_.find(pid) == peers_.end()) {
                PeerTracker pt;
                pt.lastSeen = nowSteady;
                peers_[pid] = pt;
            }
        }
    }
    thread_ = std::thread(&HeartbeatManager::heartbeatLoop, this);
}

void HeartbeatManager::stop() {
    if (!running_.exchange(false)) return;
    // Notify peers of graceful shutdown so they distinguish SHUTDOWN from crash
    Message leaveMsg;
    leaveMsg.type = MessageType::NODE_LEAVE;
    leaveMsg.senderId = nodeId_;
    network_.broadcast(leaveMsg, std::chrono::milliseconds(500));

    sleepCv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HeartbeatManager::onHeartbeatReceived(NodeId fromNode) {
    if (fromNode == 0 || fromNode == nodeId_) return;
    bool recovered = false;
    {
        std::lock_guard<std::mutex> lock(livenessMutex_);
        auto& pt = peers_[fromNode];
        if (pt.state == NodeHealthState::FAILED || pt.state == NodeHealthState::SHUTDOWN) {
            recovered = true;
        }
        pt.state = NodeHealthState::HEALTHY;
        pt.lastSeen = std::chrono::steady_clock::now();
        pt.lastSeenEpochMs = nowEpochMs();
        pt.everSeen = true;
        pt.failureReported = false;
    }
    if (recovered) {
        NodeRecoveryCallback cb;
        {
            std::lock_guard<std::mutex> lock(cbMutex_);
            cb = recoveryCb_;
        }
        if (cb) cb(fromNode);
    }
}

void HeartbeatManager::onNodeLeaveReceived(NodeId fromNode) {
    if (fromNode == 0 || fromNode == nodeId_) return;
    {
        std::lock_guard<std::mutex> lock(livenessMutex_);
        auto& pt = peers_[fromNode];
        pt.state = NodeHealthState::SHUTDOWN;
        pt.failureReported = true;
    }
}

void HeartbeatManager::setFailureCallback(NodeFailureCallback cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    failureCb_ = std::move(cb);
}

void HeartbeatManager::setRecoveryCallback(NodeRecoveryCallback cb) {
    std::lock_guard<std::mutex> lock(cbMutex_);
    recoveryCb_ = std::move(cb);
}

bool HeartbeatManager::isNodeAlive(NodeId nodeId) const {
    if (nodeId == nodeId_) return true;
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto it = peers_.find(nodeId);
    if (it == peers_.end()) return false;
    if (it->second.state == NodeHealthState::FAILED ||
        it->second.state == NodeHealthState::SHUTDOWN) {
        return false;
    }
    if (!it->second.everSeen) return false;
    auto now = std::chrono::steady_clock::now();
    return (now - it->second.lastSeen) <= timeout_;
}

NodeHealthState HeartbeatManager::getNodeState(NodeId nodeId) const {
    if (nodeId == nodeId_) return NodeHealthState::HEALTHY;
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto it = peers_.find(nodeId);
    if (it == peers_.end()) return NodeHealthState::FAILED;
    return it->second.state;
}

std::vector<NodeId> HeartbeatManager::getAliveNodes() const {
    std::vector<NodeId> alive;
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto now = std::chrono::steady_clock::now();
    for (const auto& [id, pt] : peers_) {
        if (pt.everSeen &&
            pt.state != NodeHealthState::FAILED &&
            pt.state != NodeHealthState::SHUTDOWN &&
            (now - pt.lastSeen) <= timeout_) {
            alive.push_back(id);
        }
    }
    return alive;
}

std::vector<PeerHealthInfo> HeartbeatManager::getPeerHealthSnapshot() const {
    std::vector<PeerHealthInfo> out;
    std::lock_guard<std::mutex> lock(livenessMutex_);
    auto now = std::chrono::steady_clock::now();
    for (const auto& [id, pt] : peers_) {
        PeerHealthInfo info;
        info.nodeId = id;
        info.state = pt.state;
        info.lastHeartbeatEpochMs = pt.lastSeenEpochMs;
        if (pt.everSeen && interval_.count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pt.lastSeen);
            info.missedBeats = static_cast<uint64_t>(elapsed.count() / interval_.count());
        }
        out.push_back(info);
    }
    return out;
}

void HeartbeatManager::heartbeatLoop() {
    while (running_.load()) {
        Message msg;
        msg.type = MessageType::HEARTBEAT;
        msg.senderId = nodeId_;
        network_.broadcast(msg, std::chrono::milliseconds(1000));

        {
            std::unique_lock<std::mutex> lk(sleepMutex_);
            sleepCv_.wait_for(lk, interval_, [this]() { return !running_.load(); });
        }
        if (!running_.load()) break;

        auto now = std::chrono::steady_clock::now();
        auto suspectThreshold = std::chrono::milliseconds(
            std::max<int64_t>(interval_.count() * 2, timeout_.count() / 2));

        std::vector<NodeId> newlyFailed;
        {
            std::lock_guard<std::mutex> lock(livenessMutex_);
            for (NodeId pid : network_.getPeerIds()) {
                if (peers_.find(pid) == peers_.end()) {
                    PeerTracker pt;
                    pt.lastSeen = now;
                    peers_[pid] = pt;
                }
            }

            for (auto& [id, pt] : peers_) {
                if (pt.state == NodeHealthState::SHUTDOWN) continue;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - pt.lastSeen);
                if (elapsed > timeout_) {
                    pt.state = NodeHealthState::FAILED;
                    if (!pt.failureReported) {
                        pt.failureReported = true;
                        newlyFailed.push_back(id);
                    }
                } else if (elapsed > suspectThreshold) {
                    pt.state = NodeHealthState::SUSPECTED;
                }
            }
        }

        if (!newlyFailed.empty()) {
            NodeFailureCallback cb;
            {
                std::lock_guard<std::mutex> lock(cbMutex_);
                cb = failureCb_;
            }
            if (cb) {
                for (NodeId failed : newlyFailed) {
                    cb(failed);
                }
            }
        }
    }
}

} // namespace dlm

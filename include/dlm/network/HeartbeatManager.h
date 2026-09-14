#pragma once
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include "dlm/common/Types.h"

namespace dlm {

class NetworkManager;   // forward

class HeartbeatManager {
public:
    using NodeFailureCallback = std::function<void(NodeId)>;

    HeartbeatManager(
        NodeId nodeId,
        NetworkManager& network,
        std::chrono::milliseconds interval  = std::chrono::milliseconds(1000),
        std::chrono::milliseconds timeout   = std::chrono::milliseconds(3000));
    ~HeartbeatManager();

    void start();
    void stop();

    /// Called by the network layer when a HEARTBEAT or HEARTBEAT_ACK arrives.
    void onHeartbeatReceived(NodeId fromNode);

    void setFailureCallback(NodeFailureCallback cb);

    bool isNodeAlive(NodeId nodeId) const;
    std::vector<NodeId> getAliveNodes() const;

private:
    void heartbeatLoop();

    NodeId           nodeId_;
    NetworkManager&  network_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;

    std::atomic<bool> running_{false};
    std::thread       thread_;

    mutable std::mutex livenessMutex_;
    std::unordered_map<NodeId, std::chrono::steady_clock::time_point> lastSeen_;

    NodeFailureCallback failureCb_;
};

} // namespace dlm

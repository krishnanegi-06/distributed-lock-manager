#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "dlm/common/Types.h"

namespace dlm {

class NetworkManager;

struct PeerHealthInfo {
    NodeId          nodeId = 0;
    NodeHealthState state  = NodeHealthState::SUSPECTED;
    uint64_t        lastHeartbeatEpochMs = 0;
    uint64_t        missedBeats = 0;
};

class HeartbeatManager {
public:
    using NodeFailureCallback  = std::function<void(NodeId)>;
    using NodeRecoveryCallback = std::function<void(NodeId)>;

    HeartbeatManager(
        NodeId nodeId,
        NetworkManager& network,
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000),
        std::chrono::milliseconds timeout  = std::chrono::milliseconds(4000));
    ~HeartbeatManager();

    void start();
    void stop();

    void onHeartbeatReceived(NodeId fromNode);
    void onNodeLeaveReceived(NodeId fromNode);

    void setFailureCallback(NodeFailureCallback cb);
    void setRecoveryCallback(NodeRecoveryCallback cb);

    bool isNodeAlive(NodeId nodeId) const;
    NodeHealthState getNodeState(NodeId nodeId) const;
    std::vector<NodeId> getAliveNodes() const;
    std::vector<PeerHealthInfo> getPeerHealthSnapshot() const;

private:
    struct PeerTracker {
        NodeHealthState state = NodeHealthState::SUSPECTED;
        std::chrono::steady_clock::time_point lastSeen{};
        uint64_t lastSeenEpochMs = 0;
        bool everSeen = false;
        bool failureReported = false;
    };

    void heartbeatLoop();

    NodeId                    nodeId_;
    NetworkManager&           network_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;

    std::atomic<bool>       running_{false};
    std::thread             thread_;
    std::mutex              sleepMutex_;
    std::condition_variable sleepCv_;

    mutable std::mutex livenessMutex_;
    std::unordered_map<NodeId, PeerTracker> peers_;

    mutable std::mutex   cbMutex_;
    NodeFailureCallback  failureCb_;
    NodeRecoveryCallback recoveryCb_;
};

} // namespace dlm

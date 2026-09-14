#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include "dlm/common/Types.h"
#include "dlm/lock_manager/LockManager.h"

namespace dlm {

class DeadlockDetector {
public:
    using AbortCallback = std::function<void(TxnId)>;

    DeadlockDetector();
    ~DeadlockDetector();

    // -------- one-shot detection --------

    /// Detect cycles in the given edges.  Returns the set of victim TxnIds
    /// that should be aborted to break every cycle.
    std::vector<TxnId> detectCycles(const std::vector<WaitEdge>& edges) const;

    // -------- periodic (background) detection --------

    /// Start a background thread that periodically:
    ///   1. calls `getLocalEdges()` to get local WFG
    ///   2. calls `getGlobalEdges()` to get merged WFG from all nodes
    ///   3. runs cycle detection on each
    ///   4. calls `onAbort(victimTxnId)` for every victim
    void startPeriodicDetection(
        std::chrono::milliseconds interval,
        std::function<std::vector<WaitEdge>()> getLocalEdges,
        std::function<std::vector<WaitEdge>()> getGlobalEdges,
        AbortCallback onAbort);

    void stopPeriodicDetection();

private:
    /// DFS-based cycle finder.  Returns one victim per cycle found.
    std::vector<TxnId> findCycles(
        const std::unordered_map<TxnId, std::vector<TxnId>>& adj) const;

    /// Among the transactions in a cycle, pick the youngest (highest id) as
    /// the victim.
    static TxnId selectVictim(const std::vector<TxnId>& cycle);

    std::atomic<bool> running_{false};
    std::thread       thread_;
};

} // namespace dlm

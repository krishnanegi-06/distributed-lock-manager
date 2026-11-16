#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "dlm/common/Types.h"
#include "dlm/lock_manager/LockManager.h"

namespace dlm {

struct DeadlockCycle {
    std::vector<TxnId>    cycle;       // Ordered cycle path e.g. [T1, T2, T1]
    TxnId                 victim = 0;  // Selected victim TxnId
    std::vector<WaitEdge> edges;       // Involved wait edges
    uint64_t              detectedAtMs = 0;
    bool                  resolved = false;
};

class DeadlockDetector {
public:
    using AbortCallback    = std::function<void(TxnId)>;
    using DetectedCallback = std::function<void(const DeadlockCycle&)>;

    DeadlockDetector();
    ~DeadlockDetector();

    // -------- one-shot detection --------

    /// Detect cycles in the given edges. Returns the deduplicated set of victim TxnIds
    /// that should be aborted to break every detected cycle.
    std::vector<TxnId> detectCycles(const std::vector<WaitEdge>& edges) const;

    /// Detect cycles and return full cycle/edge/victim metadata.
    std::vector<DeadlockCycle> detectCyclesDetailed(const std::vector<WaitEdge>& edges) const;

    // -------- periodic (background) detection --------

    /// Start a background thread that periodically checks local and/or global WFGs.
    void startPeriodicDetection(
        std::chrono::milliseconds interval,
        std::function<std::vector<WaitEdge>()> getLocalEdges,
        std::function<std::vector<WaitEdge>()> getGlobalEdges,
        AbortCallback onAbort,
        DetectedCallback onDetected = nullptr);

    void stopPeriodicDetection();

    /// Recent detected deadlocks history (bounded).
    std::vector<DeadlockCycle> getHistory() const;
    uint64_t getTotalDetectedCount() const { return totalDetected_.load(); }

private:
    std::vector<DeadlockCycle> findCyclesDetailed(
        const std::unordered_map<TxnId, std::vector<WaitEdge>>& adj) const;

    /// Among the transactions in a cycle, pick the youngest (highest TxnId sequence,
    /// tie-broken by full TxnId) as the victim.
    static TxnId selectVictim(const std::vector<TxnId>& cycle);

    std::atomic<bool>       running_{false};
    std::thread             thread_;
    mutable std::mutex      sleepMutex_;
    std::condition_variable sleepCv_;

    mutable std::mutex         historyMutex_;
    std::vector<DeadlockCycle> history_;
    std::atomic<uint64_t>      totalDetected_{0};
};

} // namespace dlm

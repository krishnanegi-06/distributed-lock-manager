#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "dlm/common/Types.h"

namespace dlm {

// ---------- data structures ----------

/// One row/key that is currently locked.
struct LockEntry {
    LockMode                  mode = LockMode::SHARED;
    std::unordered_set<TxnId> owners;   // transactions holding this lock
};

/// An edge in the wait-for graph: waitingTxn is blocked by holdingTxn on resourceId.
struct WaitEdge {
    TxnId      waitingTxn = 0;
    TxnId      holdingTxn = 0;
    ResourceId resourceId;
};

/// Snapshot of one resource's lock state for observability and APIs.
struct LockInfo {
    ResourceId resourceId;
    LockMode   mode = LockMode::SHARED;
    std::vector<TxnId> owners;
    std::vector<std::pair<TxnId, LockMode>> waiters;
};

// ---------- local (single-node) lock manager ----------

class LockManager {
public:
    /// Acquire a lock. Blocks until compatible (or timeout expires / cancelled).
    /// Supports lock re-entry and safe SHARED -> EXCLUSIVE upgrades.
    /// @return true if granted, false on timeout or cancellation.
    bool acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds::zero());

    /// Downgrade an EXCLUSIVE lock held solely by `txnId` to SHARED.
    /// @return true if downgraded or already SHARED; false if not held.
    bool downgradeLock(TxnId txnId, const ResourceId& resourceId);

    /// Cancel any active lock wait for `txnId` without releasing held locks.
    void cancelWait(TxnId txnId);

    /// Release one lock held by txnId.
    void releaseLock(TxnId txnId, const ResourceId& resourceId);

    /// Release every lock held by txnId and cancel any active waits.
    void releaseAllLocks(TxnId txnId);

    /// Check whether txnId currently holds at least `mode` on `resourceId`.
    bool checkLock(TxnId txnId, const ResourceId& resourceId, LockMode mode) const;

    /// Return the current set of wait-for edges (for deadlock detection).
    std::vector<WaitEdge> getWaitForEdges() const;

    /// Return a consistent snapshot of all held and waiting locks.
    std::vector<LockInfo> getLockTableSnapshot() const;

    /// Return only resources with waiting transactions.
    std::vector<LockInfo> getWaitingLocks() const;

private:
    struct Waiter {
        TxnId    txnId;
        LockMode mode;
        bool     isUpgrade = false;
        uint64_t ticket    = 0;
    };

    bool isCompatible(const LockEntry& entry, TxnId txnId, LockMode requested) const;
    bool canGrantLocked(const ResourceId& resourceId, TxnId txnId, LockMode mode,
                        bool isUpgrade, uint64_t ticket) const;

    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    uint64_t                nextTicket_{1};

    // resourceId -> who owns the lock and in what mode
    std::unordered_map<ResourceId, LockEntry> lockTable_;

    // resourceId -> ordered queue of waiting transactions
    std::unordered_map<ResourceId, std::vector<Waiter>> waiters_;

    // txnId -> set of resources this txn holds
    std::unordered_map<TxnId, std::unordered_set<ResourceId>> txnLocks_;
};

} // namespace dlm

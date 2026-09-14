#pragma once
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <chrono>
#include "dlm/common/Types.h"
using namespace std;

namespace dlm {

// ---------- data structures ----------

/// One row/table that is currently locked.
struct LockEntry {
    LockMode                   mode;
    std::unordered_set<TxnId>  owners;   // transactions holding this lock
};

/// An edge in the wait-for graph: waitingTxn is blocked by holdingTxn on
/// resourceId.
struct WaitEdge {
    TxnId      waitingTxn;
    TxnId      holdingTxn;
    ResourceId resourceId;
};

// ---------- local (single-node) lock manager ----------

class LockManager {
public:
    /// Acquire a lock.  Blocks until compatible (or timeout expires).
    /// @return true if granted, false on timeout.
    bool acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                     chrono::milliseconds timeout = chrono::milliseconds::zero());

    /// Release one lock held by txnId.
    void releaseLock(TxnId txnId, const ResourceId& resourceId);

    /// Release every lock held by txnId (used on abort / commit).
    void releaseAllLocks(TxnId txnId);

    /// Check whether txnId currently holds the given lock.
    bool checkLock(TxnId txnId, const ResourceId& resourceId, LockMode mode) const;

    /// Return the current set of wait-for edges (for deadlock detection).
    std::vector<WaitEdge> getWaitForEdges() const;

private:
    bool isCompatible(const LockEntry& entry, TxnId txnId,
                      LockMode requested) const;

    mutable mutex      mutex_;
    condition_variable  cv_;

    // resourceId  →  who owns the lock and in what mode
    unordered_map<ResourceId, LockEntry> lockTable_;

    // resourceId  →  ordered queue of (txnId, requested mode) waiting
    unordered_map<ResourceId,
                       vector<pair<TxnId, LockMode>>> waiters_;

    // txnId  →  set of resources this txn holds
    unordered_map<TxnId, unordered_set<ResourceId>> txnLocks_;
};

} // namespace dlm
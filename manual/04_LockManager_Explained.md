# Chapter 4: Lock Manager Deep Dive

The **Lock Manager** (`include/dlm/lock_manager/LockManager.h` and `src/lock_manager/LockManager.cpp`) is the foundational concurrency control component of the DLM. It regulates simultaneous access to shared database resources (such as table rows or records) across multiple transactions.

---

## 1. Concurrency Theory & Lock Modes

In database management systems and operating systems, uncoordinated concurrent reads and writes cause data corruption, dirty reads, and lost updates. The Lock Manager implements **Strict Two-Phase Locking (SS2PL)** semantics using two lock modes:

### Lock Modes
1. **SHARED (S)**: Read lock. Multiple transactions can hold a `SHARED` lock on the same resource concurrently as long as none modify it.
2. **EXCLUSIVE (X)**: Write lock. Only a single transaction can hold an `EXCLUSIVE` lock. No other transaction can hold either `SHARED` or `EXCLUSIVE` while an exclusive lock is active.

### Lock Compatibility Matrix

| Requested Mode \ Current Mode | FREE | SHARED | EXCLUSIVE |
| :--- | :---: | :---: | :---: |
| **SHARED** | ✅ GRANTED | ✅ GRANTED | ❌ MUST WAIT |
| **EXCLUSIVE** | ✅ GRANTED | ❌ MUST WAIT | ❌ MUST WAIT |

---

## 2. Core Data Structures

### `LockEntry`
Represents the lock state of an individual resource:
```cpp
struct LockEntry {
    LockMode mode = LockMode::SHARED;
    std::unordered_set<TxnId> owners; // Transaction IDs currently holding this lock
};
```

### Internal State of `LockManager`
```cpp
class LockManager {
private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;

    // ResourceId -> Current Lock Status
    std::unordered_map<ResourceId, LockEntry> lockTable_;

    // ResourceId -> List of Transaction IDs waiting for this resource
    std::unordered_map<ResourceId, std::vector<TxnId>> waiters_;

    // TxnId -> Set of Resource IDs held by this transaction
    std::unordered_map<TxnId, std::unordered_set<ResourceId>> txnLocks_;
};
```

- `mtx_`: Guard protecting all internal hash maps from race conditions across multiple worker threads.
- `cv_`: Condition variable used to put blocked transactions to sleep and wake them up when holders release locks.
- `lockTable_`: Primary index mapping a resource (e.g. `"account:101"`) to its active owners.
- `waiters_`: Tracks who is waiting for what resource, directly generating edges for the **Wait-For Graph (WFG)**.
- `txnLocks_`: Reverse lookup map allowing instant bulk release of all locks when a transaction commits or aborts.

---

## 3. Step-by-Step Code Walkthrough: `acquireLock()`

Let's inspect how `acquireLock()` works under the hood:

```cpp
bool LockManager::acquireLock(TxnId txnId, const ResourceId& resId, 
                              LockMode mode, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx_);

    // Helper lambda checking if the lock is immediately grantable
    auto canGrant = [&]() {
        auto it = lockTable_.find(resId);
        if (it == lockTable_.end() || it->second.owners.empty()) return true;
        return isCompatible(it->second, txnId, mode);
    };

    if (!canGrant()) {
        // Step 1: Register this transaction as waiting for resId
        waiters_[resId].push_back(txnId);

        // Step 2: Block on the condition variable until woken or timeout expires
        bool acquired = cv_.wait_for(lock, timeout, canGrant);

        // Step 3: Remove from waiters list
        auto& wl = waiters_[resId];
        wl.erase(std::remove(wl.begin(), wl.end(), txnId), wl.end());
        if (wl.empty()) waiters_.erase(resId);

        if (!acquired) {
            // Lock acquisition timed out!
            return false;
        }
    }

    // Step 4: Grant lock
    auto& entry = lockTable_[resId];
    entry.mode = mode;
    entry.owners.insert(txnId);
    txnLocks_[txnId].insert(resId);

    return true;
}
```

### Execution Scenarios:
1. **Resource is FREE**:
   - `lockTable_.find(resId)` returns empty or owners is empty.
   - `canGrant()` returns `true`.
   - The thread skips `wait_for`, records ownership, and returns `true` immediately.
2. **Compatible Request (SHARED alongside SHARED)**:
   - Resource currently held in `SHARED` mode by `T1`.
   - `T2` requests `SHARED`.
   - `isCompatible()` evaluates `mode == SHARED && entry.mode == SHARED`, which is `true`.
   - `T2` is added to `entry.owners`. Both `T1` and `T2` concurrently hold the lock.
3. **Incompatible Request (EXCLUSIVE requested while SHARED held)**:
   - `T1` holds `SHARED`. `T3` requests `EXCLUSIVE`.
   - `isCompatible()` returns `false`.
   - `T3` inserts itself into `waiters_[resId]` and invokes `cv_.wait_for(lock, timeout, canGrant)`.
   - `std::unique_lock` automatically releases `mtx_` while sleeping so other threads can proceed!
4. **Timeout Expiration**:
   - If `T1` does not release the lock within `timeout`, `cv_.wait_for` returns `false`.
   - `T3` cleans up its entry in `waiters_` and exits cleanly returning `false`.

---

## 4. Releasing Locks: `releaseLock()` and `releaseAllLocks()`

### Individual Lock Release (`releaseLock`)
```cpp
bool LockManager::releaseLock(TxnId txnId, const ResourceId& resId) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = lockTable_.find(resId);
    if (it == lockTable_.end() || it->second.owners.count(txnId) == 0) {
        return false; // Transaction didn't hold this lock
    }

    it->second.owners.erase(txnId);
    if (it->second.owners.empty()) {
        lockTable_.erase(it);
    }

    txnLocks_[txnId].erase(resId);

    // Notify all blocked waiting threads to re-evaluate their canGrant() predicate
    cv_.notify_all();
    return true;
}
```

### Bulk Lock Release on Commit / Abort (`releaseAllLocks`)
In distributed transactions, when a transaction completes (or is aborted by deadlock detection), it must atomically surrender all locks:
```cpp
void LockManager::releaseAllLocks(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mtx_);

    auto it = txnLocks_.find(txnId);
    if (it == txnLocks_.end()) return;

    for (const auto& resId : it->second) {
        auto lit = lockTable_.find(resId);
        if (lit != lockTable_.end()) {
            lit->second.owners.erase(txnId);
            if (lit->second.owners.empty()) {
                lockTable_.erase(lit);
            }
        }
    }

    txnLocks_.erase(it);
    cv_.notify_all();
}
```

---

## 5. Wait-For Graph Edge Generation (`getWaitForEdges()`)

To detect deadlocks, the system must know which transactions are waiting for which other transactions.
`getWaitForEdges()` constructs directed edges `(Waiter -> Holder)`:

```cpp
std::vector<WaitEdge> LockManager::getWaitForEdges() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<WaitEdge> edges;

    for (const auto& [resId, waitingTxns] : waiters_) {
        auto it = lockTable_.find(resId);
        if (it == lockTable_.end()) continue;

        for (TxnId waiter : waitingTxns) {
            for (TxnId holder : it->second.owners) {
                if (waiter != holder) {
                    edges.push_back({waiter, holder, resId});
                }
            }
        }
    }
    return edges;
}
```
If Transaction 10 holds `"row:1"` and Transaction 20 is waiting for `"row:1"`, this function emits:
`WaitEdge { waiter: 20, holder: 10, resource: "row:1" }`.
This edge is directly consumed by the Deadlock Detector.

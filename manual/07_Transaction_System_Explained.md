# Chapter 7: Transaction Management & Write-Ahead Logging (WAL)

This chapter covers the transaction subsystem (`include/dlm/transaction/` and `src/transaction/`), including the transaction state machine, ACID compliance, lock tracking, and durability via Write-Ahead Logging.

---

## 1. Transaction Concept & ACID Guarantees

A **transaction** represents a logical unit of database work containing one or more read and write operations that must execute with ACID properties:

1. **Atomicity (All or Nothing)**: Either all operations succeed and their changes are permanently recorded, or the transaction aborts and all partial effects are rolled back.
2. **Consistency (Correct State Transition)**: The database transitions from one valid state to another without violating integrity constraints.
3. **Isolation (Concurrent Independence)**: Uncommitted changes made by one transaction are invisible to concurrent transactions (guaranteed via Strict 2PL).
4. **Durability (Persistence)**: Once committed, state changes survive system crashes and power loss (guaranteed via Write-Ahead Logging).

---

## 2. Transaction State Machine

The transaction state enum is defined in `include/dlm/common/Types.h`:
```cpp
enum class TxnState {
    ACTIVE,      // Transaction is currently executing operations and acquiring locks
    PREPARING,   // Participant in 2PC has prepared and voted YES
    COMMITTED,   // Transaction has successfully committed
    ABORTED      // Transaction has aborted; all locks released and work rolled back
};
```

### State Transition Diagram:
```
                beginTransaction()
                        │
                        ▼
                  ┌────────────┐
                  │   ACTIVE   │
                  └─────┬──────┘
         acquireLock()  │  (local abort / deadlock victim / timeout)
                        ├──────────────────────────┐
                        │                          │
                        │ 2PC prepare()            │
                        ▼                          │
                  ┌────────────┐                   │
                  │ PREPARING  │                   │
                  └─────┬──────┘                   │
           2PC commit() │       2PC abort()        │
                        ├─────────────┐            │
                        ▼             ▼            ▼
                 ┌───────────┐   ┌───────────────────┐
                 │ COMMITTED │   │      ABORTED      │
                 └───────────┘   └───────────────────┘
```

---

## 3. The `Transaction` Entity Class

Declared in `include/dlm/transaction/Transaction.h`:
```cpp
class Transaction {
private:
    TxnId txnId_;
    TxnState state_;
    IsolationLevel isolationLevel_;
    std::unordered_set<ResourceId> lockedResources_;
    std::unordered_set<NodeId> participantNodes_;
};
```
- `lockedResources_`: Tracks all resources currently held by this transaction. When the transaction commits or aborts, `TransactionManager` iterates this set to ensure no locks are leaked.
- `participantNodes_`: Tracks which cluster nodes were contacted during distributed operations. If this set contains other nodes, the transaction is marked as distributed and must use **Two-Phase Commit (2PC)**.

---

## 4. Write-Ahead Logging (WAL) Architecture

In computer science, the **Write-Ahead Logging protocol** states:
> *No data mutation or state transition shall be committed to persistent memory until the corresponding log record has been flushed to durable storage.*

### Log File Location & Format
Each node maintains its own independent log file: `logs/node_<nodeId>_wal.log`.
The log records are human-readable, pipe-delimited records:
`RECORD_TYPE|TXN_ID|RESOURCE_ID|DATA|TIMESTAMP`

### Record Types:
- `BEGIN`: Marks start of transaction.
- `OPERATION`: Records a write or lock action on a resource.
- `PREPARE`: Records that participant is prepared to commit (2PC Phase 1).
- `COMMIT`: Records that transaction is permanently committed.
- `ABORT`: Records that transaction was rolled back.

### Implementation in `TransactionLog.cpp`:
```cpp
void TransactionLog::logCommit(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mtx_);
    logFile_ << "COMMIT|" << txnId << "|||" << getTimestamp() << "\n";
    logFile_.flush(); // Crucial: Flush immediately to disk!
}
```

---

## 5. Transaction Manager Orchestration

`TransactionManager` (`include/dlm/transaction/TransactionManager.h`) provides the high-level API:
- `beginTransaction(IsolationLevel)`: Generates an incrementing `TxnId`, records `BEGIN` in WAL, and returns the ID.
- `acquireLock(txnId, resId, mode)`: Validates that the transaction is `ACTIVE`, invokes `LockManager::acquireLock()`, and appends to `lockedResources_`.
- `commitTransaction(txnId)`: Sets state to `COMMITTED`, logs `COMMIT` in WAL, and invokes `LockManager::releaseAllLocks(txnId)`.
- `abortTransaction(txnId)`: Sets state to `ABORTED`, logs `ABORT` in WAL, rolls back changes, and releases all held locks.

#pragma once
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include "dlm/transaction/Transaction.h"
#include "dlm/lock_manager/LockManager.h"

namespace dlm {

class TransactionLog;   // forward

/// Manages the lifecycle of all transactions on a single node.
class TransactionManager {
public:
    /// @param lockManager  local lock manager for this node
    /// @param logPath      path for the WAL file (empty = no WAL)
    explicit TransactionManager(LockManager& lockManager,
                                const std::string& logPath = "");
    ~TransactionManager();

    /// Begin a new transaction, returns its id.
    TxnId beginTransaction(
        IsolationLevel iso = IsolationLevel::SERIALIZABLE);

    /// Acquire a lock inside a transaction.
    bool acquireLock(TxnId txnId, const ResourceId& resourceId,
                     LockMode mode);

    /// Explicitly release one lock (READ_COMMITTED may do this
    /// automatically after a read).
    void releaseLock(TxnId txnId, const ResourceId& resourceId);

    /// Commit — releases all locks.
    bool commitTransaction(TxnId txnId);

    /// Abort — rollback + release all locks.
    bool abortTransaction(TxnId txnId);

    /// Query
    Transaction* getTransaction(TxnId txnId);
    TxnState     getTransactionState(TxnId txnId);

    // ---------- 2PC participant hooks ----------

    /// Phase-1 vote: can this txn be committed?
    bool prepareTransaction(TxnId txnId);

    /// Phase-2 commit (called after coordinator decision).
    void commitPrepared(TxnId txnId);

    /// Phase-2 abort.
    void abortPrepared(TxnId txnId);

private:
    void rollbackTransaction(TxnId txnId);

    LockManager& lockManager_;
    std::unique_ptr<TransactionLog> txnLog_;

    mutable std::mutex mutex_;
    std::unordered_map<TxnId, std::unique_ptr<Transaction>> transactions_;
    std::atomic<TxnId> nextTxnId_{1};
};

} // namespace dlm

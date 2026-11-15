#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "dlm/lock_manager/LockManager.h"
#include "dlm/storage/StorageEngine.h"
#include "dlm/transaction/Transaction.h"
#include "dlm/transaction/TransactionLog.h"

namespace dlm {

struct RecoveryReport {
    bool     executed             = false;
    bool     truncatedCorruptTail = false;
    uint64_t checkpointLsn        = 0;
    uint64_t walRecordsScanned    = 0;
    uint64_t redoneTransactions   = 0;
    uint64_t undoneTransactions   = 0;
    uint64_t inDoubtTransactions  = 0;
    std::vector<TxnId> inDoubtTxnIds;
    std::vector<TxnId> pendingCoordinatorCommits;
    std::vector<TxnId> pendingCoordinatorAborts;
    uint64_t recoveredAtMs        = 0;
};

struct TransactionSnapshot {
    TxnId          id = 0;
    NodeId         coordinatorNode = 0;
    TxnState       state = TxnState::ACTIVE;
    IsolationLevel isolation = IsolationLevel::SERIALIZABLE;
    uint64_t       startEpochMs = 0;
    uint64_t       endEpochMs = 0;
    std::string    abortReason;
    std::vector<ResourceId>   lockedResources;
    std::vector<NodeId>       participants;
    std::vector<TxnOperation> operations;
    size_t                    stagedWriteCount = 0;
};

/// Manages the lifecycle, Strict 2PL, write buffering, WAL, and crash recovery
/// of all transactions on a node.
class TransactionManager {
public:
    using EventCallback = std::function<void(const std::string& eventType, TxnId txnId, const std::string& detail)>;

    /// @param lockManager  local lock manager for this node
    /// @param logPath      path for the WAL file (empty = no WAL)
    /// @param nodeId       owning node id for globally unique 64-bit TxnIds
    /// @param storage      optional persistent StorageEngine (non-owning)
    explicit TransactionManager(LockManager& lockManager,
                                const std::string& logPath = "",
                                NodeId nodeId = 0,
                                StorageEngine* storage = nullptr);
    ~TransactionManager();

    void setStorageEngine(StorageEngine* storage) { storage_ = storage; }
    void setNodeId(NodeId nodeId) { nodeId_ = nodeId; }
    void setEventCallback(EventCallback cb) { eventCb_ = std::move(cb); }
    void setLockTimeout(std::chrono::milliseconds timeout) { lockTimeout_ = timeout; }

    /// Begin a new transaction, returns its globally unique TxnId.
    TxnId beginTransaction(IsolationLevel iso = IsolationLevel::SERIALIZABLE);

    /// Register or fetch a remote-originated transaction on a participant node.
    bool ensureParticipantTransaction(TxnId txnId, NodeId coordinatorNode,
                                      IsolationLevel iso = IsolationLevel::SERIALIZABLE);

    /// Acquire a lock inside a transaction (Strict 2PL).
    bool acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                     OpStatus* outStatus = nullptr);

    /// Explicitly release one lock (READ_COMMITTED may do this after a read).
    void releaseLock(TxnId txnId, const ResourceId& resourceId);

    // ---------- Local KV Transactional Operations (with write buffering) ----------
    OpStatus readKey(TxnId txnId, const std::string& key,
                     std::string& outValue, bool& outFound);
    OpStatus writeKey(TxnId txnId, const std::string& key, const std::string& value);
    OpStatus deleteKey(TxnId txnId, const std::string& key);

    /// Apply remote staged writes into a participant transaction's write buffer
    /// (acquires EXCLUSIVE lock if not already held).
    OpStatus stageRemoteWrites(TxnId txnId, NodeId coordinatorNode,
                               const std::vector<std::pair<std::string, std::optional<std::string>>>& writes);

    void addParticipant(TxnId txnId, NodeId participantNode);

    /// Commit — durably writes WAL + applies write buffer to storage + releases all locks.
    /// Idempotent if already COMMITTED.
    bool commitTransaction(TxnId txnId);

    /// Abort — discards write buffer + writes WAL ABORT + releases all locks.
    /// Idempotent if already ABORTED; returns false if already COMMITTED.
    bool abortTransaction(TxnId txnId, const std::string& reason = "");

    /// Query
    Transaction* getTransaction(TxnId txnId);
    TxnState     getTransactionState(TxnId txnId);
    std::vector<TransactionSnapshot> getTransactionsSnapshot() const;
    TransactionSnapshot getTransactionSnapshot(TxnId txnId, bool& found) const;

    // ---------- 2PC participant hooks ----------
    bool prepareTransaction(TxnId txnId);
    bool commitPrepared(TxnId txnId);
    bool abortPrepared(TxnId txnId);

    // ---------- WAL, Checkpointing & Crash Recovery ----------
    RecoveryReport recoverFromWal();
    bool createCheckpoint();
    RecoveryReport getLastRecoveryReport() const;
    std::vector<LogEntry> getWalEntries(size_t maxEntries = 500) const;
    TransactionLog* getTransactionLog() { return txnLog_.get(); }

private:
    bool commitLocked(Transaction& txn);
    bool abortLocked(Transaction& txn, const std::string& reason);
    void emitEvent(const std::string& type, TxnId txnId, const std::string& detail);

    LockManager&                    lockManager_;
    NodeId                          nodeId_ = 0;
    StorageEngine*                  storage_ = nullptr;
    std::unique_ptr<TransactionLog> txnLog_;
    std::chrono::milliseconds       lockTimeout_{5000};
    EventCallback                   eventCb_;

    mutable std::mutex mutex_;
    std::unordered_map<TxnId, std::unique_ptr<Transaction>> transactions_;
    std::atomic<uint64_t> nextSeq_{1};
    RecoveryReport        lastRecovery_;
};

} // namespace dlm

#include "dlm/transaction/TransactionManager.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <algorithm>
#include <unordered_set>

namespace dlm {

TransactionManager::TransactionManager(LockManager& lockManager,
                                       const std::string& logPath,
                                       NodeId nodeId,
                                       StorageEngine* storage)
    : lockManager_(lockManager),
      nodeId_(nodeId),
      storage_(storage) {
    if (!logPath.empty()) {
        txnLog_ = std::make_unique<TransactionLog>(logPath, nodeId_);
    }
}

TransactionManager::~TransactionManager() = default;

void TransactionManager::emitEvent(const std::string& type, TxnId txnId, const std::string& detail) {
    if (eventCb_) {
        try {
            eventCb_(type, txnId, detail);
        } catch (...) {}
    }
}

TxnId TransactionManager::beginTransaction(IsolationLevel iso) {
    uint64_t seq = nextSeq_.fetch_add(1);
    TxnId id = (nodeId_ == 0) ? seq : makeTxnId(nodeId_, seq);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        transactions_[id] = std::make_unique<Transaction>(id, iso, nodeId_);
        if (txnLog_) {
            txnLog_->logBegin(id);
        }
    }
    emitEvent("transaction.created", id, "ACTIVE");
    return id;
}

bool TransactionManager::ensureParticipantTransaction(TxnId txnId, NodeId coordinatorNode,
                                                      IsolationLevel iso) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it != transactions_.end()) {
        return !isTerminalState(it->second->getState());
    }
    auto txn = std::make_unique<Transaction>(txnId, iso, coordinatorNode);
    txn->addParticipantNode(nodeId_);
    transactions_[txnId] = std::move(txn);
    if (txnLog_) {
        txnLog_->logBegin(txnId);
    }
    return true;
}

bool TransactionManager::acquireLock(TxnId txnId, const ResourceId& resourceId,
                                     LockMode mode, OpStatus* outStatus) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
            if (outStatus) *outStatus = OpStatus::INVALID_STATE;
            return false;
        }
        it->second->transitionState(TxnState::WAITING);
    }

    bool granted = lockManager_.acquireLock(txnId, resourceId, mode, lockTimeout_);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        if (!granted) lockManager_.releaseLock(txnId, resourceId);
        if (outStatus) *outStatus = OpStatus::INVALID_STATE;
        return false;
    }

    TxnState cur = it->second->getState();
    if (cur == TxnState::ABORTED) {
        // Aborted while waiting (e.g. deadlock victim or explicit abort)
        if (granted) lockManager_.releaseLock(txnId, resourceId);
        if (outStatus) *outStatus = OpStatus::DEADLOCK_ABORT;
        return false;
    }

    if (cur == TxnState::WAITING) {
        it->second->transitionState(TxnState::ACTIVE);
    }

    if (granted) {
        it->second->addLockedResource(resourceId);
        it->second->addOperation(TxnOperation{
            TxnOperation::OpType::LOCK_ACQUIRE, resourceId, mode, "", "", false, nodeId_
        });
        if (txnLog_) {
            txnLog_->logOperation(txnId, resourceId, to_string(mode));
        }
        if (outStatus) *outStatus = OpStatus::OK;
        return true;
    }

    if (outStatus) *outStatus = OpStatus::LOCK_TIMEOUT;
    return false;
}

void TransactionManager::releaseLock(TxnId txnId, const ResourceId& resourceId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end()) return;
    }
    lockManager_.releaseLock(txnId, resourceId);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it != transactions_.end()) {
            it->second->removeLockedResource(resourceId);
        }
    }
}

OpStatus TransactionManager::readKey(TxnId txnId, const std::string& key,
                                     std::string& outValue, bool& outFound) {
    if (key.empty()) return OpStatus::INVALID_ARGUMENT;
    IsolationLevel iso = IsolationLevel::SERIALIZABLE;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
            return OpStatus::INVALID_STATE;
        }
        iso = it->second->getIsolation();
    }

    OpStatus lockSt = OpStatus::OK;
    if (!acquireLock(txnId, key, LockMode::SHARED, &lockSt)) {
        return lockSt;
    }

    std::optional<std::string> stagedVal;
    bool inWriteBuffer = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
            return OpStatus::INVALID_STATE;
        }
        inWriteBuffer = it->second->getStagedWrite(key, stagedVal);
    }

    if (inWriteBuffer) {
        if (stagedVal.has_value()) {
            outValue = *stagedVal;
            outFound = true;
        } else {
            outValue.clear();
            outFound = false;
        }
    } else if (storage_) {
        auto rec = storage_->get(key);
        if (rec.has_value()) {
            outValue = rec->value;
            outFound = true;
        } else {
            outValue.clear();
            outFound = false;
        }
    } else {
        outValue.clear();
        outFound = false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it != transactions_.end()) {
            it->second->addOperation(TxnOperation{
                TxnOperation::OpType::READ, key, LockMode::SHARED,
                outFound ? outValue : "", "", outFound, nodeId_
            });
        }
    }

    if (iso == IsolationLevel::READ_COMMITTED) {
        // Only release if we don't hold an EXCLUSIVE write lock on the key
        if (!lockManager_.checkLock(txnId, key, LockMode::EXCLUSIVE)) {
            releaseLock(txnId, key);
        }
    }

    return OpStatus::OK;
}

OpStatus TransactionManager::writeKey(TxnId txnId, const std::string& key, const std::string& value) {
    if (key.empty()) return OpStatus::INVALID_ARGUMENT;
    OpStatus lockSt = OpStatus::OK;
    if (!acquireLock(txnId, key, LockMode::EXCLUSIVE, &lockSt)) {
        return lockSt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
        return OpStatus::INVALID_STATE;
    }

    std::string prevVal;
    bool prevExisted = false;
    if (storage_) {
        auto rec = storage_->get(key);
        if (rec.has_value()) {
            prevVal = rec->value;
            prevExisted = true;
        }
    }

    it->second->stagePut(key, value);
    it->second->addOperation(TxnOperation{
        TxnOperation::OpType::WRITE, key, LockMode::EXCLUSIVE,
        value, prevVal, prevExisted, nodeId_
    });
    return OpStatus::OK;
}

OpStatus TransactionManager::deleteKey(TxnId txnId, const std::string& key) {
    if (key.empty()) return OpStatus::INVALID_ARGUMENT;
    OpStatus lockSt = OpStatus::OK;
    if (!acquireLock(txnId, key, LockMode::EXCLUSIVE, &lockSt)) {
        return lockSt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
        return OpStatus::INVALID_STATE;
    }

    std::string prevVal;
    bool prevExisted = false;
    if (storage_) {
        auto rec = storage_->get(key);
        if (rec.has_value()) {
            prevVal = rec->value;
            prevExisted = true;
        }
    }

    it->second->stageDelete(key);
    it->second->addOperation(TxnOperation{
        TxnOperation::OpType::ERASE, key, LockMode::EXCLUSIVE,
        "", prevVal, prevExisted, nodeId_
    });
    return OpStatus::OK;
}

OpStatus TransactionManager::stageRemoteWrites(
        TxnId txnId, NodeId coordinatorNode,
        const std::vector<std::pair<std::string, std::optional<std::string>>>& writes) {
    if (!ensureParticipantTransaction(txnId, coordinatorNode)) {
        return OpStatus::INVALID_STATE;
    }
    for (const auto& [k, optVal] : writes) {
        if (optVal.has_value()) {
            OpStatus st = writeKey(txnId, k, *optVal);
            if (st != OpStatus::OK) return st;
        } else {
            OpStatus st = deleteKey(txnId, k);
            if (st != OpStatus::OK) return st;
        }
    }
    return OpStatus::OK;
}

void TransactionManager::addParticipant(TxnId txnId, NodeId participantNode) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it != transactions_.end()) {
        it->second->addParticipantNode(participantNode);
    }
}

bool TransactionManager::commitLocked(Transaction& txn) {
    TxnState st = txn.getState();
    if (st == TxnState::COMMITTED) {
        return true; // Idempotent
    }
    if (st == TxnState::ABORTED || st == TxnState::WAITING) {
        return false;
    }

    const auto& wb = txn.getWriteBuffer();
    std::vector<std::pair<std::string, std::optional<std::string>>> batch;
    batch.reserve(wb.size());

    // 1. Log undo/redo WAL records for any staged writes not yet logged in prepare
    if (txnLog_ && st == TxnState::ACTIVE) {
        for (const auto& [k, optVal] : wb) {
            std::string prevVal;
            bool prevExisted = false;
            if (storage_) {
                auto rec = storage_->get(k);
                if (rec.has_value()) {
                    prevVal = rec->value;
                    prevExisted = true;
                }
            }
            if (optVal.has_value()) {
                if (txnLog_->logPut(txn.getId(), k, prevVal, prevExisted, *optVal) == 0) {
                    return false;
                }
            } else {
                if (txnLog_->logDelete(txn.getId(), k, prevVal, prevExisted) == 0) {
                    return false;
                }
            }
        }
    }

    // 2. Durable WAL COMMIT before exposing writes
    if (txnLog_) {
        if (txnLog_->logCommit(txn.getId()) == 0) {
            return false;
        }
    }

    // 3. Apply staged writes to StorageEngine
    for (const auto& [k, optVal] : wb) {
        batch.emplace_back(k, optVal);
    }
    if (storage_ && !batch.empty()) {
        storage_->applyCommittedBatch(batch, true);
    }

    txn.clearWriteBuffer();
    txn.transitionState(TxnState::COMMITTED);
    lockManager_.releaseAllLocks(txn.getId());
    return true;
}

bool TransactionManager::abortLocked(Transaction& txn, const std::string& reason) {
    TxnState st = txn.getState();
    if (st == TxnState::ABORTED) {
        return true; // Idempotent
    }
    if (st == TxnState::COMMITTED) {
        return false; // Cannot abort an already committed transaction
    }

    if (!reason.empty() && txn.getAbortReason().empty()) {
        txn.setAbortReason(reason);
    }
    txn.clearWriteBuffer();
    lockManager_.cancelWait(txn.getId());
    txn.transitionState(TxnState::ABORTED);
    lockManager_.releaseAllLocks(txn.getId());

    if (txnLog_) {
        txnLog_->logAbort(txn.getId());
    }
    return true;
}

bool TransactionManager::commitTransaction(TxnId txnId) {
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end()) {
            return false;
        }
        ok = commitLocked(*it->second);
    }
    if (ok) {
        emitEvent("transaction.committed", txnId, "COMMITTED");
    }
    return ok;
}

bool TransactionManager::abortTransaction(TxnId txnId, const std::string& reason) {
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end()) {
            return false;
        }
        ok = abortLocked(*it->second, reason);
    }
    if (ok) {
        emitEvent("transaction.aborted", txnId, reason.empty() ? "ABORTED" : reason);
    }
    return ok;
}

Transaction* TransactionManager::getTransaction(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it != transactions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

TxnState TransactionManager::getTransactionState(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it != transactions_.end()) {
        return it->second->getState();
    }
    return TxnState::ABORTED;
}

std::vector<TransactionSnapshot> TransactionManager::getTransactionsSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TransactionSnapshot> out;
    out.reserve(transactions_.size());
    for (const auto& [id, txn] : transactions_) {
        TransactionSnapshot snap;
        snap.id = txn->getId();
        snap.coordinatorNode = txn->getCoordinatorNode();
        snap.state = txn->getState();
        snap.isolation = txn->getIsolation();
        snap.startEpochMs = txn->getStartEpochMs();
        snap.endEpochMs = txn->getEndEpochMs();
        snap.abortReason = txn->getAbortReason();
        snap.lockedResources.assign(txn->getLockedResources().begin(),
                                    txn->getLockedResources().end());
        std::sort(snap.lockedResources.begin(), snap.lockedResources.end());
        snap.participants.assign(txn->getParticipantNodes().begin(),
                                 txn->getParticipantNodes().end());
        std::sort(snap.participants.begin(), snap.participants.end());
        snap.operations = txn->getOperations();
        snap.stagedWriteCount = txn->getWriteBuffer().size();
        out.push_back(std::move(snap));
    }
    std::sort(out.begin(), out.end(), [](const TransactionSnapshot& a, const TransactionSnapshot& b) {
        return a.startEpochMs > b.startEpochMs || (a.startEpochMs == b.startEpochMs && a.id > b.id);
    });
    return out;
}

TransactionSnapshot TransactionManager::getTransactionSnapshot(TxnId txnId, bool& found) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        found = false;
        return {};
    }
    found = true;
    const auto& txn = it->second;
    TransactionSnapshot snap;
    snap.id = txn->getId();
    snap.coordinatorNode = txn->getCoordinatorNode();
    snap.state = txn->getState();
    snap.isolation = txn->getIsolation();
    snap.startEpochMs = txn->getStartEpochMs();
    snap.endEpochMs = txn->getEndEpochMs();
    snap.abortReason = txn->getAbortReason();
    snap.lockedResources.assign(txn->getLockedResources().begin(),
                                txn->getLockedResources().end());
    snap.participants.assign(txn->getParticipantNodes().begin(),
                             txn->getParticipantNodes().end());
    snap.operations = txn->getOperations();
    snap.stagedWriteCount = txn->getWriteBuffer().size();
    return snap;
}

bool TransactionManager::prepareTransaction(TxnId txnId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end()) {
            return false;
        }
        TxnState cur = it->second->getState();
        if (cur == TxnState::PREPARED || cur == TxnState::COMMITTED) {
            return true; // Idempotent duplicate PREPARE
        }
        if (cur != TxnState::ACTIVE && cur != TxnState::PREPARING) {
            return false;
        }

        if (cur == TxnState::ACTIVE) {
            it->second->transitionState(TxnState::PREPARING);
        }

        // Persist redo/undo records + PREPARE before voting YES
        if (txnLog_) {
            for (const auto& [k, optVal] : it->second->getWriteBuffer()) {
                std::string prevVal;
                bool prevExisted = false;
                if (storage_) {
                    auto rec = storage_->get(k);
                    if (rec.has_value()) {
                        prevVal = rec->value;
                        prevExisted = true;
                    }
                }
                if (optVal.has_value()) {
                    if (txnLog_->logPut(txnId, k, prevVal, prevExisted, *optVal) == 0) {
                        return false;
                    }
                } else {
                    if (txnLog_->logDelete(txnId, k, prevVal, prevExisted) == 0) {
                        return false;
                    }
                }
            }
            if (txnLog_->logPrepare(txnId) == 0) {
                return false;
            }
        }

        it->second->transitionState(TxnState::PREPARED);
    }
    emitEvent("transaction.updated", txnId, "PREPARED");
    return true;
}

bool TransactionManager::commitPrepared(TxnId txnId) {
    return commitTransaction(txnId);
}

bool TransactionManager::abortPrepared(TxnId txnId) {
    return abortTransaction(txnId, "2PC global abort");
}

RecoveryReport TransactionManager::recoverFromWal() {
    RecoveryReport report;
    report.executed = true;
    report.recoveredAtMs = nowEpochMs();

    uint64_t cpLsn = 0;
    if (storage_) {
        storage_->recover(&cpLsn);
    }
    report.checkpointLsn = cpLsn;

    if (!txnLog_) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastRecovery_ = report;
        return report;
    }

    bool truncatedTail = false;
    auto entries = txnLog_->readLogSafe(&truncatedTail);
    report.truncatedCorruptTail = truncatedTail;
    report.walRecordsScanned = entries.size();

    struct WalTxnInfo {
        TxnId txnId = 0;
        NodeId nodeId = 0;
        bool hasBegin = false;
        bool hasPrepare = false;
        bool hasCommit = false;
        bool hasAbort = false;
        bool hasDecisionCommit = false;
        bool hasDecisionAbort = false;
        std::vector<NodeId> participants;
        std::vector<LogEntry> ops;
        uint64_t maxSeqSeen = 0;
    };

    std::unordered_map<TxnId, WalTxnInfo> walTxns;
    std::vector<TxnId> txnOrder;
    uint64_t maxLocalSeq = 0;

    for (const auto& e : entries) {
        if (e.txnId == 0) continue;
        auto& info = walTxns[e.txnId];
        if (info.txnId == 0) {
            info.txnId = e.txnId;
            info.nodeId = e.nodeId;
            txnOrder.push_back(e.txnId);
        }

        uint64_t seq = (nodeId_ == 0) ? e.txnId : txnSequence(e.txnId);
        if ((nodeId_ == 0 || txnOriginNode(e.txnId) == nodeId_) && seq > maxLocalSeq) {
            maxLocalSeq = seq;
        }

        switch (e.type) {
            case LogEntry::Type::TXN_BEGIN:
                info.hasBegin = true;
                break;
            case LogEntry::Type::TXN_PREPARE:
                info.hasPrepare = true;
                break;
            case LogEntry::Type::TXN_COMMIT:
                info.hasCommit = true;
                break;
            case LogEntry::Type::TXN_ABORT:
                info.hasAbort = true;
                break;
            case LogEntry::Type::TXN_DECISION_COMMIT:
                info.hasDecisionCommit = true;
                info.participants = e.participants;
                break;
            case LogEntry::Type::TXN_DECISION_ABORT:
                info.hasDecisionAbort = true;
                info.participants = e.participants;
                break;
            case LogEntry::Type::OP_PUT:
            case LogEntry::Type::OP_DELETE:
                info.ops.push_back(e);
                break;
            default:
                break;
        }
    }

    if (maxLocalSeq >= nextSeq_.load()) {
        nextSeq_.store(maxLocalSeq + 1);
    }

    // 1. REDO committed transactions (idempotent)
    std::vector<std::pair<std::string, std::optional<std::string>>> redoChanges;
    for (TxnId tid : txnOrder) {
        const auto& info = walTxns[tid];
        if (info.hasCommit) {
            bool anyPostCheckpoint = false;
            for (const auto& op : info.ops) {
                if (op.lsn > cpLsn || cpLsn == 0) {
                    anyPostCheckpoint = true;
                    if (op.type == LogEntry::Type::OP_PUT) {
                        redoChanges.emplace_back(op.resourceId, op.data);
                    } else if (op.type == LogEntry::Type::OP_DELETE) {
                        redoChanges.emplace_back(op.resourceId, std::nullopt);
                    }
                }
            }
            if (anyPostCheckpoint) {
                report.redoneTransactions++;
            }
        }
    }
    if (storage_ && !redoChanges.empty()) {
        storage_->applyCommittedBatch(redoChanges, true);
    }

    // 2. UNDO uncommitted non-prepared transactions & restore PREPARED in-doubt transactions
    std::vector<std::pair<std::string, std::optional<std::string>>> undoChanges;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = txnOrder.rbegin(); it != txnOrder.rend(); ++it) {
        TxnId tid = *it;
        const auto& info = walTxns[tid];

        if (info.hasCommit || info.hasAbort) {
            continue;
        }

        if (info.hasDecisionCommit) {
            report.pendingCoordinatorCommits.push_back(tid);
        } else if (info.hasDecisionAbort) {
            report.pendingCoordinatorAborts.push_back(tid);
        }

        if (info.hasPrepare) {
            // In-doubt 2PC participant transaction: restore PREPARED state and re-acquire locks!
            auto txn = std::make_unique<Transaction>(tid, IsolationLevel::SERIALIZABLE,
                                                     txnOriginNode(tid));
            for (auto opIt = info.ops.begin(); opIt != info.ops.end(); ++opIt) {
                const auto& op = *opIt;
                if (!op.resourceId.empty()) {
                    lockManager_.acquireLock(tid, op.resourceId, LockMode::EXCLUSIVE,
                                             std::chrono::milliseconds(1000));
                    txn->addLockedResource(op.resourceId);
                    if (op.type == LogEntry::Type::OP_PUT) {
                        txn->stagePut(op.resourceId, op.data);
                    } else if (op.type == LogEntry::Type::OP_DELETE) {
                        txn->stageDelete(op.resourceId);
                    }
                }
            }
            txn->transitionState(TxnState::PREPARED);
            transactions_[tid] = std::move(txn);
            report.inDoubtTransactions++;
            report.inDoubtTxnIds.push_back(tid);
        } else {
            // Uncommitted ACTIVE transaction: undo in reverse order and log ABORT
            for (auto opIt = info.ops.rbegin(); opIt != info.ops.rend(); ++opIt) {
                if (opIt->prevExisted) {
                    undoChanges.emplace_back(opIt->resourceId, opIt->prevData);
                } else {
                    undoChanges.emplace_back(opIt->resourceId, std::nullopt);
                }
            }
            txnLog_->logAbort(tid);
            report.undoneTransactions++;
        }
    }

    if (storage_ && !undoChanges.empty()) {
        storage_->applyCommittedBatch(undoChanges, true);
    }

    lastRecovery_ = report;
    return report;
}

bool TransactionManager::createCheckpoint() {
    if (!storage_ || !txnLog_) return false;
    uint64_t lsn = txnLog_->getLastLsn();
    if (!storage_->checkpoint(lsn)) return false;

    // Retain WAL entries only for transactions that are still active or prepared (in-doubt)
    std::unordered_set<TxnId> keepTxns;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [tid, txn] : transactions_) {
            if (!isTerminalState(txn->getState())) {
                keepTxns.insert(tid);
            }
        }
    }
    auto entries = txnLog_->readLogSafe(nullptr);
    std::vector<LogEntry> activeEntries;
    for (const auto& e : entries) {
        if (e.txnId != 0 && keepTxns.count(e.txnId)) {
            activeEntries.push_back(e);
        }
    }
    return txnLog_->compact(lsn, activeEntries);
}

RecoveryReport TransactionManager::getLastRecoveryReport() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastRecovery_;
}

std::vector<LogEntry> TransactionManager::getWalEntries(size_t maxEntries) const {
    if (!txnLog_) return {};
    auto entries = txnLog_->readLogSafe(nullptr);
    if (entries.size() > maxEntries) {
        entries.erase(entries.begin(), entries.begin() + (entries.size() - maxEntries));
    }
    return entries;
}

} // namespace dlm

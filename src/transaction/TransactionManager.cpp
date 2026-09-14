#include "dlm/transaction/TransactionManager.h"
#include "dlm/transaction/TransactionLog.h"
#include "dlm/common/Logger.h"

namespace dlm {

TransactionManager::TransactionManager(LockManager& lockManager, const std::string& logPath)
    : lockManager_(lockManager) {
    if (!logPath.empty()) {
        txnLog_ = std::make_unique<TransactionLog>(logPath);
    }
}

TransactionManager::~TransactionManager() = default;

TxnId TransactionManager::beginTransaction(IsolationLevel iso) {
    TxnId id = nextTxnId_++;
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_[id] = std::make_unique<Transaction>(id, iso);
    if (txnLog_) {
        txnLog_->logBegin(id);
    }
    return id;
}

bool TransactionManager::acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
            return false;
        }
    }
    
    bool granted = lockManager_.acquireLock(txnId, resourceId, mode, std::chrono::milliseconds(5000));
    
    if (granted) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it != transactions_.end()) {
            it->second->addLockedResource(resourceId);
            if (txnLog_) {
                txnLog_->logOperation(txnId, resourceId, "");
            }
        }
    }
    return granted;
}

void TransactionManager::releaseLock(TxnId txnId, const ResourceId& resourceId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = transactions_.find(txnId);
        if (it == transactions_.end()) {
            return;
        }
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

bool TransactionManager::commitTransaction(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        return false;
    }
    TxnState state = it->second->getState();
    if (state != TxnState::ACTIVE && state != TxnState::PREPARING) {
        return false;
    }
    
    it->second->setState(TxnState::COMMITTED);
    lockManager_.releaseAllLocks(txnId);
    
    if (txnLog_) {
        txnLog_->logCommit(txnId);
    }
    return true;
}

bool TransactionManager::abortTransaction(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end()) {
        return false;
    }
    it->second->setState(TxnState::ABORTED);
    rollbackTransaction(txnId);
    if (txnLog_) {
        txnLog_->logAbort(txnId);
    }
    return true;
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

bool TransactionManager::prepareTransaction(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(txnId);
    if (it == transactions_.end() || it->second->getState() != TxnState::ACTIVE) {
        return false;
    }
    it->second->setState(TxnState::PREPARING);
    if (txnLog_) {
        txnLog_->logPrepare(txnId);
    }
    return true;
}

void TransactionManager::commitPrepared(TxnId txnId) {
    // Drop lock as commitTransaction takes it
    commitTransaction(txnId);
}

void TransactionManager::abortPrepared(TxnId txnId) {
    // Drop lock as abortTransaction takes it
    abortTransaction(txnId);
}

void TransactionManager::rollbackTransaction(TxnId txnId) {
    lockManager_.releaseAllLocks(txnId);
}

} // namespace dlm

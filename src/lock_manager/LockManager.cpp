#include "dlm/lock_manager/LockManager.h"
#include "dlm/common/Logger.h"
#include <algorithm>
using namespace std;

namespace dlm {

bool LockManager::acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                              std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Add to waiters_
    auto& waitQueue = waiters_[resourceId];
    waitQueue.push_back({txnId, mode});

    auto canAcquire = [&]() {
        // If the lock is held, check compatibility with current owners
        if (lockTable_.count(resourceId)) {
            const auto& entry = lockTable_[resourceId];
            if (!isCompatible(entry, txnId, mode)) {
                return false;
            }
        }
        
        // Check for conflicts with transactions ahead of us in the wait queue
        for (const auto& [wTxnId, wMode] : waitQueue) {
            if (wTxnId == txnId) break; // Reached ourselves
            
            // Conflict if either wants EXCLUSIVE
            if (mode == LockMode::EXCLUSIVE || wMode == LockMode::EXCLUSIVE) {
                return false;
            }
        }
        return true;
    };

    bool granted = false;
    if (timeout.count() > 0) {
        granted = cv_.wait_for(lock, timeout, canAcquire);
    } else {
        cv_.wait(lock, canAcquire);
        granted = true; // infinite wait
    }

    if (granted) {
        // Remove from waitQueue
        waitQueue.erase(remove_if(waitQueue.begin(), waitQueue.end(),
                                       [txnId](const auto& p) { return p.first == txnId; }),
                        waitQueue.end());
        if (waitQueue.empty()) {
            waiters_.erase(resourceId);
        }

        // Grant the lock
        auto& entry = lockTable_[resourceId];
        if (entry.owners.empty()) {
            entry.mode = mode;
        } else if (mode == LockMode::EXCLUSIVE) {
            entry.mode = LockMode::EXCLUSIVE; // handle lock upgrade
        }
        entry.owners.insert(txnId);
        txnLocks_[txnId].insert(resourceId);
        return true;
    } else {
        // Timeout
        waitQueue.erase(remove_if(waitQueue.begin(), waitQueue.end(),
                                       [txnId](const auto& p) { return p.first == txnId; }),
                        waitQueue.end());
        if (waitQueue.empty()) {
            waiters_.erase(resourceId);
        }
        return false;
    }
}

void LockManager::releaseLock(TxnId txnId, const ResourceId& resourceId) {
    unique_lock<mutex> lock(mutex_);
    
    if (lockTable_.count(resourceId)) {
        auto& entry = lockTable_[resourceId];
        entry.owners.erase(txnId);
        if (entry.owners.empty()) {
            lockTable_.erase(resourceId);
        } else {
            // If there are still owners, it must be SHARED
            entry.mode = LockMode::SHARED; 
        }
    }
    
    if (txnLocks_.count(txnId)) {
        txnLocks_[txnId].erase(resourceId);
        if (txnLocks_[txnId].empty()) {
            txnLocks_.erase(txnId);
        }
    }
    
    cv_.notify_all();
}

void LockManager::releaseAllLocks(TxnId txnId) {
    unique_lock<mutex> lock(mutex_);
    
    if (txnLocks_.count(txnId)) {
        auto resources = txnLocks_[txnId]; // copy
        for (const auto& res : resources) {
            if (lockTable_.count(res)) {
                auto& entry = lockTable_[res];
                entry.owners.erase(txnId);
                if (entry.owners.empty()) {
                    lockTable_.erase(res);
                } else {
                    entry.mode = LockMode::SHARED;
                }
            }
        }
        txnLocks_.erase(txnId);
    }
    
    // Also remove from any wait queues just in case it was aborted while waiting
    for (auto it = waiters_.begin(); it != waiters_.end(); ) {
        auto& waitQueue = it->second;
        waitQueue.erase(remove_if(waitQueue.begin(), waitQueue.end(),
                                       [txnId](const auto& p) { return p.first == txnId; }),
                        waitQueue.end());
        if (waitQueue.empty()) {
            it = waiters_.erase(it);
        } else {
            ++it;
        }
    }
    
    cv_.notify_all();
}

bool LockManager::checkLock(TxnId txnId, const ResourceId& resourceId, LockMode mode) const {
    lock_guard<mutex> lock(mutex_);
    if (!lockTable_.count(resourceId)) return false;
    
    const auto& entry = lockTable_.at(resourceId);
    if (!entry.owners.count(txnId)) return false;
    
    if (mode == LockMode::SHARED) return true;
    return entry.mode == LockMode::EXCLUSIVE;
}

vector<WaitEdge> LockManager::getWaitForEdges() const {
    lock_guard<mutex> lock(mutex_);
    vector<WaitEdge> edges;
    
    for (const auto& [resId, waitQueue] : waiters_) {
        if (!lockTable_.count(resId)) continue;
        const auto& entry = lockTable_.at(resId);
        
        for (const auto& [wTxnId, wMode] : waitQueue) {
            for (TxnId owner : entry.owners) {
                if (wTxnId != owner) {
                    edges.push_back({wTxnId, owner, resId});
                }
            }
        }
    }
    
    return edges;
}

bool LockManager::isCompatible(const LockEntry& entry, TxnId txnId, LockMode requested) const {
    if (entry.owners.count(txnId) && entry.owners.size() == 1) {
        return true; // Already owns it exclusively, or it's the only one holding shared.
    }
    if (requested == LockMode::SHARED && entry.mode == LockMode::SHARED) {
        return true;
    }
    return false;
}

} // namespace dlm
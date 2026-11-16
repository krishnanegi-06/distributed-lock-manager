#include "dlm/lock_manager/LockManager.h"
#include "dlm/common/Logger.h"
#include <algorithm>

namespace dlm {

bool LockManager::isCompatible(const LockEntry& entry, TxnId txnId, LockMode requested) const {
    if (entry.owners.empty()) {
        return true;
    }
    if (entry.owners.count(txnId)) {
        // Already holds EXCLUSIVE (satisfies S or X), or is sole SHARED owner (upgrade allowed).
        if (entry.mode == LockMode::EXCLUSIVE) return true;
        if (requested == LockMode::SHARED) return true;
        return entry.owners.size() == 1;
    }
    return requested == LockMode::SHARED && entry.mode == LockMode::SHARED;
}

bool LockManager::canGrantLocked(const ResourceId& resourceId, TxnId txnId, LockMode mode,
                                 bool isUpgrade, uint64_t ticket) const {
    auto ltIt = lockTable_.find(resourceId);
    if (ltIt != lockTable_.end()) {
        if (!isCompatible(ltIt->second, txnId, mode)) {
            return false;
        }
    }

    auto wit = waiters_.find(resourceId);
    if (wit == waiters_.end()) return true;

    for (const auto& w : wit->second) {
        if (w.ticket == ticket) break; // reached ourselves
        // Upgrades only wait behind earlier upgrades, never behind non-upgrade waiters
        // (avoids circular deadlock where new waiter waits for txnId's S-lock while txnId waits for new waiter).
        if (isUpgrade && !w.isUpgrade) {
            continue;
        }
        // Any waiter ahead that conflicts with our requested mode blocks us to preserve FIFO fairness.
        if (mode == LockMode::EXCLUSIVE || w.mode == LockMode::EXCLUSIVE) {
            return false;
        }
    }
    return true;
}

bool LockManager::acquireLock(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                              std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Fast path for re-entrant acquisition when txnId already holds a sufficient lock.
    auto ltIt = lockTable_.find(resourceId);
    bool alreadyHolds = (ltIt != lockTable_.end() && ltIt->second.owners.count(txnId) > 0);
    if (alreadyHolds) {
        if (ltIt->second.mode == LockMode::EXCLUSIVE || mode == LockMode::SHARED) {
            return true;
        }
        // Sole owner upgrading SHARED -> EXCLUSIVE immediately.
        if (ltIt->second.owners.size() == 1) {
            ltIt->second.mode = LockMode::EXCLUSIVE;
            return true;
        }
    }

    const bool isUpgrade = alreadyHolds && (mode == LockMode::EXCLUSIVE);
    const uint64_t ticket = nextTicket_++;

    // Fast path when immediately grantable without waiting.
    if (canGrantLocked(resourceId, txnId, mode, isUpgrade, ticket)) {
        auto& entry = lockTable_[resourceId];
        if (entry.owners.empty() || mode == LockMode::EXCLUSIVE) {
            entry.mode = mode;
        }
        entry.owners.insert(txnId);
        txnLocks_[txnId].insert(resourceId);
        return true;
    }

    // Add to waiters_ queue: upgrades go before non-upgrade waiters to prevent upgrade inversion.
    auto& wq = waiters_[resourceId];
    Waiter waiter{txnId, mode, isUpgrade, ticket};
    if (isUpgrade) {
        auto insertPos = std::find_if(wq.begin(), wq.end(),
                                      [](const Waiter& w) { return !w.isUpgrade; });
        wq.insert(insertPos, waiter);
    } else {
        wq.push_back(waiter);
    }

    auto wakePredicate = [&]() {
        auto wit = waiters_.find(resourceId);
        if (wit == waiters_.end()) return true; // cancelled / removed
        bool stillWaiting = false;
        for (const auto& w : wit->second) {
            if (w.ticket == ticket) {
                stillWaiting = true;
                break;
            }
        }
        if (!stillWaiting) return true; // cancelled / aborted while waiting
        return canGrantLocked(resourceId, txnId, mode, isUpgrade, ticket);
    };

    bool waitResult = false;
    if (timeout.count() > 0) {
        waitResult = cv_.wait_for(lock, timeout, wakePredicate);
    } else {
        cv_.wait(lock, wakePredicate);
        waitResult = true;
    }

    // Remove ourselves from waiters_ if still present.
    bool stillInQueue = false;
    auto wit = waiters_.find(resourceId);
    if (wit != waiters_.end()) {
        auto& q = wit->second;
        auto pit = std::find_if(q.begin(), q.end(),
                                [ticket](const Waiter& w) { return w.ticket == ticket; });
        if (pit != q.end()) {
            stillInQueue = true;
            q.erase(pit);
            if (q.empty()) {
                waiters_.erase(wit);
            }
        }
    }

    if (!waitResult || !stillInQueue) {
        cv_.notify_all();
        return false;
    }

    // Final compatibility verification before granting.
    auto ltFinal = lockTable_.find(resourceId);
    if (ltFinal != lockTable_.end() && !isCompatible(ltFinal->second, txnId, mode)) {
        cv_.notify_all();
        return false;
    }

    auto& entry = lockTable_[resourceId];
    if (entry.owners.empty() || mode == LockMode::EXCLUSIVE) {
        entry.mode = mode;
    }
    entry.owners.insert(txnId);
    txnLocks_[txnId].insert(resourceId);
    return true;
}

bool LockManager::downgradeLock(TxnId txnId, const ResourceId& resourceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lockTable_.find(resourceId);
    if (it == lockTable_.end() || !it->second.owners.count(txnId)) {
        return false;
    }
    if (it->second.mode == LockMode::SHARED) {
        return true;
    }
    if (it->second.owners.size() == 1) {
        it->second.mode = LockMode::SHARED;
        cv_.notify_all();
        return true;
    }
    return false;
}

void LockManager::cancelWait(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed = false;
    for (auto it = waiters_.begin(); it != waiters_.end(); ) {
        auto& q = it->second;
        auto oldSize = q.size();
        q.erase(std::remove_if(q.begin(), q.end(),
                               [txnId](const Waiter& w) { return w.txnId == txnId; }),
                q.end());
        if (q.size() != oldSize) removed = true;
        if (q.empty()) {
            it = waiters_.erase(it);
        } else {
            ++it;
        }
    }
    if (removed) {
        cv_.notify_all();
    }
}

void LockManager::releaseLock(TxnId txnId, const ResourceId& resourceId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto ltIt = lockTable_.find(resourceId);
    if (ltIt != lockTable_.end()) {
        auto& entry = ltIt->second;
        entry.owners.erase(txnId);
        if (entry.owners.empty()) {
            lockTable_.erase(ltIt);
        } else {
            entry.mode = LockMode::SHARED;
        }
    }

    auto tlIt = txnLocks_.find(txnId);
    if (tlIt != txnLocks_.end()) {
        tlIt->second.erase(resourceId);
        if (tlIt->second.empty()) {
            txnLocks_.erase(tlIt);
        }
    }

    cv_.notify_all();
}

void LockManager::releaseAllLocks(TxnId txnId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto tlIt = txnLocks_.find(txnId);
    if (tlIt != txnLocks_.end()) {
        for (const auto& res : tlIt->second) {
            auto ltIt = lockTable_.find(res);
            if (ltIt != lockTable_.end()) {
                auto& entry = ltIt->second;
                entry.owners.erase(txnId);
                if (entry.owners.empty()) {
                    lockTable_.erase(ltIt);
                } else {
                    entry.mode = LockMode::SHARED;
                }
            }
        }
        txnLocks_.erase(tlIt);
    }

    for (auto it = waiters_.begin(); it != waiters_.end(); ) {
        auto& q = it->second;
        q.erase(std::remove_if(q.begin(), q.end(),
                               [txnId](const Waiter& w) { return w.txnId == txnId; }),
                q.end());
        if (q.empty()) {
            it = waiters_.erase(it);
        } else {
            ++it;
        }
    }

    cv_.notify_all();
}

bool LockManager::checkLock(TxnId txnId, const ResourceId& resourceId, LockMode mode) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lockTable_.find(resourceId);
    if (it == lockTable_.end()) return false;

    const auto& entry = it->second;
    if (!entry.owners.count(txnId)) return false;

    if (mode == LockMode::SHARED) return true;
    return entry.mode == LockMode::EXCLUSIVE;
}

std::vector<WaitEdge> LockManager::getWaitForEdges() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<WaitEdge> edges;

    for (const auto& [resId, waitQueue] : waiters_) {
        auto ltIt = lockTable_.find(resId);
        if (ltIt != lockTable_.end()) {
            const auto& entry = ltIt->second;
            for (const auto& w : waitQueue) {
                for (TxnId owner : entry.owners) {
                    if (w.txnId != owner) {
                        edges.push_back({w.txnId, owner, resId});
                    }
                }
            }
        }
        // Also capture wait-queue ordering conflicts (e.g. W2 behind incompatible W1)
        for (size_t i = 0; i < waitQueue.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                const auto& ahead = waitQueue[j];
                const auto& behind = waitQueue[i];
                if (ahead.txnId == behind.txnId) continue;
                if (behind.isUpgrade && !ahead.isUpgrade) continue;
                if (behind.mode == LockMode::EXCLUSIVE || ahead.mode == LockMode::EXCLUSIVE) {
                    edges.push_back({behind.txnId, ahead.txnId, resId});
                }
            }
        }
    }

    return edges;
}

std::vector<LockInfo> LockManager::getLockTableSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_set<ResourceId> resources;
    for (const auto& [res, _] : lockTable_) resources.insert(res);
    for (const auto& [res, _] : waiters_) resources.insert(res);

    std::vector<LockInfo> out;
    out.reserve(resources.size());
    for (const auto& res : resources) {
        LockInfo info;
        info.resourceId = res;
        auto ltIt = lockTable_.find(res);
        if (ltIt != lockTable_.end()) {
            info.mode = ltIt->second.mode;
            info.owners.assign(ltIt->second.owners.begin(), ltIt->second.owners.end());
            std::sort(info.owners.begin(), info.owners.end());
        }
        auto wIt = waiters_.find(res);
        if (wIt != waiters_.end()) {
            for (const auto& w : wIt->second) {
                info.waiters.emplace_back(w.txnId, w.mode);
            }
        }
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const LockInfo& a, const LockInfo& b) {
        return a.resourceId < b.resourceId;
    });
    return out;
}

std::vector<LockInfo> LockManager::getWaitingLocks() const {
    auto all = getLockTableSnapshot();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [](const LockInfo& info) { return info.waiters.empty(); }),
              all.end());
    return all;
}

} // namespace dlm

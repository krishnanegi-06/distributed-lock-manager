#include "dlm/transaction/Transaction.h"
#include "dlm/common/Platform.h"

namespace dlm {

Transaction::Transaction(TxnId id, IsolationLevel iso, NodeId coordinatorNode)
    : id_(id),
      state_(TxnState::ACTIVE),
      isolation_(iso),
      coordinatorNode_(coordinatorNode),
      startTime_(std::chrono::steady_clock::now()),
      startEpochMs_(nowEpochMs()) {
    if (coordinatorNode_ != 0) {
        participantNodes_.insert(coordinatorNode_);
    }
}

bool Transaction::transitionState(TxnState s) {
    if (!isValidTxnTransition(state_, s)) {
        return false;
    }
    state_ = s;
    if (isTerminalState(s)) {
        markEnded();
    }
    return true;
}

void Transaction::setState(TxnState s) {
    if (isValidTxnTransition(state_, s)) {
        state_ = s;
        if (isTerminalState(s)) {
            markEnded();
        }
    }
}

void Transaction::markEnded() {
    if (endEpochMs_ == 0) {
        endEpochMs_ = nowEpochMs();
    }
}

void Transaction::addLockedResource(const ResourceId& r) {
    lockedResources_.insert(r);
}

void Transaction::removeLockedResource(const ResourceId& r) {
    lockedResources_.erase(r);
}

void Transaction::addOperation(const TxnOperation& op) {
    operations_.push_back(op);
}

void Transaction::stagePut(const std::string& key, const std::string& value) {
    writeBuffer_[key] = value;
}

void Transaction::stageDelete(const std::string& key) {
    writeBuffer_[key] = std::nullopt;
}

bool Transaction::hasStagedWrite(const std::string& key) const {
    return writeBuffer_.find(key) != writeBuffer_.end();
}

bool Transaction::getStagedWrite(const std::string& key,
                                 std::optional<std::string>& outValue) const {
    auto it = writeBuffer_.find(key);
    if (it == writeBuffer_.end()) return false;
    outValue = it->second;
    return true;
}

void Transaction::addParticipantNode(NodeId n) {
    if (n != 0) {
        participantNodes_.insert(n);
    }
}

} // namespace dlm

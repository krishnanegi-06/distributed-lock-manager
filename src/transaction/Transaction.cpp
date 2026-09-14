#include "dlm/transaction/Transaction.h"

namespace dlm {

Transaction::Transaction(TxnId id, IsolationLevel iso)
    : id_(id), state_(TxnState::ACTIVE), isolation_(iso), startTime_(std::chrono::steady_clock::now()) {
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

void Transaction::addParticipantNode(NodeId n) {
    participantNodes_.insert(n);
}

} // namespace dlm

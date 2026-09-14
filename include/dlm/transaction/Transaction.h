#pragma once
#include <vector>
#include <unordered_set>
#include <string>
#include <chrono>
#include "dlm/common/Types.h"

namespace dlm {

/// A single recorded operation inside a transaction.
struct TxnOperation {
    enum class OpType : uint8_t { LOCK_ACQUIRE, LOCK_RELEASE, READ, WRITE };
    OpType      type;
    ResourceId  resourceId;
    LockMode    lockMode   = LockMode::SHARED;
    std::string data;
};

/// Represents one transaction on a single node.
class Transaction {
public:
    explicit Transaction(TxnId id,
                         IsolationLevel iso = IsolationLevel::SERIALIZABLE);

    // Accessors
    TxnId          getId()        const { return id_; }
    TxnState       getState()     const { return state_; }
    IsolationLevel getIsolation() const { return isolation_; }
    auto           getStartTime() const { return startTime_; }

    void setState(TxnState s) { state_ = s; }

    // Locked resources
    void addLockedResource(const ResourceId& r);
    void removeLockedResource(const ResourceId& r);
    const std::unordered_set<ResourceId>& getLockedResources() const {
        return lockedResources_;
    }

    // Operations log (for rollback)
    void addOperation(const TxnOperation& op);
    const std::vector<TxnOperation>& getOperations() const {
        return operations_;
    }

    // Multi-node awareness
    void addParticipantNode(NodeId n);
    const std::unordered_set<NodeId>& getParticipantNodes() const {
        return participantNodes_;
    }
    bool isDistributed() const { return participantNodes_.size() > 1; }

private:
    TxnId          id_;
    TxnState       state_     = TxnState::ACTIVE;
    IsolationLevel isolation_;
    std::unordered_set<ResourceId> lockedResources_;
    std::vector<TxnOperation>      operations_;
    std::unordered_set<NodeId>     participantNodes_;
    std::chrono::steady_clock::time_point startTime_;
};

} // namespace dlm

#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "dlm/common/Types.h"

namespace dlm {

/// A single recorded operation inside a transaction.
struct TxnOperation {
    enum class OpType : uint8_t { LOCK_ACQUIRE, LOCK_RELEASE, READ, WRITE, ERASE };
    OpType      type = OpType::READ;
    ResourceId  resourceId;
    LockMode    lockMode   = LockMode::SHARED;
    std::string data;
    std::string prevData;
    bool        prevExisted = false;
    NodeId      ownerNode   = 0;
};

/// Represents one transaction's local or coordinator state.
class Transaction {
public:
    explicit Transaction(TxnId id,
                         IsolationLevel iso = IsolationLevel::SERIALIZABLE,
                         NodeId coordinatorNode = 0);

    // Accessors
    TxnId          getId()              const { return id_; }
    TxnState       getState()           const { return state_; }
    IsolationLevel getIsolation()       const { return isolation_; }
    NodeId         getCoordinatorNode() const { return coordinatorNode_; }
    auto           getStartTime()       const { return startTime_; }
    uint64_t       getStartEpochMs()    const { return startEpochMs_; }
    uint64_t       getEndEpochMs()      const { return endEpochMs_; }
    const std::string& getAbortReason() const { return abortReason_; }

    /// Enforces valid lifecycle transitions. Returns false if invalid.
    bool transitionState(TxnState s);
    void setState(TxnState s); // kept for compatibility; validates unless terminal recovery
    void setAbortReason(const std::string& reason) { abortReason_ = reason; }
    void markEnded();

    // Locked resources
    void addLockedResource(const ResourceId& r);
    void removeLockedResource(const ResourceId& r);
    const std::unordered_set<ResourceId>& getLockedResources() const {
        return lockedResources_;
    }

    // Operations log (for observability and rollback)
    void addOperation(const TxnOperation& op);
    const std::vector<TxnOperation>& getOperations() const {
        return operations_;
    }

    // Per-transaction write buffer (prevents dirty reads & enables read-your-own-writes)
    void stagePut(const std::string& key, const std::string& value);
    void stageDelete(const std::string& key);
    bool hasStagedWrite(const std::string& key) const;
    /// Returns true if key is in write buffer; `outValue` is std::nullopt when staged as DELETE.
    bool getStagedWrite(const std::string& key, std::optional<std::string>& outValue) const;
    const std::unordered_map<std::string, std::optional<std::string>>& getWriteBuffer() const {
        return writeBuffer_;
    }
    void clearWriteBuffer() { writeBuffer_.clear(); }

    // Multi-node awareness
    void addParticipantNode(NodeId n);
    const std::unordered_set<NodeId>& getParticipantNodes() const {
        return participantNodes_;
    }
    bool isDistributed() const { return participantNodes_.size() > 1; }

private:
    TxnId          id_;
    TxnState       state_ = TxnState::ACTIVE;
    IsolationLevel isolation_;
    NodeId         coordinatorNode_;
    std::unordered_set<ResourceId> lockedResources_;
    std::vector<TxnOperation>      operations_;
    std::unordered_map<std::string, std::optional<std::string>> writeBuffer_;
    std::unordered_set<NodeId>     participantNodes_;
    std::chrono::steady_clock::time_point startTime_;
    uint64_t       startEpochMs_ = 0;
    uint64_t       endEpochMs_   = 0;
    std::string    abortReason_;
};

} // namespace dlm

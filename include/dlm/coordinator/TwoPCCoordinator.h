#pragma once
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "dlm/common/Platform.h"
#include "dlm/common/Types.h"
#include "dlm/network/Message.h"

namespace dlm {

class NetworkManager;
class TransactionLog;

enum class TwoPCRole  { COORDINATOR, PARTICIPANT };
enum class TwoPCState { INIT, PREPARING, PREPARED, COMMITTING, ABORTING, COMMITTED, ABORTED };

inline const char* to_string(TwoPCRole r) {
    return r == TwoPCRole::COORDINATOR ? "COORDINATOR" : "PARTICIPANT";
}

inline const char* to_string(TwoPCState s) {
    switch (s) {
        case TwoPCState::INIT:       return "INIT";
        case TwoPCState::PREPARING:  return "PREPARING";
        case TwoPCState::PREPARED:   return "PREPARED";
        case TwoPCState::COMMITTING: return "COMMITTING";
        case TwoPCState::ABORTING:   return "ABORTING";
        case TwoPCState::COMMITTED:  return "COMMITTED";
        case TwoPCState::ABORTED:    return "ABORTED";
    }
    return "UNKNOWN";
}

struct TwoPCTransactionSnapshot {
    TxnId      txnId = 0;
    TwoPCRole  role = TwoPCRole::COORDINATOR;
    TwoPCState state = TwoPCState::INIT;
    NodeId     coordinatorNode = 0;
    std::vector<NodeId> participants;
    std::unordered_map<NodeId, bool> votes;
    std::unordered_map<NodeId, bool> acks;
    bool       decisionDurable = false;
    uint64_t   updatedAtMs = 0;
};

class TwoPCCoordinator {
public:
    using PrepareHandler = std::function<bool(TxnId)>;
    using CommitHandler  = std::function<void(TxnId)>;
    using AbortHandler   = std::function<void(TxnId)>;
    using EventCallback  = std::function<void(const std::string& eventType, TxnId txnId, const std::string& detail)>;

    TwoPCCoordinator(
        NodeId nodeId,
        NetworkManager& network,
        std::chrono::milliseconds voteTimeout = std::chrono::milliseconds(5000),
        TransactionLog* txnLog = nullptr,
        const std::string& decisionLogPath = "");
    ~TwoPCCoordinator();

    void setTransactionLog(TransactionLog* txnLog) { txnLog_ = txnLog; }
    void setDecisionLogPath(const std::string& path);
    void setEventCallback(EventCallback cb) { eventCb_ = std::move(cb); }

    // ---------- coordinator side ----------

    /// Drive the full 2PC protocol for `txnId` across `participants`.
    /// Persists decision durably before broadcasting GLOBAL_COMMIT / GLOBAL_ABORT.
    bool coordinateCommit(TxnId txnId,
                          const std::vector<NodeId>& participants);

    /// Broadcast GLOBAL_ABORT to participants when aborting a distributed txn before commit.
    void coordinateAbort(TxnId txnId, const std::vector<NodeId>& participants);

    /// Retry delivery of unresolved durable coordinator decisions after restart.
    void recoverCoordinatorDecisions();

    // ---------- participant side ----------

    void registerParticipantHandlers(PrepareHandler onPrepare,
                                     CommitHandler  onCommit,
                                     AbortHandler   onAbort);

    /// Query coordinator for resolution of an in-doubt PREPARED transaction.
    /// Returns true if resolved (committed or aborted), false if still unknown/in-doubt.
    bool queryCoordinatorStatus(TxnId txnId, NodeId coordinatorNode);

    /// Called by the NetworkManager message dispatch.
    void handleMessage(const Message& msg);

    TwoPCState getTransactionState(TxnId txnId) const;
    std::vector<TwoPCTransactionSnapshot> getSnapshot() const;

private:
    bool persistDecision(TxnId txnId, TwoPCState decision, const std::vector<NodeId>& participants);
    void loadDurableDecisions();
    void emitEvent(const std::string& type, TxnId txnId, const std::string& detail);

    NodeId                    nodeId_;
    NetworkManager&           network_;
    std::chrono::milliseconds voteTimeout_;
    TransactionLog*           txnLog_ = nullptr;
    std::string               decisionLogPath_;
    DurableFile               decisionFile_;
    std::mutex                decisionFileMutex_;
    EventCallback             eventCb_;

    struct CoordTxn {
        TwoPCRole  role  = TwoPCRole::COORDINATOR;
        TwoPCState state = TwoPCState::INIT;
        NodeId     coordinatorNode = 0;
        std::vector<NodeId> participants;
        std::unordered_map<NodeId, bool> votes; // true = YES
        std::unordered_map<NodeId, bool> acks;  // true = COMMIT/ABORT ACK
        bool       decisionDurable = false;
        uint64_t   updatedAtMs = 0;
        mutable std::mutex      mtx;
        std::condition_variable cv;
    };

    mutable std::mutex coordMutex_;
    std::unordered_map<TxnId, std::shared_ptr<CoordTxn>> coordState_;

    PrepareHandler prepareHandler_;
    CommitHandler  commitHandler_;
    AbortHandler   abortHandler_;
};

} // namespace dlm

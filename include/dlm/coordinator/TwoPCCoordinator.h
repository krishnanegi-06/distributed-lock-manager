#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include "dlm/common/Types.h"
#include "dlm/network/Message.h"

namespace dlm {

class NetworkManager;   // forward

enum class TwoPCRole  { COORDINATOR, PARTICIPANT };
enum class TwoPCState { INIT, PREPARING, COMMITTING, ABORTING, COMMITTED, ABORTED };

class TwoPCCoordinator {
public:
    TwoPCCoordinator(
        NodeId nodeId,
        NetworkManager& network,
        std::chrono::milliseconds voteTimeout = std::chrono::milliseconds(5000));

    // ---------- coordinator side ----------

    /// Drive the full 2PC protocol for `txnId` across `participants`.
    /// Returns true if all voted YES and COMMIT was sent, false on abort.
    bool coordinateCommit(TxnId txnId,
                          const std::vector<NodeId>& participants);

    // ---------- participant side ----------

    using PrepareHandler = std::function<bool(TxnId)>;   // returns vote
    using CommitHandler  = std::function<void(TxnId)>;
    using AbortHandler   = std::function<void(TxnId)>;

    void registerParticipantHandlers(PrepareHandler onPrepare,
                                     CommitHandler  onCommit,
                                     AbortHandler   onAbort);

    /// Called by the NetworkManager message dispatch.
    void handleMessage(const Message& msg);

    TwoPCState getTransactionState(TxnId txnId) const;

private:
    void logDecision(TxnId txnId, TwoPCState decision);

    NodeId           nodeId_;
    NetworkManager&  network_;
    std::chrono::milliseconds voteTimeout_;

    // Per-transaction coordinator state
    struct CoordTxn {
        TwoPCState state = TwoPCState::INIT;
        std::vector<NodeId> participants;
        std::unordered_map<NodeId, bool> votes;   // true = YES
        mutable std::mutex  mtx;
        std::condition_variable cv;
    };

    mutable std::mutex coordMutex_;
    std::unordered_map<TxnId, CoordTxn> coordState_;

    // Participant handlers
    PrepareHandler prepareHandler_;
    CommitHandler  commitHandler_;
    AbortHandler   abortHandler_;
};

} // namespace dlm

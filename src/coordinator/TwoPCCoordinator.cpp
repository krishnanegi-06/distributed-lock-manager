#include "dlm/coordinator/TwoPCCoordinator.h"
#include "dlm/common/Logger.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/network/Message.h"

namespace dlm {

TwoPCCoordinator::TwoPCCoordinator(NodeId nodeId, NetworkManager& network, std::chrono::milliseconds voteTimeout)
    : nodeId_(nodeId), network_(network), voteTimeout_(voteTimeout) {
}

bool TwoPCCoordinator::coordinateCommit(TxnId txnId, const std::vector<NodeId>& participants) {
    std::unique_lock<std::mutex> mainLock(coordMutex_);
    auto& txn = coordState_[txnId];
    mainLock.unlock();

    std::unique_lock<std::mutex> txnLock(txn.mtx);
    txn.state = TwoPCState::PREPARING;
    txn.participants = participants;
    txnLock.unlock();

    Message msg;
    msg.type = MessageType::PREPARE;
    msg.senderId = nodeId_;
    msg.txnId = txnId;

    for (NodeId p : participants) {
        network_.sendMessage(p, msg);
    }

    txnLock.lock();
    bool all_voted = txn.cv.wait_for(txnLock, voteTimeout_, [&]() {
        return txn.votes.size() == txn.participants.size();
    });

    bool commit = true;
    if (!all_voted) {
        commit = false;
    } else {
        for (const auto& [p, vote] : txn.votes) {
            if (!vote) {
                commit = false;
                break;
            }
        }
    }

    if (commit) {
        txn.state = TwoPCState::COMMITTING;
        logDecision(txnId, txn.state);
        txnLock.unlock();

        Message commitMsg;
        commitMsg.type = MessageType::GLOBAL_COMMIT;
        commitMsg.senderId = nodeId_;
        commitMsg.txnId = txnId;
        for (NodeId p : participants) {
            network_.sendMessage(p, commitMsg);
        }

        txnLock.lock();
        txn.state = TwoPCState::COMMITTED;
        return true;
    } else {
        txn.state = TwoPCState::ABORTING;
        logDecision(txnId, txn.state);
        txnLock.unlock();

        Message abortMsg;
        abortMsg.type = MessageType::GLOBAL_ABORT;
        abortMsg.senderId = nodeId_;
        abortMsg.txnId = txnId;
        for (NodeId p : participants) {
            network_.sendMessage(p, abortMsg);
        }

        txnLock.lock();
        txn.state = TwoPCState::ABORTED;
        return false;
    }
}

void TwoPCCoordinator::registerParticipantHandlers(PrepareHandler onPrepare, CommitHandler onCommit, AbortHandler onAbort) {
    prepareHandler_ = std::move(onPrepare);
    commitHandler_ = std::move(onCommit);
    abortHandler_ = std::move(onAbort);
}

void TwoPCCoordinator::handleMessage(const Message& msg) {
    if (msg.type == MessageType::VOTE_YES || msg.type == MessageType::VOTE_NO) {
        std::unique_lock<std::mutex> mainLock(coordMutex_);
        auto it = coordState_.find(msg.txnId);
        if (it != coordState_.end()) {
            auto& txn = it->second;
            mainLock.unlock();
            
            std::lock_guard<std::mutex> txnLock(txn.mtx);
            txn.votes[msg.senderId] = (msg.type == MessageType::VOTE_YES);
            txn.cv.notify_all();
        }
    } else if (msg.type == MessageType::PREPARE) {
        bool vote = false;
        if (prepareHandler_) {
            vote = prepareHandler_(msg.txnId);
        }
        Message reply;
        reply.type = vote ? MessageType::VOTE_YES : MessageType::VOTE_NO;
        reply.senderId = nodeId_;
        reply.txnId = msg.txnId;
        network_.sendMessage(msg.senderId, reply);
    } else if (msg.type == MessageType::GLOBAL_COMMIT) {
        if (commitHandler_) {
            commitHandler_(msg.txnId);
        }
    } else if (msg.type == MessageType::GLOBAL_ABORT) {
        if (abortHandler_) {
            abortHandler_(msg.txnId);
        }
    }
}

TwoPCState TwoPCCoordinator::getTransactionState(TxnId txnId) const {
    std::lock_guard<std::mutex> lock(coordMutex_);
    auto it = coordState_.find(txnId);
    if (it != coordState_.end()) {
        std::lock_guard<std::mutex> txnLock(it->second.mtx);
        return it->second.state;
    }
    return TwoPCState::INIT;
}

void TwoPCCoordinator::logDecision(TxnId txnId, TwoPCState decision) {
    std::string stateStr;
    switch (decision) {
        case TwoPCState::COMMITTING: stateStr = "COMMITTING"; break;
        case TwoPCState::ABORTING: stateStr = "ABORTING"; break;
        case TwoPCState::COMMITTED: stateStr = "COMMITTED"; break;
        case TwoPCState::ABORTED: stateStr = "ABORTED"; break;
        default: stateStr = "UNKNOWN"; break;
    }
    Logger::info("TxnId: " + std::to_string(txnId) + " decision: " + stateStr);
}

} // namespace dlm

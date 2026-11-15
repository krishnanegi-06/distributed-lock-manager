#include "dlm/coordinator/TwoPCCoordinator.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/transaction/TransactionLog.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dlm {

TwoPCCoordinator::TwoPCCoordinator(NodeId nodeId,
                                   NetworkManager& network,
                                   std::chrono::milliseconds voteTimeout,
                                   TransactionLog* txnLog,
                                   const std::string& decisionLogPath)
    : nodeId_(nodeId),
      network_(network),
      voteTimeout_(voteTimeout),
      txnLog_(txnLog) {
    if (!decisionLogPath.empty()) {
        setDecisionLogPath(decisionLogPath);
    }
}

TwoPCCoordinator::~TwoPCCoordinator() {
    std::lock_guard<std::mutex> lock(decisionFileMutex_);
    decisionFile_.close();
}

void TwoPCCoordinator::setDecisionLogPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(decisionFileMutex_);
    decisionLogPath_ = path;
    if (!decisionLogPath_.empty()) {
        std::filesystem::path p(decisionLogPath_);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        loadDurableDecisions();
        decisionFile_.openAppend(decisionLogPath_);
    }
}

void TwoPCCoordinator::emitEvent(const std::string& type, TxnId txnId, const std::string& detail) {
    if (eventCb_) {
        try {
            eventCb_(type, txnId, detail);
        } catch (...) {}
    }
}

bool TwoPCCoordinator::persistDecision(TxnId txnId, TwoPCState decision,
                                       const std::vector<NodeId>& participants) {
    bool walOk = true;
    if (txnLog_) {
        if (decision == TwoPCState::COMMITTING || decision == TwoPCState::COMMITTED) {
            walOk = (txnLog_->logDecisionCommit(txnId, participants) != 0);
        } else {
            walOk = (txnLog_->logDecisionAbort(txnId, participants) != 0);
        }
    }

    bool fileOk = true;
    if (!decisionLogPath_.empty()) {
        std::lock_guard<std::mutex> lock(decisionFileMutex_);
        if (!decisionFile_.isOpen()) {
            decisionFile_.openAppend(decisionLogPath_);
        }
        nlohmann::json j;
        j["txnId"] = txnId;
        j["decision"] = to_string(decision);
        j["participants"] = participants;
        j["ts"] = nowEpochMs();
        std::string payload = j.dump();
        std::ostringstream line;
        line << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
             << crc32(payload) << "|" << payload;
        fileOk = decisionFile_.writeLine(line.str()) && decisionFile_.sync();
    }
    return walOk && fileOk;
}

void TwoPCCoordinator::loadDurableDecisions() {
    if (decisionLogPath_.empty()) return;
    std::ifstream f(decisionLogPath_, std::ios::binary);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() <= 9 || line[8] != '|') continue;
        uint32_t expected = 0;
        try {
            expected = static_cast<uint32_t>(std::stoul(line.substr(0, 8), nullptr, 16));
        } catch (...) {
            break;
        }
        std::string payload = line.substr(9);
        if (crc32(payload) != expected) break;
        try {
            auto j = nlohmann::json::parse(payload);
            TxnId tid = j.value("txnId", uint64_t{0});
            std::string dec = j.value("decision", std::string{});
            auto parts = j.value("participants", std::vector<NodeId>{});
            if (tid == 0) continue;

            std::lock_guard<std::mutex> lock(coordMutex_);
            auto& ptr = coordState_[tid];
            if (!ptr) ptr = std::make_shared<CoordTxn>();
            std::lock_guard<std::mutex> tl(ptr->mtx);
            ptr->role = TwoPCRole::COORDINATOR;
            ptr->coordinatorNode = nodeId_;
            ptr->participants = parts;
            ptr->decisionDurable = true;
            ptr->updatedAtMs = j.value("ts", nowEpochMs());
            if (dec == "COMMITTING" || dec == "COMMITTED") {
                ptr->state = (dec == "COMMITTED") ? TwoPCState::COMMITTED : TwoPCState::COMMITTING;
            } else if (dec == "ABORTING" || dec == "ABORTED") {
                ptr->state = (dec == "ABORTED") ? TwoPCState::ABORTED : TwoPCState::ABORTING;
            }
        } catch (...) {
            break;
        }
    }
}

bool TwoPCCoordinator::coordinateCommit(TxnId txnId, const std::vector<NodeId>& participants) {
    // Deduplicate participants
    std::vector<NodeId> uniqueParts = participants;
    std::sort(uniqueParts.begin(), uniqueParts.end());
    uniqueParts.erase(std::unique(uniqueParts.begin(), uniqueParts.end()), uniqueParts.end());

    std::shared_ptr<CoordTxn> txn;
    {
        std::lock_guard<std::mutex> mainLock(coordMutex_);
        auto& ptr = coordState_[txnId];
        if (!ptr) ptr = std::make_shared<CoordTxn>();
        txn = ptr;
    }

    {
        std::lock_guard<std::mutex> txnLock(txn->mtx);
        if (txn->state == TwoPCState::COMMITTED) return true;
        if (txn->state == TwoPCState::ABORTED) return false;
        txn->role = TwoPCRole::COORDINATOR;
        txn->coordinatorNode = nodeId_;
        txn->state = TwoPCState::PREPARING;
        txn->participants = uniqueParts;
        txn->votes.clear();
        txn->acks.clear();
        txn->updatedAtMs = nowEpochMs();
    }

    emitEvent("two_pc.prepare", txnId, "Preparing " + std::to_string(uniqueParts.size()) + " participants");

    // Phase 1: Send PREPARE to remote participants, invoke local prepare directly for self
    bool selfIncluded = false;
    for (NodeId p : uniqueParts) {
        if (p == nodeId_) {
            selfIncluded = true;
            continue;
        }
        Message msg;
        msg.type = MessageType::PREPARE;
        msg.senderId = nodeId_;
        msg.txnId = txnId;
        if (!network_.sendMessage(p, msg, std::chrono::milliseconds(2000))) {
            // Immediate send failure -> vote NO for unreachable participant
            std::lock_guard<std::mutex> txnLock(txn->mtx);
            txn->votes[p] = false;
            txn->cv.notify_all();
        }
    }

    if (selfIncluded) {
        bool selfVote = prepareHandler_ ? prepareHandler_(txnId) : true;
        std::lock_guard<std::mutex> txnLock(txn->mtx);
        txn->votes[nodeId_] = selfVote;
        txn->cv.notify_all();
        emitEvent("two_pc.vote", txnId,
                  "Node " + std::to_string(nodeId_) + (selfVote ? " voted YES" : " voted NO"));
    }

    std::unique_lock<std::mutex> txnLock(txn->mtx);
    bool allVoted = txn->cv.wait_for(txnLock, voteTimeout_, [&]() {
        return txn->votes.size() >= txn->participants.size();
    });

    bool commit = allVoted;
    if (commit) {
        for (NodeId p : txn->participants) {
            auto vit = txn->votes.find(p);
            if (vit == txn->votes.end() || !vit->second) {
                commit = false;
                break;
            }
        }
    }

    // CRITICAL: Persist durable decision BEFORE broadcasting Phase 2!
    if (commit) {
        txn->state = TwoPCState::COMMITTING;
        txn->updatedAtMs = nowEpochMs();
        if (!persistDecision(txnId, TwoPCState::COMMITTING, uniqueParts)) {
            Logger::error("TwoPCCoordinator: failed to persist COMMIT decision for txn " + std::to_string(txnId));
            commit = false;
        } else {
            txn->decisionDurable = true;
        }
    }

    if (!commit) {
        txn->state = TwoPCState::ABORTING;
        txn->updatedAtMs = nowEpochMs();
        txn->decisionDurable = persistDecision(txnId, TwoPCState::ABORTING, uniqueParts);
        txnLock.unlock();

        emitEvent("two_pc.abort", txnId, allVoted ? "Participant voted NO" : "Vote timeout");

        for (NodeId p : uniqueParts) {
            if (p == nodeId_) {
                if (abortHandler_) abortHandler_(txnId);
                std::lock_guard<std::mutex> lk(txn->mtx);
                txn->acks[nodeId_] = true;
            } else {
                Message abortMsg;
                abortMsg.type = MessageType::GLOBAL_ABORT;
                abortMsg.senderId = nodeId_;
                abortMsg.txnId = txnId;
                if (network_.sendMessage(p, abortMsg, std::chrono::milliseconds(2000))) {
                    std::lock_guard<std::mutex> lk(txn->mtx);
                    txn->acks[p] = true;
                }
            }
        }

        std::lock_guard<std::mutex> lk(txn->mtx);
        txn->state = TwoPCState::ABORTED;
        txn->updatedAtMs = nowEpochMs();
        persistDecision(txnId, TwoPCState::ABORTED, uniqueParts);
        return false;
    }

    txnLock.unlock();
    emitEvent("two_pc.commit", txnId, "Durable COMMIT decision logged; broadcasting GLOBAL_COMMIT");

    for (NodeId p : uniqueParts) {
        if (p == nodeId_) {
            if (commitHandler_) commitHandler_(txnId);
            std::lock_guard<std::mutex> lk(txn->mtx);
            txn->acks[nodeId_] = true;
        } else {
            Message commitMsg;
            commitMsg.type = MessageType::GLOBAL_COMMIT;
            commitMsg.senderId = nodeId_;
            commitMsg.txnId = txnId;
            if (network_.sendMessage(p, commitMsg, std::chrono::milliseconds(2000))) {
                std::lock_guard<std::mutex> lk(txn->mtx);
                txn->acks[p] = true;
            }
        }
    }

    std::lock_guard<std::mutex> lk(txn->mtx);
    txn->state = TwoPCState::COMMITTED;
    txn->updatedAtMs = nowEpochMs();
    persistDecision(txnId, TwoPCState::COMMITTED, uniqueParts);
    return true;
}

void TwoPCCoordinator::coordinateAbort(TxnId txnId, const std::vector<NodeId>& participants) {
    std::vector<NodeId> uniqueParts = participants;
    std::sort(uniqueParts.begin(), uniqueParts.end());
    uniqueParts.erase(std::unique(uniqueParts.begin(), uniqueParts.end()), uniqueParts.end());

    persistDecision(txnId, TwoPCState::ABORTED, uniqueParts);
    emitEvent("two_pc.abort", txnId, "Coordinator initiated GLOBAL_ABORT");

    for (NodeId p : uniqueParts) {
        if (p == nodeId_) {
            if (abortHandler_) abortHandler_(txnId);
        } else {
            Message abortMsg;
            abortMsg.type = MessageType::GLOBAL_ABORT;
            abortMsg.senderId = nodeId_;
            abortMsg.txnId = txnId;
            network_.sendMessage(p, abortMsg, std::chrono::milliseconds(2000));
        }
    }
}

void TwoPCCoordinator::recoverCoordinatorDecisions() {
    std::vector<std::pair<TxnId, std::shared_ptr<CoordTxn>>> pending;
    {
        std::lock_guard<std::mutex> lock(coordMutex_);
        for (const auto& [tid, ptr] : coordState_) {
            std::lock_guard<std::mutex> tl(ptr->mtx);
            if (ptr->role == TwoPCRole::COORDINATOR &&
                (ptr->state == TwoPCState::COMMITTING || ptr->state == TwoPCState::ABORTING)) {
                pending.emplace_back(tid, ptr);
            }
        }
    }

    for (auto& [tid, ptr] : pending) {
        std::vector<NodeId> parts;
        bool isCommit = false;
        {
            std::lock_guard<std::mutex> tl(ptr->mtx);
            parts = ptr->participants;
            isCommit = (ptr->state == TwoPCState::COMMITTING);
        }
        for (NodeId p : parts) {
            if (p == nodeId_) {
                if (isCommit && commitHandler_) commitHandler_(tid);
                if (!isCommit && abortHandler_) abortHandler_(tid);
            } else {
                Message m;
                m.type = isCommit ? MessageType::GLOBAL_COMMIT : MessageType::GLOBAL_ABORT;
                m.senderId = nodeId_;
                m.txnId = tid;
                network_.sendMessage(p, m, std::chrono::milliseconds(2000));
            }
        }
        std::lock_guard<std::mutex> tl(ptr->mtx);
        ptr->state = isCommit ? TwoPCState::COMMITTED : TwoPCState::ABORTED;
        ptr->updatedAtMs = nowEpochMs();
        persistDecision(tid, ptr->state, parts);
    }
}

bool TwoPCCoordinator::queryCoordinatorStatus(TxnId txnId, NodeId coordinatorNode) {
    if (coordinatorNode == 0 || coordinatorNode == nodeId_) return false;
    Message req;
    req.type = MessageType::TXN_STATUS_REQ;
    req.senderId = nodeId_;
    req.txnId = txnId;
    Message resp;
    if (!network_.sendRequest(coordinatorNode, req, resp, std::chrono::milliseconds(2000))) {
        return false;
    }
    if (resp.type != MessageType::TXN_STATUS_RESP) return false;
    if (resp.value == "COMMITTED" || resp.value == "COMMITTING") {
        if (commitHandler_) commitHandler_(txnId);
        return true;
    }
    if (resp.value == "ABORTED" || resp.value == "ABORTING") {
        if (abortHandler_) abortHandler_(txnId);
        return true;
    }
    return false;
}

void TwoPCCoordinator::registerParticipantHandlers(PrepareHandler onPrepare,
                                                   CommitHandler  onCommit,
                                                   AbortHandler   onAbort) {
    prepareHandler_ = std::move(onPrepare);
    commitHandler_  = std::move(onCommit);
    abortHandler_   = std::move(onAbort);
}

void TwoPCCoordinator::handleMessage(const Message& msg) {
    if (msg.type == MessageType::VOTE_YES || msg.type == MessageType::VOTE_NO) {
        std::shared_ptr<CoordTxn> txn;
        {
            std::lock_guard<std::mutex> mainLock(coordMutex_);
            auto it = coordState_.find(msg.txnId);
            if (it != coordState_.end()) txn = it->second;
        }
        if (txn) {
            bool yes = (msg.type == MessageType::VOTE_YES);
            {
                std::lock_guard<std::mutex> txnLock(txn->mtx);
                txn->votes[msg.senderId] = yes;
                txn->updatedAtMs = nowEpochMs();
                txn->cv.notify_all();
            }
            emitEvent("two_pc.vote", msg.txnId,
                      "Node " + std::to_string(msg.senderId) + (yes ? " voted YES" : " voted NO"));
        }
    } else if (msg.type == MessageType::PREPARE) {
        bool vote = false;
        if (prepareHandler_) {
            vote = prepareHandler_(msg.txnId);
        }
        {
            std::lock_guard<std::mutex> mainLock(coordMutex_);
            auto& ptr = coordState_[msg.txnId];
            if (!ptr) ptr = std::make_shared<CoordTxn>();
            std::lock_guard<std::mutex> tl(ptr->mtx);
            ptr->role = TwoPCRole::PARTICIPANT;
            ptr->coordinatorNode = msg.senderId;
            ptr->state = vote ? TwoPCState::PREPARED : TwoPCState::ABORTED;
            ptr->updatedAtMs = nowEpochMs();
        }
        Message reply;
        reply.type = vote ? MessageType::VOTE_YES : MessageType::VOTE_NO;
        reply.senderId = nodeId_;
        reply.txnId = msg.txnId;
        network_.sendMessage(msg.senderId, reply, std::chrono::milliseconds(2000));
    } else if (msg.type == MessageType::GLOBAL_COMMIT) {
        if (commitHandler_) {
            commitHandler_(msg.txnId);
        }
        {
            std::lock_guard<std::mutex> mainLock(coordMutex_);
            auto& ptr = coordState_[msg.txnId];
            if (!ptr) ptr = std::make_shared<CoordTxn>();
            std::lock_guard<std::mutex> tl(ptr->mtx);
            ptr->role = TwoPCRole::PARTICIPANT;
            ptr->coordinatorNode = msg.senderId;
            ptr->state = TwoPCState::COMMITTED;
            ptr->updatedAtMs = nowEpochMs();
        }
        Message ack;
        ack.type = MessageType::COMMIT_ACK;
        ack.senderId = nodeId_;
        ack.txnId = msg.txnId;
        network_.sendMessage(msg.senderId, ack, std::chrono::milliseconds(1000));
    } else if (msg.type == MessageType::GLOBAL_ABORT) {
        if (abortHandler_) {
            abortHandler_(msg.txnId);
        }
        {
            std::lock_guard<std::mutex> mainLock(coordMutex_);
            auto& ptr = coordState_[msg.txnId];
            if (!ptr) ptr = std::make_shared<CoordTxn>();
            std::lock_guard<std::mutex> tl(ptr->mtx);
            ptr->role = TwoPCRole::PARTICIPANT;
            ptr->coordinatorNode = msg.senderId;
            ptr->state = TwoPCState::ABORTED;
            ptr->updatedAtMs = nowEpochMs();
        }
        Message ack;
        ack.type = MessageType::ABORT_ACK;
        ack.senderId = nodeId_;
        ack.txnId = msg.txnId;
        network_.sendMessage(msg.senderId, ack, std::chrono::milliseconds(1000));
    } else if (msg.type == MessageType::COMMIT_ACK || msg.type == MessageType::ABORT_ACK) {
        std::shared_ptr<CoordTxn> txn;
        {
            std::lock_guard<std::mutex> mainLock(coordMutex_);
            auto it = coordState_.find(msg.txnId);
            if (it != coordState_.end()) txn = it->second;
        }
        if (txn) {
            std::lock_guard<std::mutex> tl(txn->mtx);
            txn->acks[msg.senderId] = true;
            txn->updatedAtMs = nowEpochMs();
        }
    } else if (msg.type == MessageType::TXN_STATUS_REQ) {
        TwoPCState st = getTransactionState(msg.txnId);
        // If coordinator has no durable record of this txn, conservative 2PC recovery aborts it
        std::string statusStr = (st == TwoPCState::INIT) ? "ABORTED" : to_string(st);
        Message resp;
        resp.type = MessageType::TXN_STATUS_RESP;
        resp.senderId = nodeId_;
        resp.requestId = msg.requestId;
        resp.txnId = msg.txnId;
        resp.value = statusStr;
        resp.success = true;
        network_.sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
    }
}

TwoPCState TwoPCCoordinator::getTransactionState(TxnId txnId) const {
    std::lock_guard<std::mutex> lock(coordMutex_);
    auto it = coordState_.find(txnId);
    if (it != coordState_.end()) {
        std::lock_guard<std::mutex> txnLock(it->second->mtx);
        return it->second->state;
    }
    return TwoPCState::INIT;
}

std::vector<TwoPCTransactionSnapshot> TwoPCCoordinator::getSnapshot() const {
    std::lock_guard<std::mutex> lock(coordMutex_);
    std::vector<TwoPCTransactionSnapshot> out;
    out.reserve(coordState_.size());
    for (const auto& [tid, ptr] : coordState_) {
        std::lock_guard<std::mutex> tl(ptr->mtx);
        TwoPCTransactionSnapshot snap;
        snap.txnId = tid;
        snap.role = ptr->role;
        snap.state = ptr->state;
        snap.coordinatorNode = ptr->coordinatorNode;
        snap.participants = ptr->participants;
        snap.votes = ptr->votes;
        snap.acks = ptr->acks;
        snap.decisionDurable = ptr->decisionDurable;
        snap.updatedAtMs = ptr->updatedAtMs;
        out.push_back(std::move(snap));
    }
    std::sort(out.begin(), out.end(), [](const TwoPCTransactionSnapshot& a, const TwoPCTransactionSnapshot& b) {
        return a.updatedAtMs > b.updatedAtMs || (a.updatedAtMs == b.updatedAtMs && a.txnId > b.txnId);
    });
    return out;
}

} // namespace dlm

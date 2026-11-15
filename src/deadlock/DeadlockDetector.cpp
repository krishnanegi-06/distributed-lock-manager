#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <algorithm>
#include <set>

namespace dlm {

DeadlockDetector::DeadlockDetector() = default;

DeadlockDetector::~DeadlockDetector() {
    stopPeriodicDetection();
}

std::vector<TxnId> DeadlockDetector::detectCycles(const std::vector<WaitEdge>& edges) const {
    auto detailed = detectCyclesDetailed(edges);
    std::vector<TxnId> victims;
    victims.reserve(detailed.size());
    for (const auto& dc : detailed) {
        if (dc.victim != 0) victims.push_back(dc.victim);
    }
    std::sort(victims.begin(), victims.end());
    victims.erase(std::unique(victims.begin(), victims.end()), victims.end());
    return victims;
}

std::vector<DeadlockCycle> DeadlockDetector::detectCyclesDetailed(
        const std::vector<WaitEdge>& edges) const {
    std::unordered_map<TxnId, std::vector<WaitEdge>> adj;
    for (const auto& edge : edges) {
        if (edge.waitingTxn == edge.holdingTxn) continue;
        adj[edge.waitingTxn].push_back(edge);
    }
    return findCyclesDetailed(adj);
}

std::vector<DeadlockCycle> DeadlockDetector::findCyclesDetailed(
        const std::unordered_map<TxnId, std::vector<WaitEdge>>& adj) const {
    std::vector<DeadlockCycle> results;

    enum Color { WHITE, GRAY, BLACK };
    std::unordered_map<TxnId, Color> color;
    std::vector<TxnId> sortedNodes;
    std::unordered_set<TxnId> nodeSet;

    for (const auto& [u, edgeList] : adj) {
        if (nodeSet.insert(u).second) sortedNodes.push_back(u);
        color[u] = WHITE;
        for (const auto& e : edgeList) {
            if (nodeSet.insert(e.holdingTxn).second) sortedNodes.push_back(e.holdingTxn);
            color[e.holdingTxn] = WHITE;
        }
    }
    std::sort(sortedNodes.begin(), sortedNodes.end());

    std::unordered_map<TxnId, TxnId> parent;
    std::unordered_map<TxnId, WaitEdge> parentEdge;
    std::set<std::vector<TxnId>> seenCycles;

    std::function<void(TxnId)> dfs = [&](TxnId u) {
        color[u] = GRAY;
        auto it = adj.find(u);
        if (it != adj.end()) {
            for (const auto& e : it->second) {
                TxnId v = e.holdingTxn;
                if (color[v] == WHITE) {
                    parent[v] = u;
                    parentEdge[v] = e;
                    dfs(v);
                } else if (color[v] == GRAY) {
                    std::vector<TxnId> revCycle;
                    std::vector<WaitEdge> cycleEdges;
                    cycleEdges.push_back(e);
                    TxnId curr = u;
                    bool closed = false;
                    size_t guard = 0;
                    while (guard++ <= sortedNodes.size()) {
                        revCycle.push_back(curr);
                        if (curr == v) {
                            closed = true;
                            break;
                        }
                        auto pit = parent.find(curr);
                        if (pit == parent.end()) break;
                        auto peIt = parentEdge.find(curr);
                        if (peIt != parentEdge.end()) {
                            cycleEdges.push_back(peIt->second);
                        }
                        curr = pit->second;
                    }
                    if (closed) {
                        std::reverse(revCycle.begin(), revCycle.end());
                        std::reverse(cycleEdges.begin(), cycleEdges.end());
                        // Canonicalize cycle rotation to deduplicate
                        auto minIt = std::min_element(revCycle.begin(), revCycle.end());
                        std::vector<TxnId> canonical;
                        canonical.insert(canonical.end(), minIt, revCycle.end());
                        canonical.insert(canonical.end(), revCycle.begin(), minIt);

                        if (seenCycles.insert(canonical).second) {
                            DeadlockCycle dc;
                            dc.victim = selectVictim(revCycle);
                            revCycle.push_back(revCycle.front());
                            dc.cycle = std::move(revCycle);
                            dc.edges = std::move(cycleEdges);
                            dc.detectedAtMs = nowEpochMs();
                            dc.resolved = false;
                            results.push_back(std::move(dc));
                        }
                    }
                }
            }
        }
        color[u] = BLACK;
    };

    for (TxnId u : sortedNodes) {
        if (color[u] == WHITE) {
            dfs(u);
        }
    }

    return results;
}

TxnId DeadlockDetector::selectVictim(const std::vector<TxnId>& cycle) {
    if (cycle.empty()) return 0;
    TxnId victim = cycle[0];
    uint64_t bestSeq = txnSequence(victim);
    for (TxnId t : cycle) {
        uint64_t seq = txnSequence(t);
        // Prefer youngest (highest sequence), break ties by full TxnId
        if (seq > bestSeq || (seq == bestSeq && t > victim)) {
            victim = t;
            bestSeq = seq;
        }
    }
    return victim;
}

void DeadlockDetector::startPeriodicDetection(
        std::chrono::milliseconds interval,
        std::function<std::vector<WaitEdge>()> getLocalEdges,
        std::function<std::vector<WaitEdge>()> getGlobalEdges,
        AbortCallback onAbort,
        DetectedCallback onDetected) {
    stopPeriodicDetection();
    running_.store(true);

    thread_ = std::thread([this, interval, getLocalEdges, getGlobalEdges, onAbort, onDetected]() {
        while (running_.load()) {
            {
                std::unique_lock<std::mutex> lk(sleepMutex_);
                sleepCv_.wait_for(lk, interval, [this]() { return !running_.load(); });
            }
            if (!running_.load()) break;

            std::unordered_set<TxnId> abortedThisRound;

            auto processCycles = [&](const std::vector<WaitEdge>& edges) {
                if (edges.empty()) return;
                auto cycles = detectCyclesDetailed(edges);
                for (auto& dc : cycles) {
                    if (dc.victim == 0) continue;
                    totalDetected_.fetch_add(1);
                    if (abortedThisRound.insert(dc.victim).second) {
                        dc.resolved = true;
                        {
                            std::lock_guard<std::mutex> hk(historyMutex_);
                            history_.push_back(dc);
                            if (history_.size() > 200) {
                                history_.erase(history_.begin(), history_.begin() + (history_.size() - 200));
                            }
                        }
                        if (onDetected) onDetected(dc);
                        if (onAbort) onAbort(dc.victim);
                    }
                }
            };

            if (getLocalEdges) {
                processCycles(getLocalEdges());
            }
            if (getGlobalEdges && running_.load()) {
                processCycles(getGlobalEdges());
            }
        }
    });
}

void DeadlockDetector::stopPeriodicDetection() {
    running_.store(false);
    sleepCv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::vector<DeadlockCycle> DeadlockDetector::getHistory() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return history_;
}

} // namespace dlm

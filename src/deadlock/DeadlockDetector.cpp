#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/common/Logger.h"
#include <algorithm>

namespace dlm {

DeadlockDetector::DeadlockDetector() = default;

DeadlockDetector::~DeadlockDetector() {
    stopPeriodicDetection();
}

std::vector<TxnId> DeadlockDetector::detectCycles(const std::vector<WaitEdge>& edges) const {
    std::unordered_map<TxnId, std::vector<TxnId>> adj;
    for (const auto& edge : edges) {
        adj[edge.waitingTxn].push_back(edge.holdingTxn);
    }
    return findCycles(adj);
}

std::vector<TxnId> DeadlockDetector::findCycles(const std::unordered_map<TxnId, std::vector<TxnId>>& adj) const {
    std::vector<TxnId> victims;
    
    enum Color { WHITE, GRAY, BLACK };
    std::unordered_map<TxnId, Color> color;
    
    // Initialize colors
    for (const auto& pair : adj) {
        color[pair.first] = WHITE;
        for (auto to : pair.second) {
            color[to] = WHITE;
        }
    }

    std::unordered_map<TxnId, TxnId> parent;
    
    // std::function for recursive DFS
    std::function<void(TxnId)> dfs = [&](TxnId u) {
        color[u] = GRAY;
        if (adj.count(u)) {
            for (TxnId v : adj.at(u)) {
                if (color[v] == WHITE) {
                    parent[v] = u;
                    dfs(v);
                } else if (color[v] == GRAY) {
                    // Cycle found!
                    std::vector<TxnId> cycle;
                    cycle.push_back(v);
                    TxnId curr = u;
                    // backtrack through parents until we reach 'v' again
                    while (curr != v && curr != 0) {
                        cycle.push_back(curr);
                        if (parent.count(curr) == 0) break; // defensive
                        curr = parent[curr];
                    }
                    if (curr == v) { // completed loop
                        victims.push_back(selectVictim(cycle));
                    }
                }
            }
        }
        color[u] = BLACK;
    };
    
    for (const auto& pair : adj) {
        if (color[pair.first] == WHITE) {
            dfs(pair.first);
        }
    }
    
    // De-duplicate victims
    std::sort(victims.begin(), victims.end());
    victims.erase(std::unique(victims.begin(), victims.end()), victims.end());
    
    return victims;
}

TxnId DeadlockDetector::selectVictim(const std::vector<TxnId>& cycle) {
    if (cycle.empty()) return 0;
    TxnId victim = cycle[0];
    for (TxnId t : cycle) {
        if (t > victim) {
            victim = t;
        }
    }
    return victim;
}

void DeadlockDetector::startPeriodicDetection(
        std::chrono::milliseconds interval,
        std::function<std::vector<WaitEdge>()> getLocalEdges,
        std::function<std::vector<WaitEdge>()> getGlobalEdges,
        AbortCallback onAbort) {
    
    stopPeriodicDetection();
    running_ = true;
    
    thread_ = std::thread([this, interval, getLocalEdges, getGlobalEdges, onAbort]() {
        while (running_) {
            std::this_thread::sleep_for(interval);
            if (!running_) break;
            
            // 1. Local cycle detection
            if (getLocalEdges) {
                auto localEdges = getLocalEdges();
                auto victims = detectCycles(localEdges);
                for (auto v : victims) {
                    if (onAbort) onAbort(v);
                }
            }
            
            // 2. Global cycle detection
            if (getGlobalEdges) {
                auto globalEdges = getGlobalEdges();
                auto victims = detectCycles(globalEdges);
                for (auto v : victims) {
                    if (onAbort) onAbort(v);
                }
            }
        }
    });
}

void DeadlockDetector::stopPeriodicDetection() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

} // namespace dlm

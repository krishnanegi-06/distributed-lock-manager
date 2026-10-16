
#include "dlm/deadlock/deadlock_detector.h"

#include <algorithm>
#include <utility>

namespace dlm {

DeadlockDetector::DeadlockDetector(AbortCallback callback)
    : abortCallback_(std::move(callback)) {}

// Add an edge: waiter -> holder
void DeadlockDetector::addDependency(int waiter, int holder) {
    std::lock_guard<std::mutex> lock(mutex_);
    graph_[waiter].insert(holder);
    graph_.try_emplace(holder);
}

// Remove one waiting relationship
void DeadlockDetector::removeDependency(int waiter, int holder) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = graph_.find(waiter);
    if (it == graph_.end())
        return;

    it->second.erase(holder);

    if (it->second.empty())
        graph_.erase(it);
}

// Remove a completed or aborted transaction
void DeadlockDetector::removeTransaction(int transactionId) {
    std::lock_guard<std::mutex> lock(mutex_);

    graph_.erase(transactionId);

    for (auto it = graph_.begin(); it != graph_.end();) {
        it->second.erase(transactionId);

        if (it->second.empty())
            it = graph_.erase(it);
        else
            ++it;
    }
}

// DFS detects a back edge, which indicates a cycle
bool DeadlockDetector::dfs(
    int node,
    std::unordered_map<int, int>& color,
    std::vector<int>& path,
    std::vector<int>& cycle
) const {
    color[node] = 1; // Currently visiting
    path.push_back(node);

    auto it = graph_.find(node);

    if (it != graph_.end()) {
        for (int neighbor : it->second) {
            if (color[neighbor] == 0) {
                if (dfs(neighbor, color, path, cycle))
                    return true;
            }
            else if (color[neighbor] == 1) {
                auto start = std::find(
                    path.begin(), path.end(), neighbor
                );

                cycle.assign(start, path.end());
                cycle.push_back(neighbor);
                return true;
            }
        }
    }

    path.pop_back();
    color[node] = 2; // Completely visited
    return false;
}

// Find one cycle in the Wait-For Graph
std::vector<int> DeadlockDetector::findCycle() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<int, int> color;
    std::vector<int> path;
    std::vector<int> cycle;

    for (const auto& entry : graph_) {
        color[entry.first] = 0;

        for (int neighbor : entry.second)
            color[neighbor] = 0;
    }

    for (const auto& entry : graph_) {
        if (color[entry.first] == 0 &&
            dfs(entry.first, color, path, cycle)) {
            return cycle;
        }
    }

    return {};
}

// Detect a cycle and request an abort
bool DeadlockDetector::detectAndResolve() {
    std::vector<int> cycle = findCycle();

    if (cycle.empty())
        return false;

    // Simple victim-selection policy
    int victim = cycle.front();

    if (abortCallback_)
        abortCallback_(victim);

    return true;
}

} // namespace dlm

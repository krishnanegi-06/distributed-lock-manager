
#pragma once

#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dlm {

class DeadlockDetector {
public:
    using AbortCallback = std::function<void(int)>;

    explicit DeadlockDetector(AbortCallback callback = {});

    // waiter is waiting for holder.
    void addDependency(int waiter, int holder);

    // Remove one waiting relationship.
    void removeDependency(int waiter, int holder);

    // Remove a completed or aborted transaction.
    void removeTransaction(int transactionId);

    // Return one detected cycle, or an empty vector.
    std::vector<int> findCycle() const;

    // Detect a cycle and request abort of a selected victim.
    bool detectAndResolve();

private:
    using Graph =
        std::unordered_map<int, std::unordered_set<int>>;

    mutable std::mutex mutex_;
    Graph graph_;
    AbortCallback abortCallback_;

    bool dfs(
        int node,
        std::unordered_map<int, int>& color,
        std::vector<int>& path,
        std::vector<int>& cycle
    ) const;
};

} // namespace dlm

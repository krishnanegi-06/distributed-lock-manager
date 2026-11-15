#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "dlm/common/Hash.h"
#include "dlm/common/Types.h"

namespace dlm {

/// Deterministic hash-based sharding router using FNV-1a 64-bit.
/// Maps any key deterministically to an owning NodeId without replication:
///   owner_node = sortedNodes[ fnv1a64(key) % sortedNodes.size() ]
class ShardRouter {
public:
    ShardRouter() = default;
    explicit ShardRouter(std::vector<NodeId> clusterNodes) {
        setNodes(std::move(clusterNodes));
    }

    void setNodes(std::vector<NodeId> clusterNodes) {
        std::sort(clusterNodes.begin(), clusterNodes.end());
        clusterNodes.erase(std::unique(clusterNodes.begin(), clusterNodes.end()),
                           clusterNodes.end());
        nodes_ = std::move(clusterNodes);
    }

    const std::vector<NodeId>& getNodes() const { return nodes_; }

    /// Returns 0 if no cluster nodes are configured.
    NodeId getOwnerNode(const std::string& key) const {
        if (nodes_.empty()) return 0;
        uint64_t h = fnv1a64(key);
        return nodes_[static_cast<size_t>(h % nodes_.size())];
    }

    uint64_t getKeyHash(const std::string& key) const {
        return fnv1a64(key);
    }

    bool isLocalKey(NodeId localNodeId, const std::string& key) const {
        return getOwnerNode(key) == localNodeId;
    }

private:
    std::vector<NodeId> nodes_;
};

} // namespace dlm

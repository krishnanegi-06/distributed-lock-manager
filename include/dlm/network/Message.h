#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "dlm/common/Types.h"
#include "dlm/lock_manager/LockManager.h"

namespace dlm {

// --------------- message types ---------------
enum class MessageType : uint8_t {
    // Lock operations
    LOCK_REQUEST       = 0x01,
    LOCK_RESPONSE      = 0x02,
    LOCK_RELEASE       = 0x03,
    LOCK_RELEASE_ALL   = 0x04,

    // 2PC protocol
    PREPARE            = 0x10,
    VOTE_YES           = 0x11,
    VOTE_NO            = 0x12,
    GLOBAL_COMMIT      = 0x13,
    GLOBAL_ABORT       = 0x14,

    // Deadlock detection
    WFG_REQUEST        = 0x20,
    WFG_RESPONSE       = 0x21,
    ABORT_TXN          = 0x22,

    // Heartbeat
    HEARTBEAT          = 0x30,
    HEARTBEAT_ACK      = 0x31,

    // Node management
    NODE_JOIN          = 0x40,
    NODE_LEAVE         = 0x41,
};

// --------------- wire message ---------------
struct Message {
    MessageType          type       = MessageType::HEARTBEAT;
    NodeId               senderId   = 0;
    TxnId                txnId      = 0;
    ResourceId           resourceId;
    LockMode             lockMode   = LockMode::SHARED;
    bool                 success    = false;
    std::vector<WaitEdge>  waitEdges;
    std::vector<NodeId>    participants;

    /// Serialize to a length-prefixed binary blob.
    std::string serialize() const;

    /// Deserialize from a binary blob (without the length prefix).
    static Message deserialize(const std::string& data);
};

} // namespace dlm

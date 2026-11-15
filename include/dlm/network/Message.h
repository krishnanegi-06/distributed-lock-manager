#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
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
    PREPARE_ACK        = 0x15,
    COMMIT_ACK         = 0x16,
    ABORT_ACK          = 0x17,
    TXN_STATUS_REQ     = 0x18,
    TXN_STATUS_RESP    = 0x19,

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

    // Distributed KV storage operations (remote shard access)
    KV_GET_REQ         = 0x50,
    KV_GET_RESP        = 0x51,
    KV_PUT_REQ         = 0x52,
    KV_PUT_RESP        = 0x53,
    KV_DELETE_REQ      = 0x54,
    KV_DELETE_RESP     = 0x55,
    KV_SCAN_REQ        = 0x56,
    KV_SCAN_RESP       = 0x57,

    // Control / Observability RPC (used by API gateway)
    CTRL_RPC_REQ       = 0x60,
    CTRL_RPC_RESP      = 0x61
};

/// Staged write operation transmitted during 2PC PREPARE or remote PUT/DELETE.
struct StagedWriteItem {
    std::string key;
    bool        isDelete = false;
    std::string value;
};

// --------------- wire message ---------------
struct Message {
    static constexpr uint32_t kMaxSerializedBytes = 16U * 1024U * 1024U; // 16 MiB

    MessageType           type       = MessageType::HEARTBEAT;
    NodeId                senderId   = 0;
    uint64_t              requestId  = 0;
    TxnId                 txnId      = 0;
    ResourceId            resourceId;
    LockMode              lockMode   = LockMode::SHARED;
    bool                  success    = false;
    OpStatus              status     = OpStatus::OK;
    bool                  found      = false;
    std::string           key;
    std::string           value;
    std::string           error;
    std::vector<WaitEdge>                  waitEdges;
    std::vector<NodeId>                    participants;
    std::vector<StagedWriteItem>           stagedWrites;
    std::vector<std::pair<std::string, std::string>> kvPairs;

    /// Serialize to a binary payload (without outer TCP 4-byte length prefix).
    std::string serialize() const;

    /// Deserialize safely; returns false if malformed or truncated.
    static bool deserialize(const std::string& data, Message& out);

    /// Convenience wrapper; returns default Message on parse failure.
    static Message deserialize(const std::string& data);
};

} // namespace dlm

#pragma once
#include <cstdint>
#include <string>

namespace dlm {

// --------------- fundamental type aliases ---------------
using NodeId     = uint32_t;
using TxnId      = uint64_t;      // Globally unique: (NodeId << 48) | seq
using ResourceId = std::string;   // Key / resource identifier

// --------------- globally unique TxnId helpers ---------------
constexpr uint64_t kTxnSeqMask = 0x0000FFFFFFFFFFFFULL;

inline TxnId makeTxnId(NodeId originNode, uint64_t seq) {
    return (static_cast<uint64_t>(originNode & 0xFFFFu) << 48) | (seq & kTxnSeqMask);
}

inline NodeId txnOriginNode(TxnId id) {
    return static_cast<NodeId>((id >> 48) & 0xFFFFu);
}

inline uint64_t txnSequence(TxnId id) {
    return id & kTxnSeqMask;
}

// --------------- lock modes ---------------
enum class LockMode : uint8_t {
    SHARED,       // multiple readers
    EXCLUSIVE     // single writer
};

// --------------- lock granularity ---------------
enum class LockGranularity : uint8_t {
    ROW,
    TABLE
};

// --------------- transaction states ---------------
enum class TxnState : uint8_t {
    ACTIVE,
    WAITING,      // blocked waiting for a lock
    PREPARING,    // 2PC Phase-1 in progress
    PREPARED,     // 2PC Phase-1 voted YES, waiting for decision
    COMMITTED,
    ABORTED
};

inline bool isTerminalState(TxnState s) {
    return s == TxnState::COMMITTED || s == TxnState::ABORTED;
}

inline bool isValidTxnTransition(TxnState from, TxnState to) {
    if (from == to) return false;
    switch (from) {
        case TxnState::ACTIVE:
            return to == TxnState::WAITING || to == TxnState::PREPARING ||
                   to == TxnState::PREPARED || to == TxnState::COMMITTED ||
                   to == TxnState::ABORTED;
        case TxnState::WAITING:
            return to == TxnState::ACTIVE || to == TxnState::ABORTED;
        case TxnState::PREPARING:
            return to == TxnState::PREPARED || to == TxnState::COMMITTED ||
                   to == TxnState::ABORTED;
        case TxnState::PREPARED:
            return to == TxnState::COMMITTED || to == TxnState::ABORTED;
        case TxnState::COMMITTED:
        case TxnState::ABORTED:
            return false;
    }
    return false;
}

// --------------- isolation levels ---------------
enum class IsolationLevel : uint8_t {
    READ_COMMITTED,   // release read-locks immediately after read
    SERIALIZABLE      // hold all locks until commit/abort (Strict 2PL)
};

// --------------- node health states ---------------
enum class NodeHealthState : uint8_t {
    HEALTHY,
    SUSPECTED,
    FAILED,
    SHUTDOWN
};

// --------------- operation outcome status ---------------
enum class OpStatus : uint8_t {
    OK,
    NOT_FOUND,
    LOCK_TIMEOUT,
    DEADLOCK_ABORT,
    NODE_UNAVAILABLE,
    IN_DOUBT,
    INVALID_STATE,
    INVALID_ARGUMENT,
    IO_ERROR,
    INTERNAL_ERROR
};

// --------------- helpers ---------------
inline const char* to_string(LockMode m) {
    return m == LockMode::SHARED ? "SHARED" : "EXCLUSIVE";
}

inline const char* to_string(TxnState s) {
    switch (s) {
        case TxnState::ACTIVE:    return "ACTIVE";
        case TxnState::WAITING:   return "WAITING";
        case TxnState::PREPARING: return "PREPARING";
        case TxnState::PREPARED:  return "PREPARED";
        case TxnState::COMMITTED: return "COMMITTED";
        case TxnState::ABORTED:   return "ABORTED";
    }
    return "UNKNOWN";
}

inline const char* to_string(NodeHealthState h) {
    switch (h) {
        case NodeHealthState::HEALTHY:   return "HEALTHY";
        case NodeHealthState::SUSPECTED: return "SUSPECTED";
        case NodeHealthState::FAILED:    return "FAILED";
        case NodeHealthState::SHUTDOWN:  return "SHUTDOWN";
    }
    return "UNKNOWN";
}

inline const char* to_string(OpStatus st) {
    switch (st) {
        case OpStatus::OK:               return "OK";
        case OpStatus::NOT_FOUND:        return "NOT_FOUND";
        case OpStatus::LOCK_TIMEOUT:     return "LOCK_TIMEOUT";
        case OpStatus::DEADLOCK_ABORT:   return "DEADLOCK_ABORT";
        case OpStatus::NODE_UNAVAILABLE: return "NODE_UNAVAILABLE";
        case OpStatus::IN_DOUBT:         return "IN_DOUBT";
        case OpStatus::INVALID_STATE:    return "INVALID_STATE";
        case OpStatus::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case OpStatus::IO_ERROR:         return "IO_ERROR";
        case OpStatus::INTERNAL_ERROR:   return "INTERNAL_ERROR";
    }
    return "UNKNOWN";
}

} // namespace dlm

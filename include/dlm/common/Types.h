#pragma once
#include <cstdint>
#include <string>

namespace dlm {

// --------------- fundamental type aliases ---------------
using NodeId     = uint32_t;
using TxnId      = uint32_t;
using ResourceId = std::string;   // e.g. "table:orders" or "row:orders:42"

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
    PREPARING,    // 2PC Phase-1 in progress
    COMMITTED,
    ABORTED
};

// --------------- isolation levels ---------------
enum class IsolationLevel : uint8_t {
    READ_COMMITTED,   // release read-locks immediately after read
    SERIALIZABLE      // hold all locks until commit/abort
};

// --------------- helpers ---------------
inline const char* to_string(LockMode m) {
    return m == LockMode::SHARED ? "SHARED" : "EXCLUSIVE";
}
inline const char* to_string(TxnState s) {
    switch (s) {
        case TxnState::ACTIVE:    return "ACTIVE";
        case TxnState::PREPARING: return "PREPARING";
        case TxnState::COMMITTED: return "COMMITTED";
        case TxnState::ABORTED:   return "ABORTED";
    }
    return "UNKNOWN";
}

} // namespace dlm

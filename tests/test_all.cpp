// ---------------------------------------------------------------------------
// Distributed Lock Manager – Unit & Integration Tests
// Simple assertion-based tests (no external framework dependency).
// ---------------------------------------------------------------------------
#include "dlm/lock_manager/LockManager.h"
#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/transaction/Transaction.h"
#include "dlm/transaction/TransactionLog.h"
#include "dlm/transaction/TransactionManager.h"
#include "dlm/network/Message.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Types.h"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <string>
#include <atomic>

using namespace dlm;
using namespace std::chrono_literals;

static int passed = 0;
static int failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct Register_##name { Register_##name() { tests.push_back({#name, test_##name}); } } reg_##name; \
    static void test_##name()

// Simple test runner
struct TestEntry { const char* name; void(*fn)(); };
static std::vector<TestEntry> tests;

#define ASSERT_TRUE(expr) \
    do { if (!(expr)) { std::cerr << "  FAIL: " #expr " at line " << __LINE__ << "\n"; throw std::runtime_error("assertion failed"); } } while(0)
#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

// =====================================================================
// LockManager Tests
// =====================================================================

void test_shared_lock_compatibility() {
    LockManager lm;
    // Two shared locks on same row should both succeed
    bool ok1 = lm.acquireLock(1, "row_1", LockMode::SHARED, 1000ms);
    bool ok2 = lm.acquireLock(2, "row_1", LockMode::SHARED, 1000ms);
    ASSERT_TRUE(ok1);
    ASSERT_TRUE(ok2);
    ASSERT_TRUE(lm.checkLock(1, "row_1", LockMode::SHARED));
    ASSERT_TRUE(lm.checkLock(2, "row_1", LockMode::SHARED));
    lm.releaseAllLocks(1);
    lm.releaseAllLocks(2);
}

void test_exclusive_lock_blocks() {
    LockManager lm;
    bool ok1 = lm.acquireLock(1, "row_1", LockMode::EXCLUSIVE, 1000ms);
    ASSERT_TRUE(ok1);

    // Second exclusive should timeout
    bool ok2 = lm.acquireLock(2, "row_1", LockMode::EXCLUSIVE, 500ms);
    ASSERT_FALSE(ok2);

    lm.releaseAllLocks(1);

    // Now should succeed
    bool ok3 = lm.acquireLock(2, "row_1", LockMode::EXCLUSIVE, 1000ms);
    ASSERT_TRUE(ok3);
    lm.releaseAllLocks(2);
}

void test_release_lock() {
    LockManager lm;
    lm.acquireLock(1, "row_1", LockMode::EXCLUSIVE, 1000ms);
    ASSERT_TRUE(lm.checkLock(1, "row_1", LockMode::EXCLUSIVE));

    lm.releaseLock(1, "row_1");
    ASSERT_FALSE(lm.checkLock(1, "row_1", LockMode::EXCLUSIVE));
}

void test_release_all_locks() {
    LockManager lm;
    lm.acquireLock(1, "row_1", LockMode::EXCLUSIVE, 1000ms);
    lm.acquireLock(1, "row_2", LockMode::EXCLUSIVE, 1000ms);
    lm.acquireLock(1, "row_3", LockMode::SHARED, 1000ms);

    lm.releaseAllLocks(1);
    ASSERT_FALSE(lm.checkLock(1, "row_1", LockMode::EXCLUSIVE));
    ASSERT_FALSE(lm.checkLock(1, "row_2", LockMode::EXCLUSIVE));
    ASSERT_FALSE(lm.checkLock(1, "row_3", LockMode::SHARED));
}

void test_wait_for_edges() {
    LockManager lm;
    lm.acquireLock(1, "row_1", LockMode::EXCLUSIVE, 1000ms);

    // Txn 2 will block in a separate thread
    std::atomic<bool> waiting{false};
    std::thread t([&]() {
        waiting = true;
        lm.acquireLock(2, "row_1", LockMode::EXCLUSIVE, 5000ms);
    });

    std::this_thread::sleep_for(200ms);

    auto edges = lm.getWaitForEdges();
    // Should have edge: txn2 waits for txn1 on row_1
    bool found = false;
    for (auto& e : edges) {
        if (e.waitingTxn == 2 && e.holdingTxn == 1 && e.resourceId == "row_1")
            found = true;
    }
    ASSERT_TRUE(found);

    lm.releaseAllLocks(1);
    t.join();
    lm.releaseAllLocks(2);
}

void test_shared_exclusive_conflict() {
    LockManager lm;
    lm.acquireLock(1, "row_1", LockMode::SHARED, 1000ms);

    // Exclusive should block and timeout
    bool ok = lm.acquireLock(2, "row_1", LockMode::EXCLUSIVE, 300ms);
    ASSERT_FALSE(ok);

    // But another shared should succeed
    bool ok2 = lm.acquireLock(3, "row_1", LockMode::SHARED, 1000ms);
    ASSERT_TRUE(ok2);

    lm.releaseAllLocks(1);
    lm.releaseAllLocks(3);
}

// =====================================================================
// DeadlockDetector Tests
// =====================================================================

void test_no_deadlock() {
    DeadlockDetector dd;
    std::vector<WaitEdge> edges = {
        {1, 2, "row_1"},
        {2, 3, "row_2"},
    };
    auto victims = dd.detectCycles(edges);
    ASSERT_TRUE(victims.empty());
}

void test_simple_deadlock() {
    DeadlockDetector dd;
    // A→B and B→A = cycle
    std::vector<WaitEdge> edges = {
        {1, 2, "row_1"},
        {2, 1, "row_2"},
    };
    auto victims = dd.detectCycles(edges);
    ASSERT_FALSE(victims.empty());
    // Victim should be the highest TxnId = 2
    ASSERT_EQ(victims[0], (TxnId)2);
}

void test_three_way_deadlock() {
    DeadlockDetector dd;
    // A→B→C→A
    std::vector<WaitEdge> edges = {
        {1, 2, "r1"},
        {2, 3, "r2"},
        {3, 1, "r3"},
    };
    auto victims = dd.detectCycles(edges);
    ASSERT_FALSE(victims.empty());
    ASSERT_EQ(victims[0], (TxnId)3);
}

void test_empty_edges() {
    DeadlockDetector dd;
    auto victims = dd.detectCycles({});
    ASSERT_TRUE(victims.empty());
}

// =====================================================================
// Transaction Tests
// =====================================================================

void test_transaction_lifecycle() {
    Transaction txn(42, IsolationLevel::SERIALIZABLE);
    ASSERT_EQ(txn.getId(), (TxnId)42);
    ASSERT_EQ(txn.getState(), TxnState::ACTIVE);
    ASSERT_EQ(txn.getIsolation(), IsolationLevel::SERIALIZABLE);
    ASSERT_FALSE(txn.isDistributed());

    txn.addLockedResource("row_1");
    txn.addLockedResource("row_2");
    ASSERT_EQ(txn.getLockedResources().size(), (size_t)2);

    txn.removeLockedResource("row_1");
    ASSERT_EQ(txn.getLockedResources().size(), (size_t)1);

    txn.addParticipantNode(1);
    txn.addParticipantNode(2);
    ASSERT_TRUE(txn.isDistributed());

    txn.setState(TxnState::COMMITTED);
    ASSERT_EQ(txn.getState(), TxnState::COMMITTED);
}

// =====================================================================
// TransactionManager Tests
// =====================================================================

void test_txn_manager_begin_commit() {
    LockManager lm;
    TransactionManager tm(lm);

    TxnId t1 = tm.beginTransaction();
    ASSERT_EQ(tm.getTransactionState(t1), TxnState::ACTIVE);

    bool locked = tm.acquireLock(t1, "row_1", LockMode::EXCLUSIVE);
    ASSERT_TRUE(locked);

    bool committed = tm.commitTransaction(t1);
    ASSERT_TRUE(committed);
    ASSERT_EQ(tm.getTransactionState(t1), TxnState::COMMITTED);

    // Lock should be released
    ASSERT_FALSE(lm.checkLock(t1, "row_1", LockMode::EXCLUSIVE));
}

void test_txn_manager_abort() {
    LockManager lm;
    TransactionManager tm(lm);

    TxnId t1 = tm.beginTransaction();
    tm.acquireLock(t1, "row_1", LockMode::EXCLUSIVE);

    bool aborted = tm.abortTransaction(t1);
    ASSERT_TRUE(aborted);
    ASSERT_EQ(tm.getTransactionState(t1), TxnState::ABORTED);
    ASSERT_FALSE(lm.checkLock(t1, "row_1", LockMode::EXCLUSIVE));
}

void test_txn_manager_prepare() {
    LockManager lm;
    TransactionManager tm(lm);

    TxnId t1 = tm.beginTransaction();
    tm.acquireLock(t1, "row_1", LockMode::EXCLUSIVE);

    bool prepared = tm.prepareTransaction(t1);
    ASSERT_TRUE(prepared);
    ASSERT_EQ(tm.getTransactionState(t1), TxnState::PREPARING);

    // Commit from prepared state
    tm.commitPrepared(t1);
    ASSERT_EQ(tm.getTransactionState(t1), TxnState::COMMITTED);
}

// =====================================================================
// TransactionLog Tests
// =====================================================================

void test_transaction_log() {
    std::string logPath = "logs/test_wal.log";
    std::filesystem::create_directories("logs");
    // Remove old log if exists
    std::filesystem::remove(logPath);

    {
        TransactionLog log(logPath);
        log.logBegin(1);
        log.logOperation(1, "row_1", "write_data");
        log.logPrepare(1);
        log.logCommit(1);
        log.logBegin(2);
        log.logAbort(2);
    }

    // Read back
    TransactionLog log2(logPath);
    auto entries = log2.readLog();
    ASSERT_EQ(entries.size(), (size_t)6);
    ASSERT_EQ(entries[0].type, LogEntry::Type::TXN_BEGIN);
    ASSERT_EQ(entries[0].txnId, (TxnId)1);
    ASSERT_EQ(entries[1].type, LogEntry::Type::OPERATION);
    ASSERT_EQ(entries[2].type, LogEntry::Type::TXN_PREPARE);
    ASSERT_EQ(entries[3].type, LogEntry::Type::TXN_COMMIT);
    ASSERT_EQ(entries[4].type, LogEntry::Type::TXN_BEGIN);
    ASSERT_EQ(entries[5].type, LogEntry::Type::TXN_ABORT);

    std::filesystem::remove(logPath);
}

// =====================================================================
// Message Serialization Tests
// =====================================================================

void test_message_roundtrip() {
    Message msg;
    msg.type       = MessageType::LOCK_REQUEST;
    msg.senderId   = 42;
    msg.txnId      = 100;
    msg.resourceId = "row_123";
    msg.lockMode   = LockMode::EXCLUSIVE;
    msg.success    = true;
    msg.waitEdges  = {{1, 2, "r1"}, {3, 4, "r2"}};
    msg.participants = {10, 20, 30};

    std::string serialized = msg.serialize();
    Message deserialized = Message::deserialize(serialized);

    ASSERT_EQ(deserialized.type, MessageType::LOCK_REQUEST);
    ASSERT_EQ(deserialized.senderId, (NodeId)42);
    ASSERT_EQ(deserialized.txnId, (TxnId)100);
    ASSERT_EQ(deserialized.resourceId, std::string("row_123"));
    ASSERT_EQ(deserialized.lockMode, LockMode::EXCLUSIVE);
    ASSERT_TRUE(deserialized.success);
    ASSERT_EQ(deserialized.waitEdges.size(), (size_t)2);
    ASSERT_EQ(deserialized.waitEdges[0].waitingTxn, (TxnId)1);
    ASSERT_EQ(deserialized.waitEdges[1].holdingTxn, (TxnId)4);
    ASSERT_EQ(deserialized.participants.size(), (size_t)3);
    ASSERT_EQ(deserialized.participants[2], (NodeId)30);
}

void test_message_empty_fields() {
    Message msg;
    msg.type     = MessageType::HEARTBEAT;
    msg.senderId = 1;

    std::string serialized = msg.serialize();
    Message deserialized = Message::deserialize(serialized);

    ASSERT_EQ(deserialized.type, MessageType::HEARTBEAT);
    ASSERT_EQ(deserialized.senderId, (NodeId)1);
    ASSERT_TRUE(deserialized.waitEdges.empty());
    ASSERT_TRUE(deserialized.participants.empty());
}

// =====================================================================
// Test Runner
// =====================================================================

int main() {
    Logger::setLevel(LogLevel::WARN); // Quiet during tests

    struct TestCase { const char* name; void(*fn)(); };
    std::vector<TestCase> allTests = {
        // LockManager
        {"shared_lock_compatibility",     test_shared_lock_compatibility},
        {"exclusive_lock_blocks",         test_exclusive_lock_blocks},
        {"release_lock",                  test_release_lock},
        {"release_all_locks",             test_release_all_locks},
        {"wait_for_edges",                test_wait_for_edges},
        {"shared_exclusive_conflict",     test_shared_exclusive_conflict},
        // DeadlockDetector
        {"no_deadlock",                   test_no_deadlock},
        {"simple_deadlock",              test_simple_deadlock},
        {"three_way_deadlock",           test_three_way_deadlock},
        {"empty_edges",                  test_empty_edges},
        // Transaction
        {"transaction_lifecycle",        test_transaction_lifecycle},
        // TransactionManager
        {"txn_manager_begin_commit",     test_txn_manager_begin_commit},
        {"txn_manager_abort",            test_txn_manager_abort},
        {"txn_manager_prepare",          test_txn_manager_prepare},
        // TransactionLog
        {"transaction_log",              test_transaction_log},
        // Message
        {"message_roundtrip",            test_message_roundtrip},
        {"message_empty_fields",         test_message_empty_fields},
    };

    std::cout << "Running " << allTests.size() << " tests...\n\n";

    int passed = 0, failed = 0;
    for (auto& tc : allTests) {
        std::cout << "  " << tc.name << " ... ";
        try {
            tc.fn();
            std::cout << "PASS\n";
            passed++;
        } catch (const std::exception& e) {
            std::cout << "FAIL (" << e.what() << ")\n";
            failed++;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed, "
              << allTests.size() << " total\n";
    std::cout << "========================================\n";

    return failed > 0 ? 1 : 0;
}

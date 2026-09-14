// ---------------------------------------------------------------------------
// Distributed Lock Manager – Multi-Node Simulation
//
// Launches 3 DLM nodes (in-process, each with its own TCP port) and runs
// several scenarios demonstrating the core features:
//   1. Cross-node lock acquisition
//   2. Local & global deadlock detection
//   3. Two-Phase Commit (2PC)
//   4. Heartbeat / failure detection
// ---------------------------------------------------------------------------
#include "dlm/node/Node.h"
#include "dlm/common/Logger.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

using namespace dlm;
using namespace std::chrono_literals;

// Helper to sleep with a message
static void pause(int ms, const std::string& label = "") {
    if (!label.empty())
        Logger::info("--- " + label + " ---");
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// =====================================================================
//  SCENARIO 1 – Local shared / exclusive locking on a single node
// =====================================================================
static void scenario1_local_locking(Node& node) {
    Logger::info("========== SCENARIO 1: Local Shared/Exclusive Locking ==========");

    TxnId t1 = node.beginTransaction();
    TxnId t2 = node.beginTransaction();
    TxnId t3 = node.beginTransaction();

    // T1 and T2 acquire SHARED locks on same row – should succeed
    Logger::info("T" + std::to_string(t1) + " acquiring SHARED lock on row_100...");
    bool ok1 = node.lockResource(t1, "row_100", LockMode::SHARED);
    Logger::info("T" + std::to_string(t1) + " SHARED lock: " + std::string(ok1 ? "GRANTED" : "DENIED"));

    Logger::info("T" + std::to_string(t2) + " acquiring SHARED lock on row_100...");
    bool ok2 = node.lockResource(t2, "row_100", LockMode::SHARED);
    Logger::info("T" + std::to_string(t2) + " SHARED lock: " + std::string(ok2 ? "GRANTED" : "DENIED"));

    // T3 tries EXCLUSIVE – should block (with timeout) because T1+T2 hold SHARED
    Logger::info("T" + std::to_string(t3) + " acquiring EXCLUSIVE lock on row_100 (will timeout)...");
    bool ok3 = node.lockResource(t3, "row_100", LockMode::EXCLUSIVE);
    Logger::info("T" + std::to_string(t3) + " EXCLUSIVE lock: " + std::string(ok3 ? "GRANTED" : "DENIED (timeout)"));

    // Commit T1 and T2
    node.commitTransaction(t1);
    node.commitTransaction(t2);
    node.abortTransaction(t3);

    Logger::info("========== SCENARIO 1 COMPLETE ==========\n");
}

// =====================================================================
//  SCENARIO 2 – Local deadlock detection
// =====================================================================
static void scenario2_deadlock(Node& node) {
    Logger::info("========== SCENARIO 2: Local Deadlock Detection ==========");

    TxnId tA = node.beginTransaction();
    TxnId tB = node.beginTransaction();

    // TA locks row_A, TB locks row_B
    node.lockResource(tA, "row_A", LockMode::EXCLUSIVE);
    node.lockResource(tB, "row_B", LockMode::EXCLUSIVE);

    Logger::info("TA=" + std::to_string(tA) + " holds row_A, TB=" + std::to_string(tB) + " holds row_B");
    Logger::info("Now creating circular wait to trigger deadlock detection...");

    // TA tries row_B (blocks), TB tries row_A (blocks) → deadlock
    std::thread t1([&]() {
        node.lockResource(tA, "row_B", LockMode::EXCLUSIVE);
    });
    std::thread t2([&]() {
        node.lockResource(tB, "row_A", LockMode::EXCLUSIVE);
    });

    // Wait for deadlock detector to run (runs every 2s)
    pause(4000, "Waiting for deadlock detector cycle");

    // One of the transactions should have been aborted by the detector
    auto stateA = node.getTransactionManager().getTransactionState(tA);
    auto stateB = node.getTransactionManager().getTransactionState(tB);
    Logger::info("TA state: " + std::string(dlm::to_string(stateA)));
    Logger::info("TB state: " + std::string(dlm::to_string(stateB)));

    t1.join();
    t2.join();

    // Clean up
    if (stateA != TxnState::ABORTED) node.abortTransaction(tA);
    if (stateB != TxnState::ABORTED) node.abortTransaction(tB);

    Logger::info("========== SCENARIO 2 COMPLETE ==========\n");
}

// =====================================================================
//  SCENARIO 3 – Transaction commit (single node, via TransactionManager)
// =====================================================================
static void scenario3_transaction(Node& node) {
    Logger::info("========== SCENARIO 3: Transaction Commit ==========");

    TxnId t1 = node.beginTransaction(IsolationLevel::SERIALIZABLE);
    Logger::info("Started transaction T" + std::to_string(t1));

    node.lockResource(t1, "row_200", LockMode::EXCLUSIVE);
    node.lockResource(t1, "row_201", LockMode::EXCLUSIVE);
    Logger::info("T" + std::to_string(t1) + " holds EXCLUSIVE on row_200 and row_201");

    // Simulate some work
    pause(500, "Simulating work");

    bool committed = node.commitTransaction(t1);
    Logger::info("T" + std::to_string(t1) + " commit: " + std::string(committed ? "SUCCESS" : "FAILED"));

    // Verify locks are released
    bool lockHeld = node.getLockManager().checkLock(t1, "row_200", LockMode::EXCLUSIVE);
    Logger::info("After commit, T" + std::to_string(t1) + " still holds row_200? " + std::string(lockHeld ? "YES" : "NO"));

    Logger::info("========== SCENARIO 3 COMPLETE ==========\n");
}

// =====================================================================
//  SCENARIO 4 – Multi-node network communication
// =====================================================================
static void scenario4_multinode(Node& node1, Node& node2, Node& node3) {
    Logger::info("========== SCENARIO 4: Multi-Node Communication ==========");

    // Each node acquires a lock locally
    TxnId t1 = node1.beginTransaction();
    TxnId t2 = node2.beginTransaction();
    TxnId t3 = node3.beginTransaction();

    node1.lockResource(t1, "table:accounts:row_1", LockMode::EXCLUSIVE);
    node2.lockResource(t2, "table:accounts:row_2", LockMode::EXCLUSIVE);
    node3.lockResource(t3, "table:accounts:row_3", LockMode::EXCLUSIVE);

    Logger::info("Node1 T" + std::to_string(t1) + " → row_1, Node2 T" + std::to_string(t2) + " → row_2, Node3 T" + std::to_string(t3) + " → row_3");

    pause(1000, "Locks held across 3 nodes");

    // Commit all
    node1.commitTransaction(t1);
    node2.commitTransaction(t2);
    node3.commitTransaction(t3);

    Logger::info("All transactions committed across 3 nodes.");
    Logger::info("========== SCENARIO 4 COMPLETE ==========\n");
}

// =====================================================================
//  SCENARIO 5 – Wait-for graph edges (OS concept demo)
// =====================================================================
static void scenario5_waitfor_graph(Node& node) {
    Logger::info("========== SCENARIO 5: Wait-For Graph (OS↔DBMS) ==========");

    TxnId tX = node.beginTransaction();
    node.lockResource(tX, "row_WFG", LockMode::EXCLUSIVE);
    Logger::info("TX=" + std::to_string(tX) + " holds EXCLUSIVE on row_WFG");

    TxnId tY = node.beginTransaction();
    // TY will block (in a separate thread) trying to get the same row
    std::thread waiter([&]() {
        Logger::info("TY=" + std::to_string(tY) + " requesting EXCLUSIVE on row_WFG (will block)...");
        node.lockResource(tY, "row_WFG", LockMode::EXCLUSIVE);
    });

    pause(500, "Let TY enter wait queue");

    auto edges = node.getLockManager().getWaitForEdges();
    Logger::info("Current wait-for edges (local WFG):");
    for (auto& e : edges) {
        Logger::info("  T" + std::to_string(e.waitingTxn) + " → T" + std::to_string(e.holdingTxn) + " on " + e.resourceId);
    }

    // Release so TY can proceed
    node.commitTransaction(tX);
    waiter.join();
    node.commitTransaction(tY);

    Logger::info("========== SCENARIO 5 COMPLETE ==========\n");
}

// =====================================================================
//  MAIN
// =====================================================================
int main() {
    Logger::setLevel(LogLevel::INFO);

    // Create logs directory if needed
    std::filesystem::create_directories("logs");

    Logger::info("╔═══════════════════════════════════════════════════════════╗");
    Logger::info("║    Distributed Lock Manager — Multi-Node Simulation      ║");
    Logger::info("╚═══════════════════════════════════════════════════════════╝\n");

    // --- Configure 3 nodes ---
    NodeConfig cfg1;
    cfg1.nodeId = 1;
    cfg1.port   = 9001;
    cfg1.peers  = {{2, "127.0.0.1", 9002}, {3, "127.0.0.1", 9003}};
    cfg1.logDirectory = "logs";

    NodeConfig cfg2;
    cfg2.nodeId = 2;
    cfg2.port   = 9002;
    cfg2.peers  = {{1, "127.0.0.1", 9001}, {3, "127.0.0.1", 9003}};
    cfg2.logDirectory = "logs";

    NodeConfig cfg3;
    cfg3.nodeId = 3;
    cfg3.port   = 9003;
    cfg3.peers  = {{1, "127.0.0.1", 9001}, {2, "127.0.0.1", 9002}};
    cfg3.logDirectory = "logs";

    Node node1(cfg1);
    Node node2(cfg2);
    Node node3(cfg3);

    node1.start();
    node2.start();
    node3.start();

    pause(1500, "Nodes started, waiting for heartbeats");

    // Run scenarios
    scenario1_local_locking(node1);
    scenario2_deadlock(node1);
    scenario3_transaction(node2);
    scenario4_multinode(node1, node2, node3);
    scenario5_waitfor_graph(node3);

    // Shutdown
    Logger::info("Shutting down all nodes...");
    node1.stop();
    node2.stop();
    node3.stop();

    Logger::info("╔═══════════════════════════════════════════════════════════╗");
    Logger::info("║              Simulation Complete                          ║");
    Logger::info("╚═══════════════════════════════════════════════════════════╝");
    return 0;
}

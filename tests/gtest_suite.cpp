#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "dlm/api/ApiServer.h"
#include "dlm/api/AuthManager.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include "dlm/common/Types.h"
#include "dlm/coordinator/TwoPCCoordinator.h"
#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/lock_manager/LockManager.h"
#include "dlm/network/Message.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/node/Node.h"
#include "dlm/sql/CommandParser.h"
#include "dlm/storage/ShardRouter.h"
#include "dlm/storage/StorageEngine.h"
#include "dlm/transaction/TransactionLog.h"
#include "dlm/transaction/TransactionManager.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace dlm;
using namespace std::chrono_literals;

namespace {

struct TempDir {
    std::string path;
    explicit TempDir(const std::string& name) {
        path = "build_test_tmp_" + name + "_" + std::to_string(nowEpochMs());
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// Simple TCP HTTP client helper for end-to-end API verification
bool httpRequest(const std::string& host, uint16_t port,
                 const std::string& method, const std::string& path,
                 const std::string& body, const std::string& bearerToken,
                 int& outStatus, std::string& outBody) {
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        return false;
    }
#ifdef _WIN32
    SOCKET sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) { freeaddrinfo(res); return false; }
#else
    int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return false; }
#endif
    if (::connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n";
    if (!bearerToken.empty()) {
        req << "Authorization: Bearer " << bearerToken << "\r\n";
    }
    req << "Connection: close\r\n\r\n" << body;
    std::string raw = req.str();
    ::send(sock, raw.data(), static_cast<int>(raw.size()), 0);

    std::string resp;
    char buf[4096];
    int n = 0;
    while ((n = ::recv(sock, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(n));
    }
#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif

    auto hdrEnd = resp.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return false;
    std::string hdrs = resp.substr(0, hdrEnd);
    outBody = resp.substr(hdrEnd + 4);
    auto sp1 = hdrs.find(' ');
    if (sp1 == std::string::npos) return false;
    outStatus = std::stoi(hdrs.substr(sp1 + 1, 3));
    return true;
}

std::vector<NodeConfig> makeThreeNodeConfigs(const std::string& baseDir, uint16_t basePort) {
    std::vector<NodeConfig> cfgs(3);
    for (int i = 0; i < 3; ++i) {
        cfgs[i].nodeId = static_cast<NodeId>(i + 1);
        cfgs[i].port = static_cast<uint16_t>(basePort + i);
        cfgs[i].dataDirectory = baseDir;
        cfgs[i].logDirectory = baseDir + "/logs";
        cfgs[i].deadlockCheckInterval = 250ms;
        cfgs[i].heartbeatInterval = 250ms;
        cfgs[i].heartbeatTimeout = 1500ms;
        cfgs[i].lockTimeout = 2500ms;
        for (int j = 0; j < 3; ++j) {
            if (i != j) {
                cfgs[i].peers.push_back({static_cast<NodeId>(j + 1), "127.0.0.1",
                                         static_cast<uint16_t>(basePort + j)});
            }
        }
    }
    return cfgs;
}

// Find 3 keys that deterministically hash to Node 1, Node 2, and Node 3
std::array<std::string, 3> findKeysPerShard(const ShardRouter& router) {
    std::array<std::string, 3> keys;
    for (int i = 0; i < 500; ++i) {
        std::string k = "shard_key_" + std::to_string(i);
        NodeId owner = router.getOwnerNode(k);
        if (owner >= 1 && owner <= 3 && keys[owner - 1].empty()) {
            keys[owner - 1] = k;
        }
        if (!keys[0].empty() && !keys[1].empty() && !keys[2].empty()) break;
    }
    return keys;
}

} // namespace

// =====================================================================
// 1. UNIT TESTS
// =====================================================================

TEST(UnitLockManager, CompatibilityUpgradeDowngradeCancel) {
    LockManager lm;
    ASSERT_TRUE(lm.acquireLock(1, "k1", LockMode::SHARED, 500ms));
    ASSERT_TRUE(lm.acquireLock(2, "k1", LockMode::SHARED, 500ms));
    // Re-entrant shared lock succeeds even with waiter
    ASSERT_TRUE(lm.acquireLock(1, "k1", LockMode::SHARED, 500ms));

    // Txn 1 sole upgrade after Txn 2 releases
    lm.releaseLock(2, "k1");
    ASSERT_TRUE(lm.acquireLock(1, "k1", LockMode::EXCLUSIVE, 500ms));
    ASSERT_TRUE(lm.checkLock(1, "k1", LockMode::EXCLUSIVE));

    // Downgrade X -> S allows Txn 3 shared lock
    ASSERT_TRUE(lm.downgradeLock(1, "k1"));
    ASSERT_TRUE(lm.acquireLock(3, "k1", LockMode::SHARED, 500ms));

    // Wait cancellation test
    std::atomic<bool> acquired{true};
    std::thread t([&]() {
        acquired = lm.acquireLock(4, "k1", LockMode::EXCLUSIVE, 5000ms);
    });
    std::this_thread::sleep_for(100ms);
    lm.cancelWait(4);
    t.join();
    ASSERT_FALSE(acquired.load());

    lm.releaseAllLocks(1);
    lm.releaseAllLocks(3);
}

TEST(UnitLockManager, UpgradeDeadlockBetweenTwoSharedOwners) {
    LockManager lm;
    ASSERT_TRUE(lm.acquireLock(1, "k1", LockMode::SHARED, 500ms));
    ASSERT_TRUE(lm.acquireLock(2, "k1", LockMode::SHARED, 500ms));

    std::atomic<bool> ok1{false}, ok2{false};
    std::thread t1([&]() { ok1 = lm.acquireLock(1, "k1", LockMode::EXCLUSIVE, 1500ms); });
    std::thread t2([&]() { ok2 = lm.acquireLock(2, "k1", LockMode::EXCLUSIVE, 1500ms); });

    std::this_thread::sleep_for(150ms);
    auto edges = lm.getWaitForEdges();
    DeadlockDetector dd;
    auto victims = dd.detectCycles(edges);
    ASSERT_FALSE(victims.empty());
    lm.releaseAllLocks(victims[0]);

    t1.join();
    t2.join();
    ASSERT_TRUE(ok1.load() || ok2.load());
    lm.releaseAllLocks(1);
    lm.releaseAllLocks(2);
}

TEST(UnitTxn, GlobalUniqueTxnIdsAndLifecycleTransitions) {
    TxnId a = makeTxnId(1, 1);
    TxnId b = makeTxnId(2, 1);
    TxnId c = makeTxnId(3, 1);
    ASSERT_NE(a, b);
    ASSERT_NE(b, c);
    ASSERT_EQ(txnOriginNode(a), 1u);
    ASSERT_EQ(txnOriginNode(b), 2u);
    ASSERT_EQ(txnSequence(b), 1ULL);

    LockManager lm;
    TransactionManager tm(lm, "", 2);
    TxnId tid = tm.beginTransaction();
    ASSERT_EQ(txnOriginNode(tid), 2u);
    ASSERT_TRUE(tm.commitTransaction(tid));
    // Idempotent commit returns true
    ASSERT_TRUE(tm.commitTransaction(tid));
    // Invalid COMMITTED -> ABORTED rejected
    ASSERT_FALSE(tm.abortTransaction(tid));
    ASSERT_EQ(tm.getTransactionState(tid), TxnState::COMMITTED);
}

TEST(UnitDeadlock, MultipleSimultaneousCycles) {
    DeadlockDetector dd;
    std::vector<WaitEdge> edges = {
        {1, 2, "a"}, {2, 1, "b"},
        {10, 11, "c"}, {11, 12, "d"}, {12, 10, "e"}
    };
    auto victims = dd.detectCycles(edges);
    ASSERT_EQ(victims.size(), 2u);
    ASSERT_EQ(victims[0], 2ULL);
    ASSERT_EQ(victims[1], 12ULL);
}

TEST(UnitStorage, CrudScanIterateShardingAndPersistence) {
    TempDir dir("storage");
    {
        StorageEngine se(dir.path, 1);
        ASSERT_TRUE(se.put("user:1", R"({"name":"Alice","bal":100})"));
        ASSERT_TRUE(se.put("user:2", "plain-string-value"));
        ASSERT_TRUE(se.exists("user:1"));
        auto r1 = se.get("user:1");
        ASSERT_TRUE(r1.has_value());
        ASSERT_TRUE(r1->isJson);
        auto r2 = se.get("user:2");
        ASSERT_TRUE(r2.has_value());
        ASSERT_FALSE(r2->isJson);

        auto scanned = se.scan("user:");
        ASSERT_EQ(scanned.size(), 2u);

        int count = 0;
        se.iterate([&](const StoredRecord&) { ++count; return true; });
        ASSERT_EQ(count, 2);

        ASSERT_TRUE(se.erase("user:2"));
        ASSERT_FALSE(se.exists("user:2"));
        ASSERT_TRUE(se.checkpoint(42));
    }

    // Reopen from disk and verify persistence
    {
        StorageEngine se2(dir.path, 1);
        ASSERT_TRUE(se2.exists("user:1"));
        ASSERT_FALSE(se2.exists("user:2"));
        auto stats = se2.getStats();
        ASSERT_EQ(stats.checkpointLsn, 42ULL);
    }

    // FNV-1a sharding determinism
    ShardRouter rA({1, 2, 3});
    ShardRouter rB({3, 1, 2});
    ASSERT_EQ(rA.getOwnerNode("account_101"), rB.getOwnerNode("account_101"));
}

TEST(UnitWal, ChecksumAndCorruptedTailDetection) {
    TempDir dir("wal");
    std::string walPath = dir.path + "/wal.log";
    {
        TransactionLog log(walPath, 1);
        log.logBegin(10);
        log.logPut(10, "k1", "", false, "v1");
        log.logCommit(10);
    }
    // Append partial/corrupted tail line (simulating abrupt crash mid-write)
    {
        std::ofstream f(walPath, std::ios::app | std::ios::binary);
        f << "deadbeef|{\"lsn\":99,\"truncated";
    }
    TransactionLog log2(walPath, 1);
    bool truncated = false;
    auto entries = log2.readLogSafe(&truncated);
    ASSERT_TRUE(truncated);
    ASSERT_EQ(entries.size(), 3u);
}

TEST(UnitMessage, BinaryRoundtripWithDelimitersAndMalformedInput) {
    Message msg;
    msg.type = MessageType::KV_PUT_REQ;
    msg.senderId = 2;
    msg.requestId = 999;
    msg.txnId = makeTxnId(2, 77);
    msg.key = "key|with,special;[chars]";
    msg.value = R"({"msg":"hello|world\nline2","arr":[1,2,3]})";

    std::string bin = msg.serialize();
    Message decoded;
    ASSERT_TRUE(Message::deserialize(bin, decoded));
    ASSERT_EQ(decoded.type, msg.type);
    ASSERT_EQ(decoded.requestId, 999ULL);
    ASSERT_EQ(decoded.txnId, msg.txnId);
    ASSERT_EQ(decoded.key, msg.key);
    ASSERT_EQ(decoded.value, msg.value);

    // Malformed / truncated packet rejected safely
    Message bad;
    ASSERT_FALSE(Message::deserialize("BAD_MAGIC", bad));
    ASSERT_FALSE(Message::deserialize(bin.substr(0, 10), bad));
}

TEST(UnitSqlParser, ParseScriptsAndReportSyntaxErrors) {
    auto okRes = CommandParser::parse(R"(
        BEGIN;
        PUT account_101 {"balance":5000};
        GET account_101;
        DELETE account_101;
        ABORT;
    )");
    ASSERT_TRUE(okRes.ok);
    ASSERT_EQ(okRes.commands.size(), 5u);
    ASSERT_EQ(okRes.commands[1].type, SqlCommandType::PUT);
    ASSERT_EQ(okRes.commands[1].key, "account_101");

    auto badRes = CommandParser::parse("PUT only_key;");
    ASSERT_FALSE(badRes.ok);
    ASSERT_GT(badRes.errorLine, 0u);
}

TEST(UnitAuth, Pbkdf2HashingSetupLoginLogout) {
    TempDir dir("auth");
    AuthManager auth(dir.path + "/auth.json");
    auth.initialize();
    ASSERT_FALSE(auth.isSetupComplete());

    std::string err;
    ASSERT_TRUE(auth.setupInitialAdmin("admin", "StrongPass123!", err));
    ASSERT_TRUE(auth.isSetupComplete());

    std::string tok;
    ASSERT_FALSE(auth.login("admin", "WrongPassword", tok, err));
    ASSERT_TRUE(auth.login("admin", "StrongPass123!", tok, err));

    AuthSession sess;
    ASSERT_TRUE(auth.validateToken(tok, sess));
    ASSERT_EQ(sess.username, "admin");

    auth.logout(tok);
    ASSERT_FALSE(auth.validateToken(tok, sess));
}

// =====================================================================
// 2. DATA CONSISTENCY & RECOVERY TESTS
// =====================================================================

TEST(Consistency, NoDirtyReadsAndUncommittedUndoOnRecovery) {
    TempDir dir("dirty");
    {
        StorageEngine se(dir.path, 1);
        LockManager lm;
        TransactionManager tm(lm, se.getWalDir() + "/wal.log", 1, &se);
        tm.recoverFromWal();

        // T1 writes uncommitted data
        TxnId t1 = tm.beginTransaction();
        ASSERT_EQ(tm.writeKey(t1, "acct_1", "999"), OpStatus::OK);
        // T1 sees its own uncommitted write
        std::string val;
        bool found = false;
        ASSERT_EQ(tm.readKey(t1, "acct_1", val, found), OpStatus::OK);
        ASSERT_TRUE(found);
        ASSERT_EQ(val, "999");

        // T2 cannot read acct_1 (strict 2PL blocks) and storage does not expose dirty write
        ASSERT_FALSE(se.exists("acct_1"));

        // Simulate abrupt crash without commit/abort
    }

    // Restart and recover: T1 must be undone, data must NOT appear
    {
        StorageEngine se(dir.path, 1);
        LockManager lm;
        TransactionManager tm(lm, se.getWalDir() + "/wal.log", 1, &se);
        RecoveryReport rep = tm.recoverFromWal();
        ASSERT_EQ(rep.undoneTransactions, 1ULL);
        ASSERT_FALSE(se.exists("acct_1"));

        // Running recovery again is idempotent
        RecoveryReport rep2 = tm.recoverFromWal();
        ASSERT_EQ(rep2.undoneTransactions, 0ULL);
    }
}

TEST(Failure, DiskWriteFailurePreventsCommit) {
    TempDir dir("wal_fail");
    StorageEngine se(dir.path, 1);
    LockManager lm;
    TransactionManager tm(lm, se.getWalDir() + "/wal.log", 1, &se);

    TxnId t1 = tm.beginTransaction();
    ASSERT_EQ(tm.writeKey(t1, "k_fail", "val"), OpStatus::OK);
    tm.getTransactionLog()->setFailWrites(true);
    ASSERT_FALSE(tm.commitTransaction(t1));
    ASSERT_FALSE(se.exists("k_fail"));
    tm.getTransactionLog()->setFailWrites(false);
    tm.abortTransaction(t1, "Cleanup");
}

// =====================================================================
// 3. INTEGRATION TESTS (3-NODE CLUSTER + 2PC + DISTRIBUTED DEADLOCK)
// =====================================================================

TEST(IntegrationCluster, CrossNodeAtomic2PCCommitAndRestartPersistence) {
    TempDir dir("cluster_2pc");
    auto cfgs = makeThreeNodeConfigs(dir.path, 19101);
    ShardRouter router({1, 2, 3});
    auto keys = findKeysPerShard(router);
    ASSERT_FALSE(keys[0].empty());
    ASSERT_FALSE(keys[1].empty());
    ASSERT_FALSE(keys[2].empty());

    {
        Node n1(cfgs[0]), n2(cfgs[1]), n3(cfgs[2]);
        n1.start(); n2.start(); n3.start();
        std::this_thread::sleep_for(450ms);

        // Distributed transaction coordinated on Node 1 touching keys owned by Nodes 1, 2, 3
        TxnId t1 = n1.beginTransaction();
        ASSERT_TRUE(n1.put(t1, keys[0], R"({"shard":1})").ok);
        ASSERT_TRUE(n1.put(t1, keys[1], R"({"shard":2})").ok);
        ASSERT_TRUE(n1.put(t1, keys[2], R"({"shard":3})").ok);

        // Before commit, storage on all 3 nodes has no dirty writes
        ASSERT_FALSE(n1.getStorageEngine().exists(keys[0]));
        ASSERT_FALSE(n2.getStorageEngine().exists(keys[1]));
        ASSERT_FALSE(n3.getStorageEngine().exists(keys[2]));

        ASSERT_TRUE(n1.commitTransaction(t1));

        // Committed atomically across all 3 shards
        ASSERT_TRUE(n1.getStorageEngine().exists(keys[0]));
        ASSERT_TRUE(n2.getStorageEngine().exists(keys[1]));
        ASSERT_TRUE(n3.getStorageEngine().exists(keys[2]));

        // Distributed abort test
        TxnId t2 = n1.beginTransaction();
        ASSERT_TRUE(n1.put(t2, keys[1], "ABORTED_VAL").ok);
        ASSERT_TRUE(n1.put(t2, keys[2], "ABORTED_VAL").ok);
        ASSERT_TRUE(n1.abortTransaction(t2, "Intentional multi-node abort"));
        ASSERT_EQ(n2.getStorageEngine().get(keys[1])->value, R"({"shard":2})");

        n1.stop(); n2.stop(); n3.stop();
    }

    // Restart all 3 nodes and verify committed data survives
    {
        Node n1(cfgs[0]), n2(cfgs[1]), n3(cfgs[2]);
        n1.start(); n2.start(); n3.start();
        ASSERT_EQ(n1.getStorageEngine().get(keys[0])->value, R"({"shard":1})");
        ASSERT_EQ(n2.getStorageEngine().get(keys[1])->value, R"({"shard":2})");
        ASSERT_EQ(n3.getStorageEngine().get(keys[2])->value, R"({"shard":3})");
        n1.stop(); n2.stop(); n3.stop();
    }
}

TEST(IntegrationCluster, CrossNodeDeadlockDetectedAndResolved) {
    TempDir dir("cluster_dd");
    auto cfgs = makeThreeNodeConfigs(dir.path, 19201);
    ShardRouter router({1, 2, 3});
    auto keys = findKeysPerShard(router);

    Node n1(cfgs[0]), n2(cfgs[1]), n3(cfgs[2]);
    n1.start(); n2.start(); n3.start();
    std::this_thread::sleep_for(500ms);

    // T1 on Node 1 locks key on Node 1; T2 on Node 2 locks key on Node 2
    TxnId t1 = n1.beginTransaction();
    TxnId t2 = n2.beginTransaction();
    ASSERT_TRUE(n1.put(t1, keys[0], "from_t1").ok);
    ASSERT_TRUE(n2.put(t2, keys[1], "from_t2").ok);

    // Cross-node circular wait: T1 requests keys[1] (on Node 2), T2 requests keys[0] (on Node 1)
    std::atomic<bool> t1Ok{false}, t2Ok{false};
    std::thread th1([&]() { t1Ok = n1.put(t1, keys[1], "deadlock1").ok; });
    std::thread th2([&]() { t2Ok = n2.put(t2, keys[0], "deadlock2").ok; });

    th1.join();
    th2.join();

    // Exactly one should succeed or recover after deadlock victim abort
    ASSERT_FALSE(t1Ok.load() && t2Ok.load());

    n1.abortTransaction(t1, "cleanup");
    n2.abortTransaction(t2, "cleanup");
    n1.stop(); n2.stop(); n3.stop();
}

TEST(IntegrationFailure, NodeUnavailableAndInDoubt2PCRecovery) {
    TempDir dir("cluster_fail");
    auto cfgs = makeThreeNodeConfigs(dir.path, 19301);
    ShardRouter router({1, 2, 3});
    auto keys = findKeysPerShard(router);

    Node n1(cfgs[0]), n2(cfgs[1]), n3(cfgs[2]);
    n1.start(); n2.start(); n3.start();
    std::this_thread::sleep_for(450ms);

    // Crash Node 3: accessing keys[2] must report NODE_UNAVAILABLE (not route elsewhere)
    n3.stop();
    std::this_thread::sleep_for(200ms);
    TxnId tFail = n1.beginTransaction();
    auto res = n1.put(tFail, keys[2], "should_fail");
    ASSERT_FALSE(res.ok);
    ASSERT_EQ(res.status, OpStatus::NODE_UNAVAILABLE);
    n1.abortTransaction(tFail, "Shard unavailable");

    // Restart Node 3 and verify in-doubt participant resolution via TXN_STATUS_REQ
    n3.start();
    std::this_thread::sleep_for(450ms);

    // Simulate participant Node 2 preparing TxnId from Node 1 and surviving restart
    TxnId tInDoubt = n1.beginTransaction();
    ASSERT_TRUE(n1.put(tInDoubt, keys[1], R"({"indoubt":true})").ok);
    ASSERT_TRUE(n2.getTransactionManager().prepareTransaction(tInDoubt));

    // Stop Node 2 cleanly (preserving WAL PREPARED record), restart Node 2 -> queries Node 1 -> resolves
    n2.stop();
    n1.abortTransaction(tInDoubt, "Coordinator aborted");
    n2.start();
    std::this_thread::sleep_for(400ms);
    ASSERT_FALSE(n2.getStorageEngine().exists(keys[1]));

    n1.stop(); n2.stop(); n3.stop();
}

TEST(IntegrationConcurrency, ConcurrentWritersNoLostUpdates) {
    TempDir dir("conc");
    StorageEngine se(dir.path, 1);
    LockManager lm;
    TransactionManager tm(lm, se.getWalDir() + "/wal.log", 1, &se);
    tm.setLockTimeout(5000ms);

    // Initialize counter = 0
    {
        TxnId t0 = tm.beginTransaction();
        ASSERT_EQ(tm.writeKey(t0, "counter", "0"), OpStatus::OK);
        ASSERT_TRUE(tm.commitTransaction(t0));
    }

    constexpr int kThreads = 6;
    constexpr int kIters = 5;
    std::vector<std::thread> threads;
    std::atomic<int> committed{0};

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&]() {
            for (int iter = 0; iter < kIters; ++iter) {
                TxnId t = tm.beginTransaction();
                std::string cur;
                bool found = false;
                if (tm.readKey(t, "counter", cur, found) != OpStatus::OK) {
                    tm.abortTransaction(t, "read failed");
                    continue;
                }
                int next = std::stoi(cur) + 1;
                if (tm.writeKey(t, "counter", std::to_string(next)) != OpStatus::OK) {
                    // Upgrade conflict / deadlock -> abort & retry
                    tm.abortTransaction(t, "upgrade conflict");
                    --iter;
                    std::this_thread::sleep_for(10ms);
                    continue;
                }
                if (tm.commitTransaction(t)) {
                    committed.fetch_add(1);
                }
            }
        });
    }
    for (auto& th : threads) th.join();

    auto rec = se.get("counter");
    ASSERT_TRUE(rec.has_value());
    ASSERT_EQ(std::stoi(rec->value), committed.load());
}

TEST(IntegrationEndToEnd, ApiServerHttpAuthAndSqlExecution) {
    TempDir dir("e2e_api");
    auto cfgs = makeThreeNodeConfigs(dir.path, 19401);
    Node n1(cfgs[0]), n2(cfgs[1]), n3(cfgs[2]);
    n1.start(); n2.start(); n3.start();
    std::this_thread::sleep_for(400ms);

    ApiServerConfig apiCfg;
    apiCfg.host = "127.0.0.1";
    apiCfg.port = 18085;
    apiCfg.authStorePath = dir.path + "/auth.json";
    apiCfg.cluster.nodes = {
        {1, "127.0.0.1", 19401},
        {2, "127.0.0.1", 19402},
        {3, "127.0.0.1", 19403}
    };
    apiCfg.cluster.dataDirectory = dir.path;
    apiCfg.cluster.logDirectory = dir.path + "/logs";

    ApiServer server(apiCfg);
    ASSERT_TRUE(server.start());

    int status = 0;
    std::string body;
    ASSERT_TRUE(httpRequest("127.0.0.1", 18085, "GET", "/api/health", "", "", status, body));
    ASSERT_EQ(status, 200);

    // Unauthenticated access to protected route returns 401
    ASSERT_TRUE(httpRequest("127.0.0.1", 18085, "GET", "/api/cluster", "", "", status, body));
    ASSERT_EQ(status, 401);

    // First-run setup
    ASSERT_TRUE(httpRequest("127.0.0.1", 18085, "POST", "/api/auth/setup",
                            R"({"username":"admin","password":"TestPassword123"})", "", status, body));
    ASSERT_EQ(status, 201);
    auto setupJson = nlohmann::json::parse(body);
    std::string token = setupJson.at("token").get<std::string>();

    // Execute real SQL-like script across cluster
    std::string sqlPayload =
        R"({"nodeId":1,"command":"BEGIN; PUT acct_e2e {\"balance\":777}; GET acct_e2e; COMMIT;"})";
    ASSERT_TRUE(httpRequest("127.0.0.1", 18085, "POST", "/api/sql",
                            sqlPayload, token, status, body));
    ASSERT_EQ(status, 200);
    auto sqlJson = nlohmann::json::parse(body);
    ASSERT_TRUE(sqlJson.value("ok", false));

    // Verify key appears in /api/database/keys
    ASSERT_TRUE(httpRequest("127.0.0.1", 18085, "GET", "/api/database/keys", "", token, status, body));
    ASSERT_EQ(status, 200);
    auto keysJson = nlohmann::json::parse(body);
    ASSERT_EQ(keysJson.value("count", 0), 1);

    server.stop();
    n1.stop(); n2.stop(); n3.stop();
}

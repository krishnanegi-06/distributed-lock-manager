// ---------------------------------------------------------------------------
// dlm_benchmark - Reproducible Latency, Throughput & Recovery Benchmarks
// ---------------------------------------------------------------------------
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/lock_manager/LockManager.h"
#include "dlm/node/Node.h"
#include "dlm/storage/ShardRouter.h"
#include "dlm/storage/StorageEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

using namespace dlm;

namespace {

struct Stats {
    double avgMs = 0.0;
    double medMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double throughputPerSec = 0.0;
    double errorRate = 0.0;
};

Stats computeStats(std::vector<double>& samplesMs, double totalWallSec, size_t errors) {
    Stats st;
    size_t total = samplesMs.size() + errors;
    st.errorRate = total > 0 ? (100.0 * static_cast<double>(errors) / static_cast<double>(total)) : 0.0;
    st.throughputPerSec = totalWallSec > 0.0 ? (static_cast<double>(samplesMs.size()) / totalWallSec) : 0.0;
    if (samplesMs.empty()) return st;

    std::sort(samplesMs.begin(), samplesMs.end());
    double sum = std::accumulate(samplesMs.begin(), samplesMs.end(), 0.0);
    st.avgMs = sum / static_cast<double>(samplesMs.size());
    st.medMs = samplesMs[samplesMs.size() / 2];
    st.p95Ms = samplesMs[static_cast<size_t>(0.95 * static_cast<double>(samplesMs.size() - 1))];
    st.p99Ms = samplesMs[static_cast<size_t>(0.99 * static_cast<double>(samplesMs.size() - 1))];
    return st;
}

void printRow(const std::string& name, const Stats& s) {
    std::cout << std::left << std::setw(36) << name
              << " | avg=" << std::fixed << std::setprecision(3) << std::setw(7) << s.avgMs << "ms"
              << " med=" << std::setw(7) << s.medMs << "ms"
              << " p95=" << std::setw(7) << s.p95Ms << "ms"
              << " p99=" << std::setw(7) << s.p99Ms << "ms"
              << " tput=" << std::setw(8) << std::setprecision(1) << s.throughputPerSec << "/s"
              << " err=" << std::setprecision(1) << s.errorRate << "%\n";
}

std::vector<NodeConfig> makeConfigs(int numNodes, const std::string& dir, uint16_t basePort) {
    std::vector<NodeConfig> cfgs(numNodes);
    for (int i = 0; i < numNodes; ++i) {
        cfgs[i].nodeId = static_cast<NodeId>(i + 1);
        cfgs[i].port = static_cast<uint16_t>(basePort + i);
        cfgs[i].dataDirectory = dir;
        cfgs[i].logDirectory = dir + "/logs";
        cfgs[i].deadlockCheckInterval = std::chrono::milliseconds(500);
        cfgs[i].heartbeatInterval = std::chrono::milliseconds(500);
        cfgs[i].heartbeatTimeout = std::chrono::milliseconds(3000);
        cfgs[i].lockTimeout = std::chrono::milliseconds(2000);
        for (int j = 0; j < numNodes; ++j) {
            if (i != j) {
                cfgs[i].peers.push_back({static_cast<NodeId>(j + 1), "127.0.0.1",
                                         static_cast<uint16_t>(basePort + j)});
            }
        }
    }
    return cfgs;
}

} // namespace

int main() {
    Logger::setLevel(LogLevel::ERR);
    std::string dir = "build_bench_tmp";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    std::cout << "====================================================================\n";
    std::cout << "         DLM-DB Performance Benchmark Suite                         \n";
    std::cout << "====================================================================\n\n";

    // 1. Lock acquisition microbenchmark
    {
        LockManager lm;
        std::vector<double> lats;
        constexpr int N = 5000;
        lats.reserve(N);
        auto w0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            lm.acquireLock(static_cast<TxnId>(i + 1), "res_" + std::to_string(i % 64), LockMode::EXCLUSIVE);
            lm.releaseLock(static_cast<TxnId>(i + 1), "res_" + std::to_string(i % 64));
            auto t1 = std::chrono::steady_clock::now();
            lats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        auto w1 = std::chrono::steady_clock::now();
        printRow("Lock Acquire+Release (5000 ops)",
                 computeStats(lats, std::chrono::duration<double>(w1 - w0).count(), 0));
    }

    // 2. Deadlock detection latency (100-node wait-for cycle)
    {
        DeadlockDetector dd;
        std::vector<WaitEdge> edges;
        for (TxnId i = 1; i <= 50; ++i) {
            edges.push_back({i, (i % 50) + 1, "k_" + std::to_string(i)});
        }
        std::vector<double> lats;
        auto w0 = std::chrono::steady_clock::now();
        for (int r = 0; r < 200; ++r) {
            auto t0 = std::chrono::steady_clock::now();
            auto v = dd.detectCycles(edges);
            (void)v;
            auto t1 = std::chrono::steady_clock::now();
            lats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        auto w1 = std::chrono::steady_clock::now();
        printRow("Deadlock Detection (50-txn cycle)",
                 computeStats(lats, std::chrono::duration<double>(w1 - w0).count(), 0));
    }

    // 3. Single-node PUT / GET / DELETE / Commit + WAL Recovery
    {
        std::string sdir = dir + "/single";
        NodeConfig cfg;
        cfg.nodeId = 1;
        cfg.port = 19501;
        cfg.dataDirectory = sdir;
        cfg.logDirectory = sdir + "/logs";
        {
            Node n1(cfg);
            n1.start();

            std::vector<double> putLats, getLats, delLats, commitLats;
            constexpr int N = 150;
            auto w0 = std::chrono::steady_clock::now();
            for (int i = 0; i < N; ++i) {
                TxnId tid = n1.beginTransaction();
                std::string k = "bench_key_" + std::to_string(i);
                auto t0 = std::chrono::steady_clock::now();
                n1.put(tid, k, R"({"bench":true,"seq":)" + std::to_string(i) + "}");
                auto t1 = std::chrono::steady_clock::now();
                putLats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

                t0 = std::chrono::steady_clock::now();
                n1.get(tid, k);
                t1 = std::chrono::steady_clock::now();
                getLats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

                t0 = std::chrono::steady_clock::now();
                n1.commitTransaction(tid);
                t1 = std::chrono::steady_clock::now();
                commitLats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            auto w1 = std::chrono::steady_clock::now();
            double wall = std::chrono::duration<double>(w1 - w0).count();
            printRow("Single-Node PUT (150 ops)", computeStats(putLats, wall, 0));
            printRow("Single-Node GET (150 ops)", computeStats(getLats, wall, 0));
            printRow("Single-Node Commit+WAL (150 ops)", computeStats(commitLats, wall, 0));

            for (int i = 0; i < 50; ++i) {
                TxnId tid = n1.beginTransaction();
                auto t0 = std::chrono::steady_clock::now();
                n1.remove(tid, "bench_key_" + std::to_string(i));
                n1.commitTransaction(tid);
                auto t1 = std::chrono::steady_clock::now();
                delLats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            printRow("Single-Node DELETE+Commit (50 ops)", computeStats(delLats, 1.0, 0));
            n1.stop();
        }

        // Measure WAL recovery time on restart
        auto r0 = std::chrono::steady_clock::now();
        Node n1Rec(cfg);
        n1Rec.start();
        auto r1 = std::chrono::steady_clock::now();
        double recMs = std::chrono::duration<double, std::milli>(r1 - r0).count();
        std::cout << "WAL Recovery Time (150 txns)         | " << std::fixed << std::setprecision(2)
                  << recMs << " ms\n\n";
        n1Rec.stop();
    }

    // 4. Multi-node scaling: Nodes = 1, 2, 3 x Concurrent Txns = 10, 50, 100
    std::cout << "--- Multi-Node Concurrent Transaction Matrix ---\n";
    uint16_t basePort = 19600;
    for (int numNodes : {1, 2, 3}) {
        for (int concTxns : {10, 50, 100}) {
            std::string mdir = dir + "/n" + std::to_string(numNodes) + "_c" + std::to_string(concTxns);
            auto cfgs = makeConfigs(numNodes, mdir, basePort);
            basePort = static_cast<uint16_t>(basePort + 10);

            std::vector<std::unique_ptr<Node>> nodes;
            for (auto& c : cfgs) {
                nodes.push_back(std::make_unique<Node>(c));
                nodes.back()->start();
            }
            if (numNodes > 1) std::this_thread::sleep_for(std::chrono::milliseconds(350));

            std::mutex latMtx;
            std::vector<double> lats;
            std::atomic<size_t> errors{0};

            auto w0 = std::chrono::steady_clock::now();
            std::vector<std::thread> workers;
            for (int i = 0; i < concTxns; ++i) {
                workers.emplace_back([&, i]() {
                    Node& coord = *nodes[static_cast<size_t>(i % numNodes)];
                    auto t0 = std::chrono::steady_clock::now();
                    TxnId tid = coord.beginTransaction();
                    std::string k1 = "m_key_" + std::to_string(i);
                    std::string k2 = "m_key_" + std::to_string(i + 1000);
                    bool ok = coord.put(tid, k1, "v1").ok &&
                              coord.put(tid, k2, "v2").ok &&
                              coord.commitTransaction(tid);
                    auto t1 = std::chrono::steady_clock::now();
                    if (ok) {
                        std::lock_guard<std::mutex> lk(latMtx);
                        lats.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                    } else {
                        coord.abortTransaction(tid, "bench err");
                        errors.fetch_add(1);
                    }
                });
            }
            for (auto& w : workers) w.join();
            auto w1 = std::chrono::steady_clock::now();

            double wall = std::chrono::duration<double>(w1 - w0).count();
            printRow("Nodes=" + std::to_string(numNodes) + " ConcTxns=" + std::to_string(concTxns),
                     computeStats(lats, wall, errors.load()));

            for (auto& n : nodes) n->stop();
        }
    }

    std::filesystem::remove_all(dir);
    return 0;
}

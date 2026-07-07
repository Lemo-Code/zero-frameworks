/**
 * @file test_rpc_qps.cc
 * @brief RPC QPS benchmark — persistent connections, configurable concurrency (gtest)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/rpc/proto/rpc.pb.h"
#include "zero/rpc/rpc_channel.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

// ---- Configuration (read from env) ----
static std::string g_host = "127.0.0.1";
static int         g_port        = 17001;
static int         g_connections = 30;
static int         g_durationSec = 10;   // 0 = use g_maxRequests instead
static int64_t     g_maxRequests = 0;    // 0 = use g_durationSec instead
static int         g_warmup      = 100;  // warmup requests before measurement

// ---- Globals ----
static std::atomic<int64_t> g_total{0};
static std::atomic<int64_t> g_errors{0};
static std::atomic<bool>     g_running{true};

// ---- Helpers ----

static int64_t nowUs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

static int envInt(const char* name, int def) {
    const char* val = std::getenv(name);
    return val ? std::stoi(val) : def;
}

static int64_t envInt64(const char* name, int64_t def) {
    const char* val = std::getenv(name);
    return val ? std::stoll(val) : def;
}

static std::string envStr(const char* name, const std::string& def) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : def;
}

static void loadConfig() {
    g_host        = envStr("RPC_QPS_HOST", "127.0.0.1");
    g_port        = envInt("RPC_QPS_PORT", 17001);
    g_connections = envInt("RPC_QPS_CONN", 30);
    g_durationSec = envInt("RPC_QPS_DURATION", 10);
    g_maxRequests = envInt64("RPC_QPS_REQUESTS", 0);
    g_warmup      = envInt("RPC_QPS_WARMUP", 100);
}

// Worker: persistent connection, tight loop
static void worker(int id) {
    zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);

    // Connect with retry (backoff: 50, 100, 200, 400, ...)
    bool connected = false;
    int delayMs = 50;
    for (int retry = 0; retry < 15 && g_running.load(std::memory_order_relaxed); ++retry) {
        if (ch->connect(g_host, g_port, 5000)) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        if (delayMs < 1000) delayMs *= 2;
    }
    if (!connected) {
        g_errors.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    uint64_t seq = 0;
    int consecutiveErrors = 0;
    while (g_running.load(std::memory_order_relaxed)) {
        zero::rpc::RpcEnvelope req, resp;
        req.set_request_id(((uint64_t)id << 48) | (++seq));
        req.mutable_health_check_req()->set_caller_id("qps");

        if (!ch->call(req, resp, 5000)) {
            consecutiveErrors++;
            g_errors.fetch_add(1, std::memory_order_relaxed);

            // Reconnect with backoff
            ch->close();
            int backoff = 50;
            bool reconnected = false;
            for (int retry = 0; retry < 10 && g_running.load(std::memory_order_relaxed); ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                if (ch->connect(g_host, g_port, 5000)) {
                    reconnected = true;
                    consecutiveErrors = 0;
                    break;
                }
                if (backoff < 1000) backoff *= 2;
            }
            if (!reconnected) break;  // give up
            continue;
        }

        consecutiveErrors = 0;

        // Verify response
        if (!resp.health_check_resp().ok()) {
            g_errors.fetch_add(1, std::memory_order_relaxed);
        }

        g_total.fetch_add(1, std::memory_order_relaxed);

        // Check if we've reached the target
        if (g_maxRequests > 0 && g_total.load(std::memory_order_relaxed) >= g_maxRequests)
            break;
    }

    ch->close();
}

// ---- Test ----

TEST(RpcQps, Benchmark) {
    loadConfig();

    // ---- Warmup ----
    if (g_warmup > 0) {
        std::cerr << "Warmup: " << g_warmup << " requests..." << std::endl;
        zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
        if (!ch->connect(g_host, g_port, 3000)) {
            GTEST_SKIP() << "RPC server not available for QPS benchmark";
            return;
        }
        for (int i = 0; i < g_warmup; ++i) {
            zero::rpc::RpcEnvelope req, resp;
            req.set_request_id((uint64_t)i);
            req.mutable_health_check_req()->set_caller_id("warmup");
            ASSERT_TRUE(ch->call(req, resp, 3000))
                << "FAIL: warmup request " << i << " failed";
        }
        ch->close();
        std::cerr << "Warmup complete." << std::endl;
    }

    // ---- Start workers ----
    std::cerr << "\nStarting " << g_connections << " workers against "
              << g_host << ":" << g_port << "..." << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(g_connections);

    // Staggered start: 2 ms delay per thread avoids accept-queue overflow
    for (int i = 0; i < g_connections; ++i) {
        threads.emplace_back(worker, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Wait for all workers to connect (brief grace period)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // ---- Measurement window ----
    int64_t t0 = nowUs();

    if (g_maxRequests > 0) {
        // Wait for target request count
        while (g_total.load(std::memory_order_relaxed) < g_maxRequests) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            int64_t done = g_total.load(std::memory_order_relaxed);
            if (done % 10000 == 0 || done >= g_maxRequests * 0.9) {
                double elapsed = (nowUs() - t0) / 1e6;
                std::cerr << "\r  Progress: " << done << " / " << g_maxRequests
                          << "  (" << std::fixed << std::setprecision(0)
                          << (elapsed > 0 ? done / elapsed : 0) << " req/s)  " << std::flush;
            }
        }
        std::cerr << std::endl;
    } else {
        // Run for duration
        std::cerr << "  Running for " << g_durationSec << " seconds..." << std::endl;
        for (int sec = 1; sec <= g_durationSec; ++sec) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int64_t done = g_total.load(std::memory_order_relaxed);
            double elapsed = (nowUs() - t0) / 1e6;
            std::cerr << "\r  [" << sec << "/" << g_durationSec << "s] "
                      << done << " requests  "
                      << std::fixed << std::setprecision(0)
                      << (elapsed > 0 ? done / elapsed : 0) << " req/s  "
                      << g_errors.load(std::memory_order_relaxed) << " errors  "
                      << std::flush;
        }
        std::cerr << std::endl;
    }

    int64_t t1 = nowUs();

    // ---- Stop workers ----
    g_running.store(false, std::memory_order_relaxed);
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // ---- Report ----
    double elapsedSec = (t1 - t0) / 1e6;
    int64_t total  = g_total.load(std::memory_order_relaxed);
    int64_t errors = g_errors.load(std::memory_order_relaxed);

    std::cout << "\n";
    std::cout << "========== RPC QPS Results ==========\n";
    std::cout << "  Target:       " << g_host << ":" << g_port << "\n";
    std::cout << "  Connections:  " << g_connections << "\n";
    std::cout << "  Duration:     " << std::fixed << std::setprecision(3)
              << elapsedSec << " s\n";
    std::cout << "  Total:        " << total << " requests\n";
    std::cout << "  Errors:       " << errors << "\n";
    std::cout << "  QPS:          ";
    if (elapsedSec > 0 && total > 0) {
        double qps = total / elapsedSec;
        double avgLatUs = (elapsedSec / total) * 1e6 * g_connections;
        std::cout << std::fixed << std::setprecision(0) << qps << " req/s\n";
        std::cout << "  Avg latency:  " << std::fixed << std::setprecision(0)
                  << avgLatUs << " us (concurrency-adjusted)\n";
    } else {
        std::cout << "N/A\n";
    }
    std::cout << "=======================================\n";

    // If too many errors, report as test info (but don't fail — this is a benchmark)
    if (errors > total / 100) {
        ADD_FAILURE() << "Error rate exceeds 1%: " << errors << " errors out of " << total;
    }
}

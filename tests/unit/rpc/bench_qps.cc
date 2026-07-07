/**
 * @file bench_qps.cc
 * @brief Self-contained RPC QPS benchmark
 *
 * Starts a local RPC server, then floods it with concurrent clients and reports QPS.
 *
 * Usage:
 *   bench_qps [connections=30] [duration_sec=5]
 *
 * Env overrides:
 *   QPS_CONN   – number of concurrent connections
 *   QPS_SEC    – benchmark duration in seconds
 */

#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"
#include "zero/rpc/rpc_channel.h"
#include "zero/rpc/rpc_server.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

static std::atomic<int64_t> g_total{0};
static std::atomic<int64_t> g_errors{0};
static std::atomic<bool>     g_running{true};

static int64_t nowUs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

static int envInt(const char* name, int def) {
    const char* val = std::getenv(name);
    return val ? std::stoi(val) : def;
}

static void worker(const std::string& host, int port, int id) {
    zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);

    bool connected = false;
    int delayMs = 50;
    for (int retry = 0; retry < 15 && g_running.load(); ++retry) {
        if (ch->connect(host, port, 5000)) {
            connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        if (delayMs < 1000) delayMs *= 2;
    }
    if (!connected) {
        std::cerr << "  worker " << id << " failed to connect" << std::endl;
        g_errors.fetch_add(1);
        return;
    }

    uint64_t seq = 0;
    while (g_running.load()) {
        zero::rpc::RpcEnvelope req, resp;
        req.set_request_id(((uint64_t)id << 48) | (++seq));
        req.mutable_health_check_req()->set_caller_id("qps");

        if (!ch->call(req, resp, 5000)) {
            g_errors.fetch_add(1);
            // Reconnect
            ch->close();
            int backoff = 50;
            bool reconnected = false;
            for (int r = 0; r < 10 && g_running.load(); ++r) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
                if (ch->connect(host, port, 5000)) {
                    reconnected = true;
                    break;
                }
                if (backoff < 1000) backoff *= 2;
            }
            if (!reconnected) break;
            continue;
        }

        if (!resp.health_check_resp().ok()) {
            g_errors.fetch_add(1);
        }
        g_total.fetch_add(1);
    }
    ch->close();
}

int main(int argc, char* argv[]) {
    int connections = envInt("QPS_CONN", argc > 1 ? std::stoi(argv[1]) : 30);
    int durationSec = envInt("QPS_SEC", argc > 2 ? std::stoi(argv[2]) : 5);

    // Suppress framework logs
    ZERO_LOG_ROOT()->setLevel(zero::LogLevel::ERROR);

    int port = 17010;
    std::string host = "127.0.0.1";

    // ---- Start local RPC server ----
    std::cout << "Starting RPC server on " << host << ":" << port << "..." << std::endl;

    auto dispatch = [](const zero::rpc::RpcEnvelope& req, zero::rpc::RpcEnvelope& resp) {
        auto* hcr = resp.mutable_health_check_resp();
        hcr->set_ok(true);
        hcr->set_node_id("bench");
        hcr->set_role(zero::rpc::NodeRole::MASTER);
        hcr->set_replication_offset(0);
    };

    zero::IOManager iom(4, false, "bench-iom");
    zero::rpc::RpcServer::ptr srv(
        new zero::rpc::RpcServer(dispatch, &iom, &iom, &iom));

    auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(port));
    if (!srv->bind(addr)) {
        std::cerr << "FATAL: bind failed" << std::endl;
        return 1;
    }
    srv->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ---- Warmup ----
    std::cout << "Warmup (100 requests)..." << std::endl;
    {
        zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
        if (!ch->connect(host, port, 3000)) {
            std::cerr << "FATAL: warmup connect failed" << std::endl;
            srv->stop();
            iom.stop();
            return 1;
        }
        for (int i = 0; i < 100; ++i) {
            zero::rpc::RpcEnvelope req, resp;
            req.set_request_id(i);
            req.mutable_health_check_req()->set_caller_id("warmup");
            ch->call(req, resp, 3000);
        }
        ch->close();
    }

    // ---- Start workers ----
    std::cout << "\nLaunching " << connections << " workers for "
              << durationSec << " seconds...\n" << std::endl;

    std::vector<std::thread> threads;
    threads.reserve(connections);
    for (int i = 0; i < connections; ++i) {
        threads.emplace_back(worker, host, port, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // stagger
    }

    // ---- Measurement ----
    std::cout << std::fixed << std::setprecision(0);
    int64_t t0 = nowUs();

    int64_t prev = 0;
    for (int sec = 1; sec <= durationSec; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int64_t cur = g_total.load();
        std::cout << "  [" << sec << "/" << durationSec << "s]  "
                  << (cur - prev) << " req/s  (total: " << cur
                  << ", errors: " << g_errors.load() << ")" << std::endl;
        prev = cur;
    }

    int64_t t1 = nowUs();
    double elapsedSec = (t1 - t0) / 1e6;

    // ---- Stop ----
    g_running.store(false);
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // Wait for server-side fibers to drain.
    // The RPC server's handleClient fibers must detect EOF (client close)
    // and exit before we tear down the listening socket, otherwise their
    // pending I/O events prevent the IOManager from reaching stopping().
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    srv->stop();
    iom.stop();

    // ---- Report ----
    int64_t total  = g_total.load();
    int64_t errors = g_errors.load();

    std::cout << "\n";
    std::cout << "========== QPS Results ==========\n";
    std::cout << "  Connections:  " << connections << "\n";
    std::cout << "  Duration:     " << std::fixed << std::setprecision(2)
              << elapsedSec << " s\n";
    std::cout << "  Total:        " << total << " requests\n";
    std::cout << "  Errors:       " << errors << "\n";
    std::cout << "  Avg QPS:      " << std::setprecision(0)
              << (elapsedSec > 0 ? total / elapsedSec : 0) << " req/s\n";
    if (total > 0 && connections > 0) {
        double avgLatUs = (elapsedSec / total) * 1e6 * connections;
        std::cout << "  Avg latency:  " << avgLatUs << " us (per-conn)\n";
    }
    std::cout << "==================================\n";

    return 0;
}

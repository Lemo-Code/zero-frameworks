/**
 * @file qps_client.cc
 * @brief 示例程序 - qps_client
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

struct BenchConfig {
    std::string host = "127.0.0.1";
    int port = 8020;
    int connections = 100;
    int duration_sec = 10;
    int payload_size = 64;
};

struct BenchStats {
    std::atomic<uint64_t> requests{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> bytes{0};
};

static bool connect_server(int fd, const std::string& host, int port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        return false;
    }
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
        return false;
    }
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    timeval tv{};
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return true;
}

static bool read_exact(int fd, void* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, (char*)buf + off, len - off, 0);
        if (n <= 0) {
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

static bool send_exact(int fd, const void* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, (const char*)buf + off, len - off, 0);
        if (n <= 0) {
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

static void worker(const BenchConfig& cfg, BenchStats& stats, std::atomic<bool>& running) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        stats.errors.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!connect_server(fd, cfg.host, cfg.port)) {
        stats.errors.fetch_add(1, std::memory_order_relaxed);
        close(fd);
        return;
    }

    std::string payload(cfg.payload_size, 'Q');
    std::vector<char> resp(cfg.payload_size);

    while (running.load(std::memory_order_relaxed)) {
        if (!send_exact(fd, payload.data(), payload.size())) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (!read_exact(fd, resp.data(), resp.size())) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        stats.requests.fetch_add(1, std::memory_order_relaxed);
        stats.bytes.fetch_add((uint64_t)(payload.size() * 2), std::memory_order_relaxed);
    }
    close(fd);
}

static void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "  -h host         server host (default 127.0.0.1)\n"
        << "  -p port         server port (default 8020)\n"
        << "  -c connections  concurrent connections (default 100)\n"
        << "  -d seconds      test duration (default 10)\n"
        << "  -s payload      payload bytes (default 64)\n";
}

int main(int argc, char** argv) {
    BenchConfig cfg;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            cfg.port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            cfg.connections = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            cfg.duration_sec = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-s") && i + 1 < argc) {
            cfg.payload_size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }

    if (cfg.connections <= 0 || cfg.duration_sec <= 0 || cfg.payload_size <= 0) {
        std::cerr << "invalid arguments\n";
        usage(argv[0]);
        return 1;
    }

    BenchStats stats;
    std::atomic<bool> running{true};
    std::vector<std::thread> threads;
    threads.reserve(cfg.connections);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < cfg.connections; ++i) {
        threads.emplace_back([&cfg, &stats, &running]() {
            worker(cfg, stats, running);
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(cfg.duration_sec));
    running.store(false, std::memory_order_relaxed);
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    uint64_t req = stats.requests.load();
    uint64_t err = stats.errors.load();
    uint64_t bytes = stats.bytes.load();
    double qps = req / elapsed;
    double mbps = (bytes * 8.0 / 1024 / 1024) / elapsed;

    std::cout << "======== QPS Benchmark Result ========\n"
              << "Target:      " << cfg.host << ":" << cfg.port << "\n"
              << "Connections: " << cfg.connections << "\n"
              << "Duration:    " << cfg.duration_sec << " s (actual " << elapsed << " s)\n"
              << "Payload:     " << cfg.payload_size << " bytes\n"
              << "Requests:    " << req << "\n"
              << "Errors:      " << err << "\n"
              << "QPS:         " << (uint64_t)qps << " req/s\n"
              << "Throughput:  " << mbps << " Mbps\n"
              << "======================================\n";

    return err > 0 ? 1 : 0;
}

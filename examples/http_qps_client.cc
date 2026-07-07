/**
 * @file http_qps_client.cc
 * @brief 示例程序 - http_qps_client
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
#include <strings.h>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

struct BenchConfig {
    std::string host = "127.0.0.1";
    int port = 8022;
    int connections = 100;
    int duration_sec = 10;
    int payload_size = 128;
    std::string path = "/echo";
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

static bool parse_content_length(const char* hdr, size_t hdr_len, size_t* out) {
    for (size_t i = 0; i + 14 < hdr_len; ++i) {
        if (strncasecmp(hdr + i, "content-length:", 15) == 0) {
            const char* v = hdr + i + 15;
            const char* end = hdr + hdr_len;
            while (v < end && (*v == ' ' || *v == '\t')) {
                ++v;
            }
            size_t cl = 0;
            while (v < end && *v >= '0' && *v <= '9') {
                cl = cl * 10 + (size_t)(*v - '0');
                ++v;
            }
            *out = cl;
            return true;
        }
    }
    return false;
}

static bool recv_http_response(int fd, size_t expect_body, size_t* wire_bytes) {
    char hdr[4096];
    size_t total = 0;
    size_t hdr_len = 0;
    while (total < sizeof(hdr)) {
        ssize_t n = recv(fd, hdr + total, sizeof(hdr) - total, 0);
        if (n <= 0) {
            return false;
        }
        total += (size_t)n;
        const char* marker = (const char*)memmem(hdr, total, "\r\n\r\n", 4);
        if (!marker) {
            continue;
        }
        hdr_len = (size_t)(marker - hdr) + 4;
        break;
    }
    if (hdr_len < 12 || memcmp(hdr, "HTTP/1.", 7) != 0) {
        return false;
    }

    size_t body_len = 0;
    if (!parse_content_length(hdr, hdr_len, &body_len) || body_len != expect_body) {
        return false;
    }

    size_t in_hdr = total > hdr_len ? total - hdr_len : 0;
    std::vector<char> body(body_len);
    if (in_hdr > 0) {
        if (in_hdr > body_len) {
            return false;
        }
        memcpy(body.data(), hdr + hdr_len, in_hdr);
    }
    if (!read_exact(fd, body.data() + in_hdr, body_len - in_hdr)) {
        return false;
    }
    if (wire_bytes) {
        *wire_bytes = hdr_len + body_len;
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

    const std::string body(cfg.payload_size, 'H');
    const std::string req_hdr =
        "POST " + cfg.path + " HTTP/1.1\r\n"
        "Host: " + cfg.host + "\r\n"
        "Connection: keep-alive\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n";

    while (running.load(std::memory_order_relaxed)) {
        if (!send_exact(fd, req_hdr.data(), req_hdr.size())) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        if (!send_exact(fd, body.data(), body.size())) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        size_t wire = 0;
        if (!recv_http_response(fd, (size_t)cfg.payload_size, &wire)) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        stats.requests.fetch_add(1, std::memory_order_relaxed);
        stats.bytes.fetch_add((uint64_t)(req_hdr.size() + body.size() + wire),
                              std::memory_order_relaxed);
    }
    close(fd);
}

static void usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n"
        << "  -h host         server host (default 127.0.0.1)\n"
        << "  -p port         server port (default 8022)\n"
        << "  -c connections  concurrent connections (default 100)\n"
        << "  -d seconds      test duration (default 10)\n"
        << "  -s payload      POST body bytes (default 128)\n"
        << "  -u path         request path (default /echo)\n";
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
        } else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
            cfg.path = argv[++i];
        } else if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
    }

    if (cfg.connections <= 0 || cfg.duration_sec <= 0 || cfg.payload_size <= 0 || cfg.path.empty()) {
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

    std::cout << "======== HTTP QPS Benchmark Result ========\n"
              << "Target:      " << cfg.host << ":" << cfg.port << cfg.path << "\n"
              << "Connections: " << cfg.connections << "\n"
              << "Duration:    " << cfg.duration_sec << " s (actual " << elapsed << " s)\n"
              << "Payload:     " << cfg.payload_size << " bytes (POST body)\n"
              << "Requests:    " << req << "\n"
              << "Errors:      " << err << "\n"
              << "QPS:         " << (uint64_t)qps << " req/s\n"
              << "Throughput:  " << mbps << " Mbps\n"
              << "=========================================\n";

    return err > 0 ? 1 : 0;
}

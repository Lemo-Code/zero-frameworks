/**
 * @file tcp_echo_client.cc
 * @brief bench2 - TCP Echo QPS 客户端 (raw socket, 多连接并发)
 *
 * 用法: ./tcp_echo_client -p 8020 -c 500 -d 10 -s 64
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

struct Config {
    std::string host = "127.0.0.1";
    int port = 8020, conns = 100, duration = 10, payload = 64;
};

struct Stats {
    std::atomic<uint64_t> req{0}, err{0}, bytes{0};
};

static int do_connect(const Config& c) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(c.port);
    if (inet_pton(AF_INET, c.host.c_str(), &addr.sin_addr) <= 0) { close(fd); return -1; }
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static bool send_all(int fd, const void* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, (const char*)buf + off, len - off, 0);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

static bool recv_all(int fd, void* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, (char*)buf + off, len - off, 0);
        if (n <= 0) return false;
        off += n;
    }
    return true;
}

static void worker(const Config& c, Stats& s, std::atomic<bool>& running) {
    int fd = do_connect(c);
    if (fd < 0) { s.err++; return; }

    std::string payload(c.payload, 'A');
    std::vector<char> rsp(c.payload);

    while (running.load(std::memory_order_relaxed)) {
        if (!send_all(fd, payload.data(), payload.size())) { s.err++; break; }
        if (!recv_all(fd, rsp.data(), rsp.size())) { s.err++; break; }
        s.req++;
        s.bytes += (uint64_t)(payload.size() * 2);
    }
    close(fd);
}

int main(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") && i+1 < argc) c.host = argv[++i];
        else if (!strcmp(argv[i], "-p") && i+1 < argc) c.port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-c") && i+1 < argc) c.conns = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) c.duration = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i+1 < argc) c.payload = atoi(argv[++i]);
    }

    Stats s;
    std::atomic<bool> running{true};
    std::vector<std::thread> threads;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < c.conns; i++)
        threads.emplace_back([&]() { worker(c, s, running); });

    std::this_thread::sleep_for(std::chrono::seconds(c.duration));
    running = false;
    for (auto& t : threads) if (t.joinable()) t.join();
    auto t1 = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    uint64_t req = s.req, err = s.err, bytes = s.bytes;
    double qps = req / elapsed;
    double mbps = (bytes * 8.0 / 1e6) / elapsed;

    std::cout << "\n============ TCP Echo QPS ============\n"
              << "Target:      " << c.host << ":" << c.port << "\n"
              << "Connections: " << c.conns << "\n"
              << "Duration:    " << c.duration << "s (actual " << elapsed << "s)\n"
              << "Payload:     " << c.payload << " bytes\n"
              << "Requests:    " << req << "\n"
              << "Errors:      " << err << "\n"
              << "QPS:         " << (uint64_t)qps << " req/s\n"
              << "Throughput:  " << mbps << " Mbps\n"
              << "======================================\n\n";
    return err > 0 ? 1 : 0;
}

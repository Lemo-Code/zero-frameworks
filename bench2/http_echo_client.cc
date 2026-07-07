/**
 * @file http_echo_client.cc
 * @brief bench2 - HTTP POST Echo QPS 客户端 (raw socket HTTP/1.1)
 *
 * 用法: ./http_echo_client -p 8022 -c 500 -d 10 -s 64
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

struct Config {
    std::string host = "127.0.0.1";
    int port = 8022, conns = 100, duration = 10, payload = 64;
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
    timeval tv{5, 0};
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

// Read until we find "\r\n\r\n" (headers end), parse Content-Length, read body
static bool recv_http_response(int fd, std::string& body) {
    char buf[16384];
    std::string data;
    size_t header_end = std::string::npos;

    while (header_end == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        data.append(buf, n);
        header_end = data.find("\r\n\r\n");
    }

    // Parse Content-Length
    size_t cl_pos = data.find("Content-Length:");
    if (cl_pos == std::string::npos) cl_pos = data.find("content-length:");
    int content_len = 0;
    if (cl_pos != std::string::npos) {
        size_t cl_end = data.find("\r\n", cl_pos);
        std::string cl_str = data.substr(cl_pos + 15, cl_end - cl_pos - 15);
        content_len = std::stoi(cl_str);
    }

    size_t body_start = header_end + 4;
    body = data.substr(body_start);

    // Read remaining body bytes if needed
    while (body.size() < (size_t)content_len) {
        ssize_t n = recv(fd, buf, std::min(sizeof(buf), content_len - body.size()), 0);
        if (n <= 0) return false;
        body.append(buf, n);
    }
    return true;
}

static std::string build_request(const std::string& payload) {
    std::ostringstream oss;
    oss << "POST /echo HTTP/1.1\r\n"
        << "Host: 127.0.0.1\r\n"
        << "Content-Type: application/octet-stream\r\n"
        << "Content-Length: " << payload.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "\r\n"
        << payload;
    return oss.str();
}

static void worker(const Config& c, Stats& s, std::atomic<bool>& running) {
    int fd = do_connect(c);
    if (fd < 0) { s.err++; return; }

    std::string payload(c.payload, 'A');
    std::string req = build_request(payload);

    while (running.load(std::memory_order_relaxed)) {
        if (!send_all(fd, req.data(), req.size())) { s.err++; break; }
        std::string rsp_body;
        if (!recv_http_response(fd, rsp_body)) { s.err++; break; }
        // Verify echo: body should match
        if (rsp_body.size() != payload.size()) { s.err++; }
        s.req++;
        s.bytes += (uint64_t)(req.size() + rsp_body.size());
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

    std::cout << "\n============ HTTP Echo QPS ============\n"
              << "Target:      " << c.host << ":" << c.port << "/echo\n"
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

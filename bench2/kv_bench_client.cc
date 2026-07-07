/**
 * @file kv_bench_client.cc
 * @brief bench2 - KV (Redis 协议) QPS 客户端 (raw socket RESP)
 *
 * 用法: ./kv_bench_client -p 6379 -c 500 -d 10 -s 64
 *       使用 redis-benchmark 兼容的命令格式
 */

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <random>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

struct Config {
    std::string host = "127.0.0.1";
    int port = 6379, conns = 100, duration = 10, payload = 64;
    std::string cmd = "GET";  // GET, SET, PING
    int pipeline = 1;          // 管道批量 (1=no pipeline)
    int key_space = 10000;     // key 空间大小
};

struct Stats {
    std::atomic<uint64_t> req{0}, err{0};
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

// RESP: *2\r\n$3\r\nGET\r\n$<len>\r\n<key>\r\n
static std::string build_get(const std::string& key) {
    std::ostringstream oss;
    oss << "*2\r\n$3\r\nGET\r\n$" << key.size() << "\r\n" << key << "\r\n";
    return oss.str();
}

static std::string build_set(const std::string& key, const std::string& val) {
    std::ostringstream oss;
    oss << "*3\r\n$3\r\nSET\r\n$" << key.size() << "\r\n" << key << "\r\n"
        << "$" << val.size() << "\r\n" << val << "\r\n";
    return oss.str();
}

static std::string build_ping() {
    return "*1\r\n$4\r\nPING\r\n";
}

// Read RESP response. For GET: $<len>\r\n<value>\r\n. For SET/PING: +OK\r\n
// We need to read the full response based on RESP type
static bool recv_resp(int fd) {
    char buf[65536];
    // Read first line to determine response type
    ssize_t n = recv(fd, buf, 1, MSG_PEEK);
    if (n <= 0) return false;

    // Read until \r\n
    std::string line;
    char ch;
    while (line.size() < 65536) {
        if (recv(fd, &ch, 1, 0) <= 0) return false;
        line += ch;
        if (line.size() >= 2 && line[line.size()-2] == '\r' && line[line.size()-1] == '\n')
            break;
    }

    char type = line[0];
    if (type == '+') return true;   // Simple string: +OK\r\n
    if (type == ':') return true;   // Integer: :1\r\n
    if (type == '-') return false;  // Error: -ERR...\r\n

    if (type == '$') {
        // Bulk string: $<len>\r\n<data>\r\n
        int len = std::stoi(line.substr(1, line.size() - 3));
        if (len == -1) return true;  // null bulk
        // Read <len> bytes + \r\n
        std::string data;
        data.resize(len + 2);
        size_t off = 0;
        while (off < data.size()) {
            ssize_t r = recv(fd, &data[off], data.size() - off, 0);
            if (r <= 0) return false;
            off += r;
        }
        return true;
    }

    if (type == '*') {
        // Array
        int count = std::stoi(line.substr(1, line.size() - 3));
        for (int i = 0; i < count; i++) {
            if (!recv_resp(fd)) return false;
        }
        return true;
    }

    return false;
}

static void worker(const Config& c, Stats& s, std::atomic<bool>& running) {
    int fd = do_connect(c);
    if (fd < 0) { s.err++; return; }

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, c.key_space - 1);
    std::string val(c.payload, 'V');

    // Pre-build pipeline request
    std::string pipe_req;
    for (int i = 0; i < c.pipeline; i++) {
        std::string key = "bench:" + std::to_string(dist(rng));
        if (c.cmd == "GET") pipe_req += build_get(key);
        else if (c.cmd == "SET") pipe_req += build_set(key, val);
        else if (c.cmd == "PING") pipe_req += build_ping();
    }

    // We re-generate keys every pipeline batch for realistic distribution
    while (running.load(std::memory_order_relaxed)) {
        // Re-generate pipeline request with new random keys
        if (c.cmd != "PING" && c.pipeline > 0) {
            pipe_req.clear();
            for (int i = 0; i < c.pipeline; i++) {
                std::string key = "bench:" + std::to_string(dist(rng));
                if (c.cmd == "GET") pipe_req += build_get(key);
                else if (c.cmd == "SET") pipe_req += build_set(key, val);
            }
        }

        if (!send_all(fd, pipe_req.data(), pipe_req.size())) { s.err++; break; }

        // Read all responses
        bool ok = true;
        for (int i = 0; i < c.pipeline; i++) {
            if (!recv_resp(fd)) { ok = false; break; }
        }
        if (!ok) { s.err++; break; }
        s.req += c.pipeline;
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
        else if (!strcmp(argv[i], "-t") && i+1 < argc) c.cmd = argv[++i];
        else if (!strcmp(argv[i], "-P") && i+1 < argc) c.pipeline = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-k") && i+1 < argc) c.key_space = atoi(argv[++i]);
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
    uint64_t req = s.req, err = s.err;
    double qps = req / elapsed;

    std::cout << "\n============ KV (" << c.cmd << ") QPS ============\n"
              << "Target:      " << c.host << ":" << c.port << "\n"
              << "Connections: " << c.conns << "\n"
              << "Pipeline:    " << c.pipeline << "\n"
              << "Duration:    " << c.duration << "s (actual " << elapsed << "s)\n"
              << "Payload:     " << c.payload << " bytes\n"
              << "Requests:    " << req << "\n"
              << "Errors:      " << err << "\n"
              << "QPS:         " << (uint64_t)qps << " rps\n"
              << "======================================\n\n";
    return err > 0 ? 1 : 0;
}

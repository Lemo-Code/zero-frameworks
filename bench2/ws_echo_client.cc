/**
 * @file ws_echo_client.cc
 * @brief bench2 - WebSocket Echo QPS 客户端 (raw socket, 最小 WS 实现)
 *
 * 用法: ./ws_echo_client -p 8024 -c 500 -d 10 -s 64
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
    int port = 8024, conns = 100, duration = 10, payload = 64;
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

// Generate random 16-byte WebSocket key for handshake
static std::string gen_ws_key() {
    static const char* CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 63);
    std::string k(16, 0);
    for (int i = 0; i < 16; i++) k[i] = CHARS[dist(rng)];
    // Base64 encode 16 bytes -> 24 chars
    // Simplification: just use 24 random base64 chars
    std::string key(24, 0);
    for (int i = 0; i < 24; i++) key[i] = CHARS[dist(rng)];
    return key;
}

static bool ws_handshake(int fd, const std::string& host, int port) {
    std::string key = gen_ws_key();
    std::ostringstream oss;
    oss << "GET /echo HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";
    std::string req = oss.str();

    if (!send_all(fd, req.data(), req.size())) return false;

    // Read response until \r\n\r\n
    char buf[4096];
    std::string rsp;
    while (rsp.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        rsp.append(buf, n);
    }
    return rsp.find("101 ") != std::string::npos;
}

// Build a masked WebSocket text frame
// Frame format: FIN(1) + RSV(3) + OPCODE(4) | MASK(1) + LEN(7) | [EXT_LEN] | MASK_KEY(4) | DATA
static std::string build_ws_frame(const std::string& data, bool text = true) {
    std::string frame;
    frame += (char)(0x80 | (text ? 0x01 : 0x02));  // FIN + opcode

    size_t len = data.size();
    uint8_t mask_bit = 0x80;

    // Generate random mask
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 255);
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) mask[i] = (uint8_t)dist(rng);

    if (len < 126) {
        frame += (char)(mask_bit | (uint8_t)len);
    } else if (len <= 65535) {
        frame += (char)(mask_bit | 126);
        frame += (char)((len >> 8) & 0xFF);
        frame += (char)(len & 0xFF);
    } else {
        frame += (char)(mask_bit | 127);
        for (int i = 7; i >= 0; i--)
            frame += (char)((len >> (i * 8)) & 0xFF);
    }

    frame.append((char*)mask, 4);

    // Append masked data
    for (size_t i = 0; i < len; i++)
        frame += (char)(data[i] ^ mask[i % 4]);

    return frame;
}

// Read a WebSocket frame, return payload
static bool recv_ws_frame(int fd, std::string& payload) {
    char hdr[2];
    if (recv(fd, hdr, 2, 0) != 2) return false;

    uint8_t opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126) {
        char ext[2];
        if (recv(fd, ext, 2, 0) != 2) return false;
        len = ((uint16_t)(uint8_t)ext[0] << 8) | (uint8_t)ext[1];
    } else if (len == 127) {
        char ext[8];
        if (recv(fd, ext, 8, 0) != 8) return false;
        len = 0;
        for (int i = 0; i < 8; i++)
            len = (len << 8) | (uint8_t)ext[i];
    }

    // Read payload
    std::string data;
    data.resize(len);
    size_t off = 0;
    while (off < len) {
        ssize_t n = recv(fd, &data[off], len - off, 0);
        if (n <= 0) return false;
        off += n;
    }

    // If close frame, return false
    if (opcode == 0x08) return false;

    payload = data;
    return true;
}

static void worker(const Config& c, Stats& s, std::atomic<bool>& running) {
    int fd = do_connect(c);
    if (fd < 0) { s.err++; return; }
    if (!ws_handshake(fd, c.host, c.port)) { s.err++; close(fd); return; }

    std::string send_payload(c.payload, 'W');
    std::string frame = build_ws_frame(send_payload);

    while (running.load(std::memory_order_relaxed)) {
        if (!send_all(fd, frame.data(), frame.size())) { s.err++; break; }
        std::string rsp_payload;
        if (!recv_ws_frame(fd, rsp_payload)) { s.err++; break; }
        s.req++;
        s.bytes += (uint64_t)(send_payload.size() * 2);
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

    std::cout << "\n============ WS Echo QPS =============\n"
              << "Target:      " << c.host << ":" << c.port << "/echo\n"
              << "Connections: " << c.conns << "\n"
              << "Duration:    " << c.duration << "s (actual " << elapsed << "s)\n"
              << "Payload:     " << c.payload << " bytes\n"
              << "Requests:    " << req << "\n"
              << "Errors:      " << err << "\n"
              << "QPS:         " << (uint64_t)qps << " msg/s\n"
              << "Throughput:  " << mbps << " Mbps\n"
              << "======================================\n\n";
    return err > 0 ? 1 : 0;
}

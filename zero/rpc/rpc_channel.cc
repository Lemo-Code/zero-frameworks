/**
 * @file rpc_channel.cc
 * @brief RPC 模块 - rpc_channel
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_channel.h"

#include "zero/core/io/hook.h"    // for connect_f / send_f / recv_f / socket_f / close_f / fcntl_f
#include "zero/core/log/log.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("rpc");

void RpcChannel::encodeBe32(uint32_t val, uint8_t* out) {
    out[0] = (val >> 24) & 0xFF;
    out[1] = (val >> 16) & 0xFF;
    out[2] = (val >> 8) & 0xFF;
    out[3] = val & 0xFF;
}

uint32_t RpcChannel::decodeBe32(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

namespace {

// Raw non-blocking connect with poll() timeout.
// Uses connect_f (real libc connect) — bypasses the hook entirely so this
// is safe to call from any thread regardless of t_hook_enable state.
static bool rawConnect(int fd, const struct sockaddr* addr, socklen_t addrlen, int timeoutMs) {
    int flags = fcntl_f(fd, F_GETFL, 0);
    if (flags < 0) return false;
    fcntl_f(fd, F_SETFL, flags | O_NONBLOCK);

    int n = connect_f(fd, addr, addrlen);
    if (n == 0) {
        fcntl_f(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        fcntl_f(fd, F_SETFL, flags);
        return false;
    }

    // poll() is NOT hooked, ::poll is always the real syscall
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    int ret = ::poll(&pfd, 1, timeoutMs);
    if (ret <= 0) {
        fcntl_f(fd, F_SETFL, flags);
        return false;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt_f(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        fcntl_f(fd, F_SETFL, flags);
        errno = error;
        return false;
    }

    fcntl_f(fd, F_SETFL, flags);
    return true;
}

} // namespace

RpcChannel::RpcChannel() : m_fd(-1) {}
RpcChannel::~RpcChannel() { close(); }

bool RpcChannel::connect(const std::string& host, int port, int timeoutMs) {
    zero::Mutex::Lock lock(m_mutex);

    // Close any existing connection (real close_f, no hook)
    if (m_fd >= 0) {
        close_f(m_fd);
        m_fd = -1;
    }

    // Resolve host — use AI_NUMERICHOST to avoid DNS hang for IP literals
    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;

    std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        ZERO_LOG_ERROR(g_logger) << "RpcChannel: resolve failed for "
            << host << ":" << port << " err=" << gai_strerror(rc);
        return false;
    }

    int fd = socket_f(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ZERO_LOG_ERROR(g_logger) << "RpcChannel: socket failed errno=" << errno;
        freeaddrinfo(res);
        return false;
    }

    int one = 1;
    setsockopt_f(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    bool ok = rawConnect(fd, res->ai_addr, res->ai_addrlen, timeoutMs);
    freeaddrinfo(res);

    if (!ok) {
        ZERO_LOG_ERROR(g_logger) << "RpcChannel: connect failed " << host << ":" << port
            << " errno=" << errno << " (" << strerror(errno) << ")";
        close_f(fd);
        return false;
    }

    m_fd = fd;
    return true;
}

bool RpcChannel::isConnected() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_fd >= 0;
}

void RpcChannel::close() {
    zero::Mutex::Lock lock(m_mutex);
    if (m_fd >= 0) {
        close_f(m_fd);
        m_fd = -1;
    }
}

bool RpcChannel::readFull(void* buf, size_t len, int timeoutMs) {
    uint8_t* dst = static_cast<uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, timeoutMs);
        if (ret <= 0) return false;

        ssize_t n = recv_f(m_fd, dst, remaining, 0);
        if (n > 0) {
            dst += n;
            remaining -= n;
            continue;
        }
        if (n == 0) return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
        return false;
    }
    return true;
}

bool RpcChannel::writeFull(const void* buf, size_t len, int timeoutMs) {
    const uint8_t* src = static_cast<const uint8_t*>(buf);
    size_t remaining = len;
    while (remaining > 0) {
        struct pollfd pfd;
        pfd.fd = m_fd;
        pfd.events = POLLOUT;
        int ret = ::poll(&pfd, 1, timeoutMs);
        if (ret <= 0) return false;

        ssize_t n = send_f(m_fd, src, remaining, MSG_NOSIGNAL);
        if (n > 0) {
            src += n;
            remaining -= n;
            continue;
        }
        if (n == 0) return false;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
        return false;
    }
    return true;
}

// Helper: close the fd under the mutex, but only if it hasn't been replaced
// by a concurrent connect().
static void closeIfCurrent(zero::Mutex& mutex, int& m_fd, int expectedFd) {
    zero::Mutex::Lock lock(mutex);
    if (m_fd == expectedFd && m_fd >= 0) {
        close_f(m_fd);
        m_fd = -1;
    }
}

bool RpcChannel::call(const RpcEnvelope& request, RpcEnvelope& response, int timeoutMs) {
    m_mutex.lock();
    if (m_fd < 0) { m_mutex.unlock(); return false; }

    std::string wire;
    if (!request.SerializeToString(&wire)) {
        m_mutex.unlock();
        ZERO_LOG_ERROR(g_logger) << "RpcChannel: serialize request failed";
        return false;
    }

    uint8_t lenBuf[4];
    encodeBe32(static_cast<uint32_t>(wire.size()), lenBuf);
    m_mutex.unlock();

    if (!writeFull(lenBuf, 4, timeoutMs) || !writeFull(wire.data(), wire.size(), timeoutMs)) {
        m_mutex.lock();
        if (m_fd >= 0) { close_f(m_fd); m_fd = -1; }
        m_mutex.unlock();
        return false;
    }

    uint8_t respLenBuf[4];
    if (!readFull(respLenBuf, 4, timeoutMs)) {
        m_mutex.lock();
        if (m_fd >= 0) { close_f(m_fd); m_fd = -1; }
        m_mutex.unlock();
        return false;
    }
    uint32_t respLen = decodeBe32(respLenBuf);
    if (respLen == 0 || respLen > 64 * 1024 * 1024) {
        m_mutex.lock();
        if (m_fd >= 0) { close_f(m_fd); m_fd = -1; }
        m_mutex.unlock();
        return false;
    }

    std::string respWire(respLen, '\0');
    if (!readFull(&respWire[0], respLen, timeoutMs)) {
        m_mutex.lock();
        if (m_fd >= 0) { close_f(m_fd); m_fd = -1; }
        m_mutex.unlock();
        return false;
    }

    if (!response.ParseFromString(respWire)) {
        ZERO_LOG_ERROR(g_logger) << "RpcChannel: parse response failed";
        return false;
    }
    return true;
}

bool RpcChannel::send(const RpcEnvelope& msg) {
    m_mutex.lock();
    if (m_fd < 0) { m_mutex.unlock(); return false; }

    std::string wire;
    if (!msg.SerializeToString(&wire)) { m_mutex.unlock(); return false; }

    uint8_t lenBuf[4];
    encodeBe32(static_cast<uint32_t>(wire.size()), lenBuf);
    m_mutex.unlock();

    if (!writeFull(lenBuf, 4, 3000) || !writeFull(wire.data(), wire.size(), 3000)) {
        m_mutex.lock();
        if (m_fd >= 0) { close_f(m_fd); m_fd = -1; }
        m_mutex.unlock();
        return false;
    }
    return true;
}

// ---- RpcChannelPool ----

static RpcChannelPool::ptr g_channel_pool;

RpcChannelPool::ptr RpcChannelPool::GetInstance() {
    if (!g_channel_pool) g_channel_pool.reset(new RpcChannelPool);
    return g_channel_pool;
}

RpcChannel::ptr RpcChannelPool::getChannel(const std::string& host, int port) {
    std::string key = host + ":" + std::to_string(port);

    // Check the cache under the lock — fast path.
    {
        zero::Mutex::Lock lock(m_mutex);
        auto it = m_channels.find(key);
        if (it != m_channels.end() && it->second->isConnected()) return it->second;
    }

    // Connect outside the lock.  connect() can block for up to timeoutMs
    // (default 3 s); holding the pool lock during that time would stall all
    // other getChannel / removeChannel / cleanupStale / clear calls.
    RpcChannel::ptr ch(new RpcChannel);
    if (!ch->connect(host, port)) return nullptr;

    // Re-check under the lock: another thread might have created a channel
    // for the same key while we were connecting.
    {
        zero::Mutex::Lock lock(m_mutex);
        auto it = m_channels.find(key);
        if (it != m_channels.end() && it->second->isConnected()) return it->second;
        m_channels[key] = ch;
        return ch;
    }
}

void RpcChannelPool::removeChannel(const std::string& host, int port) {
    std::string key = host + ":" + std::to_string(port);
    zero::Mutex::Lock lock(m_mutex);
    auto it = m_channels.find(key);
    if (it != m_channels.end()) {
        it->second->close();
        m_channels.erase(it);
    }
}

void RpcChannelPool::cleanupStale() {
    zero::Mutex::Lock lock(m_mutex);
    for (auto it = m_channels.begin(); it != m_channels.end(); ) {
        if (!it->second->isConnected()) {
            it->second->close();
            it = m_channels.erase(it);
        } else {
            ++it;
        }
    }
}

void RpcChannelPool::clear() {
    zero::Mutex::Lock lock(m_mutex);
    for (auto& kv : m_channels) kv.second->close();
    m_channels.clear();
}

} // namespace rpc
} // namespace zero

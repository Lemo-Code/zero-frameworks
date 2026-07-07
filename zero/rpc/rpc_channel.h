/**
 * @file rpc_channel.h
 * @brief RPC client channel — raw POSIX I/O, safe for non-fiber threads
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_CHANNEL_H__
#define __ZERO_RPC_RPC_CHANNEL_H__

#include "zero/core/concurrency/mutex.h"
#include "zero/rpc/proto/rpc.pb.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace zero {
namespace rpc {

class RpcChannel {
public:
    typedef std::shared_ptr<RpcChannel> ptr;

    RpcChannel();
    ~RpcChannel();

    // Connect to remote host:port.  Blocking, must be called from non-fiber thread.
    bool connect(const std::string& host, int port, int timeoutMs = 3000);

    bool isConnected() const;

    // Send request, wait for matching response (blocking).
    bool call(const RpcEnvelope& request, RpcEnvelope& response, int timeoutMs = 3000);

    // Fire-and-forget (no response expected).
    bool send(const RpcEnvelope& msg);

    void close();

private:
    // Low-level I/O — raw syscalls, no fiber hooks.
    bool readFull(void* buf, size_t len, int timeoutMs);
    bool writeFull(const void* buf, size_t len, int timeoutMs);
    static uint32_t decodeBe32(const uint8_t* in);
    static void encodeBe32(uint32_t val, uint8_t* out);

    int m_fd = -1;
    mutable zero::Mutex m_mutex;
};

// Connection pool, keyed by "host:port"
class RpcChannelPool {
public:
    typedef std::shared_ptr<RpcChannelPool> ptr;

    static RpcChannelPool::ptr GetInstance();

    RpcChannel::ptr getChannel(const std::string& host, int port);
    void removeChannel(const std::string& host, int port);
    void cleanupStale();  // remove disconnected channels
    void clear();

private:
    RpcChannelPool() = default;

    mutable zero::Mutex m_mutex;
    std::unordered_map<std::string, RpcChannel::ptr> m_channels;
};

} // namespace rpc
} // namespace zero

#endif

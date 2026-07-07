/**
 * @file rpc_server.h
 * @brief RPC server — TcpServer-based, runs in fiber context with hooked I/O
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_SERVER_H__
#define __ZERO_RPC_RPC_SERVER_H__

#include "zero/rpc/proto/rpc.pb.h"
#include "zero/net/tcp/tcp_server.h"
#include <functional>
#include <memory>

namespace zero {
class SocketStream;

namespace rpc {

// Handler signature: inspect request.body_case(), fill response.
using RpcHandler = std::function<void(const RpcEnvelope& request, RpcEnvelope& response)>;

class RpcServer : public TcpServer {
public:
    typedef std::shared_ptr<RpcServer> ptr;

    RpcServer(RpcHandler dispatch,
              IOManager* worker = IOManager::GetThis(),
              IOManager* io_worker = IOManager::GetThis(),
              IOManager* accept_worker = IOManager::GetThis());

protected:
    void handleClient(Socket::ptr client) override;

private:
    RpcHandler m_dispatch;
};

} // namespace rpc
} // namespace zero

#endif

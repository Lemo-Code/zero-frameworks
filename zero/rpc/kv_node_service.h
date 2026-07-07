/**
 * @file kv_node_service.h
 * @brief KvNodeService RPC handler — health check, SLAVEOF, status queries
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_KV_NODE_SERVICE_H__
#define __ZERO_RPC_KV_NODE_SERVICE_H__

#include "zero/rpc/proto/rpc.pb.h"
#include <functional>
#include <memory>

namespace zero {
namespace kv {
class KvServer;
}
namespace rpc {

// Returns a handler that dispatches HealthCheck, ReplicaOf, and GetNodeStatus.
std::function<void(const RpcEnvelope& request, RpcEnvelope& response)>
MakeKvNodeHandler(std::weak_ptr<kv::KvServer> server);

} // namespace rpc
} // namespace zero

#endif

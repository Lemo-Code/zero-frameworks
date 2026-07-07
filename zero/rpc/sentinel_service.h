/**
 * @file sentinel_service.h
 * @brief SentinelService RPC handler — election and discovery
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_SENTINEL_SERVICE_H__
#define __ZERO_RPC_SENTINEL_SERVICE_H__

#include "zero/rpc/proto/rpc.pb.h"
#include <functional>
#include <memory>

namespace zero {
namespace rpc {

class SentinelManager;

std::function<void(const RpcEnvelope& request, RpcEnvelope& response)>
MakeSentinelHandler(std::shared_ptr<SentinelManager> manager);

} // namespace rpc
} // namespace zero

#endif

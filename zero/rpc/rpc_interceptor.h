/**
 * @file rpc_interceptor.h
 * @brief RPC 拦截器接口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_INTERCEPTOR_H__
#define __ZERO_RPC_RPC_INTERCEPTOR_H__

#include <memory>
#include <string>
#include "zero/rpc/proto/rpc.pb.h"
#include "zero/rpc/rpc_metadata.h"

namespace zero {
namespace rpc {

/**
 * @brief RPC 调用上下文
 */
struct RpcInvocation {
    std::string serviceName;
    std::string methodName;
    RpcEnvelope request;
    RpcEnvelope response;
    RpcMetadata metadata;
    std::string targetHost;
    int targetPort = 0;
    bool success = false;
    std::string error;
};

/**
 * @brief RPC 拦截器
 */
class RpcInterceptor {
public:
    typedef std::shared_ptr<RpcInterceptor> ptr;
    virtual ~RpcInterceptor() = default;

    /**
     * @brief 调用前拦截
     * @return false 表示中断调用
     */
    virtual bool before(RpcInvocation& invocation) { (void)invocation; return true; }

    /**
     * @brief 调用后拦截
     */
    virtual void after(RpcInvocation& invocation) { (void)invocation; }
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_RPC_INTERCEPTOR_H__

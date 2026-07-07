/**
 * @file rpc_channel_manager.h
 * @brief 基于服务发现的 RPC 通道管理器（负载均衡 + 重试 + 拦截器）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_CHANNEL_MANAGER_H__
#define __ZERO_RPC_RPC_CHANNEL_MANAGER_H__

#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <functional>
#include "zero/registry/service_discovery.h"
#include "zero/rpc/rpc_channel.h"
#include "zero/rpc/rpc_load_balancer.h"
#include "zero/rpc/rpc_circuit_breaker.h"
#include "zero/rpc/rpc_interceptor.h"
#include "zero/rpc/proto/rpc.pb.h"

namespace zero {
namespace rpc {

/**
 * @brief 重试策略
 */
struct RpcRetryPolicy {
    int maxAttempts = 3;
    int baseDelayMs = 50;
    int maxDelayMs = 1000;
    std::vector<int> retryableCodes; // 默认空表示所有非成功都重试
};

/**
 * @brief 服务级 RPC 配置
 */
struct RpcServiceConfig {
    LoadBalanceStrategy lbStrategy = LoadBalanceStrategy::RoundRobin;
    RpcRetryPolicy retryPolicy;
    RpcCircuitBreakerConfig cbConfig;
    bool metadataPropagation = true;
};

/**
 * @brief RPC 通道管理器
 */
class RpcChannelManager : public std::enable_shared_from_this<RpcChannelManager> {
public:
    typedef std::shared_ptr<RpcChannelManager> ptr;

    explicit RpcChannelManager(ServiceDiscovery::ptr discovery,
                               RpcChannelPool::ptr pool = RpcChannelPool::GetInstance());

    /**
     * @brief 注册服务配置
     */
    void registerService(const std::string& serviceName, const RpcServiceConfig& config);

    /**
     * @brief 订阅服务实例变化
     */
    void watchService(const std::string& serviceName);

    /**
     * @brief 添加全局拦截器
     */
    void addInterceptor(RpcInterceptor::ptr interceptor);

    /**
     * @brief 调用指定服务的方法
     */
    bool call(const std::string& serviceName,
              const std::string& methodName,
              const RpcEnvelope& request,
              RpcEnvelope& response,
              const RpcMetadata& metadata = RpcMetadata(),
              int timeoutMs = 3000);

    /**
     * @brief 获取当前负载均衡节点快照
     */
    std::vector<RpcNode> getNodes(const std::string& serviceName);

private:
    bool doCall(RpcInvocation& invocation,
                const RpcServiceConfig& config,
                int timeoutMs);
    int selectNode(const std::string& serviceName,
                   const RpcLbContext& ctx,
                   const RpcServiceConfig& config);

    ServiceDiscovery::ptr m_discovery;
    RpcChannelPool::ptr m_pool;
    std::mutex m_mutex;
    std::map<std::string, RpcLoadBalancer::ptr> m_loadBalancers;
    std::map<std::string, RpcCircuitBreakerManager::ptr> m_cbManagers;
    std::map<std::string, RpcServiceConfig> m_configs;
    std::vector<RpcInterceptor::ptr> m_interceptors;
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_RPC_CHANNEL_MANAGER_H__

/**
 * @file rpc_channel_manager.cc
 * @brief RPC 通道管理器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_channel_manager.h"
#include "zero/core/log/log.h"
#include <thread>
#include <chrono>

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

RpcChannelManager::RpcChannelManager(ServiceDiscovery::ptr discovery,
                                     RpcChannelPool::ptr pool)
    : m_discovery(discovery), m_pool(pool) {
}

void RpcChannelManager::registerService(const std::string& serviceName,
                                        const RpcServiceConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configs[serviceName] = config;
    if (m_loadBalancers.find(serviceName) == m_loadBalancers.end()) {
        m_loadBalancers[serviceName] = RpcLoadBalancer::create(config.lbStrategy);
    }
    if (m_cbManagers.find(serviceName) == m_cbManagers.end()) {
        m_cbManagers[serviceName] = std::make_shared<RpcCircuitBreakerManager>(config.cbConfig);
    }
}

void RpcChannelManager::watchService(const std::string& serviceName) {
    if (!m_discovery) return;
    auto self = shared_from_this();
    (void)self;
    m_discovery->watch(serviceName, [this, serviceName](const std::vector<ServiceInstance>& instances) {
        std::vector<RpcNode> nodes;
        for (const auto& inst : instances) {
            RpcNode node;
            node.id = inst.id;
            node.host = inst.host;
            node.port = inst.port;
            node.metadata = inst.metadata;
            auto it = inst.metadata.find("weight");
            if (it != inst.metadata.end()) {
                try { node.weight = std::stoi(it->second); } catch (...) {}
            }
            node.healthy = inst.healthy;
            nodes.push_back(node);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_loadBalancers.find(serviceName);
        if (it != m_loadBalancers.end()) {
            it->second->updateNodes(nodes);
        }
        ZERO_LOG_INFO(g_logger) << "RpcChannelManager updated " << serviceName
                                << " nodes=" << nodes.size();
    });
}

void RpcChannelManager::addInterceptor(RpcInterceptor::ptr interceptor) {
    if (!interceptor) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_interceptors.push_back(interceptor);
}

std::vector<RpcNode> RpcChannelManager::getNodes(const std::string& serviceName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_loadBalancers.find(serviceName);
    if (it != m_loadBalancers.end()) {
        return it->second->getNodes();
    }
    return {};
}

int RpcChannelManager::selectNode(const std::string& serviceName,
                                  const RpcLbContext& ctx,
                                  const RpcServiceConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto lbIt = m_loadBalancers.find(serviceName);
    if (lbIt == m_loadBalancers.end()) {
        m_loadBalancers[serviceName] = RpcLoadBalancer::create(config.lbStrategy);
        return -1;
    }
    return lbIt->second->selectNode(ctx);
}

bool RpcChannelManager::doCall(RpcInvocation& invocation,
                               const RpcServiceConfig& config,
                               int timeoutMs) {
    RpcLbContext ctx;
    ctx.sourceKey = invocation.metadata.traceId.empty()
                        ? std::to_string(invocation.request.request_id())
                        : invocation.metadata.traceId;
    auto it = invocation.metadata.tags.find("zone");
    if (it != invocation.metadata.tags.end()) ctx.zone = it->second;
    it = invocation.metadata.tags.find("canary");
    if (it != invocation.metadata.tags.end()) {
        ctx.canary = (it->second == "true");
        ctx.canaryTag = invocation.metadata.get("canaryTag");
    }

    int idx = selectNode(invocation.serviceName, ctx, config);
    if (idx < 0) {
        invocation.error = "no available node";
        return false;
    }

    auto nodes = getNodes(invocation.serviceName);
    if ((size_t)idx >= nodes.size()) {
        invocation.error = "node index out of range";
        return false;
    }
    const RpcNode& node = nodes[idx];
    invocation.targetHost = node.host;
    invocation.targetPort = node.port;

    // 熔断器
    std::string cbKey = invocation.serviceName + "#" + invocation.methodName;
    RpcCircuitBreakerManager::ptr cbMgr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it2 = m_cbManagers.find(invocation.serviceName);
        if (it2 != m_cbManagers.end()) cbMgr = it2->second;
    }
    if (cbMgr && !cbMgr->allowRequest(cbKey)) {
        invocation.error = "circuit breaker open";
        return false;
    }

    auto channel = m_pool->getChannel(node.host, node.port);
    if (!channel || !channel->isConnected()) {
        if (cbMgr) cbMgr->recordFailure(cbKey);
        invocation.error = "connect failed";
        return false;
    }

    bool ok = channel->call(invocation.request, invocation.response, timeoutMs);
    invocation.success = ok;
    if (!ok) {
        if (cbMgr) cbMgr->recordFailure(cbKey);
        m_pool->removeChannel(node.host, node.port);
        invocation.error = "rpc call failed";
        return false;
    }
    if (cbMgr) cbMgr->recordSuccess(cbKey);
    return true;
}

bool RpcChannelManager::call(const std::string& serviceName,
                             const std::string& methodName,
                             const RpcEnvelope& request,
                             RpcEnvelope& response,
                             const RpcMetadata& metadata,
                             int timeoutMs) {
    RpcServiceConfig config;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_configs.find(serviceName);
        if (it != m_configs.end()) config = it->second;
    }

    RpcInvocation invocation;
    invocation.serviceName = serviceName;
    invocation.methodName = methodName;
    invocation.request = request;
    invocation.metadata = metadata;

    // 拦截器 before
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& interceptor : m_interceptors) {
            if (!interceptor->before(invocation)) {
                response = invocation.response;
                return false;
            }
        }
    }

    int attempts = 0;
    int delayMs = config.retryPolicy.baseDelayMs;
    bool ok = false;
    while (attempts < config.retryPolicy.maxAttempts) {
        ++attempts;
        ok = doCall(invocation, config, timeoutMs);
        if (ok) break;
        if (attempts >= config.retryPolicy.maxAttempts) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        delayMs = std::min(delayMs * 2, config.retryPolicy.maxDelayMs);
    }

    response = invocation.response;

    // 拦截器 after
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& interceptor : m_interceptors) {
            interceptor->after(invocation);
        }
    }

    return ok;
}

} // namespace rpc
} // namespace zero

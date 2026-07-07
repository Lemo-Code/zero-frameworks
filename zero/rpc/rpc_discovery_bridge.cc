/**
 * @file rpc_discovery_bridge.cc
 * @brief 服务发现 -> RPC 负载均衡器联动桥接实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_discovery_bridge.h"
#include "zero/core/log/log.h"

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

RpcDiscoveryBridge::ptr RpcDiscoveryBridge::bind(ServiceDiscovery::ptr discovery,
                                                 RpcLoadBalancer::ptr lb,
                                                 const std::string& serviceName) {
    if(!discovery || !lb) {
        return nullptr;
    }
    auto bridge = std::shared_ptr<RpcDiscoveryBridge>(
        new RpcDiscoveryBridge(discovery, lb, serviceName));
    bridge->init();
    return bridge;
}

RpcDiscoveryBridge::RpcDiscoveryBridge(ServiceDiscovery::ptr discovery,
                                       RpcLoadBalancer::ptr lb,
                                       const std::string& serviceName)
    : m_discovery(discovery), m_lb(lb), m_serviceName(serviceName) {
}

void RpcDiscoveryBridge::init() {
    auto self = shared_from_this();
    m_discovery->watch(m_serviceName,
        [self](const std::vector<ServiceInstance>& instances) {
            self->onInstancesChanged(instances);
        });
    // 初始同步一次
    auto initial = m_discovery->discover(m_serviceName);
    onInstancesChanged(initial);
}

RpcDiscoveryBridge::~RpcDiscoveryBridge() {
    if(m_discovery) {
        m_discovery->unwatch(m_serviceName);
    }
}

void RpcDiscoveryBridge::onInstancesChanged(const std::vector<ServiceInstance>& instances) {
    auto nodes = convertToRpcNodes(instances);
    m_lb->updateNodes(nodes);
    ZERO_LOG_INFO(g_logger) << "RpcDiscoveryBridge updated " << m_serviceName
                            << " nodes=" << nodes.size();
}

std::vector<RpcNode> RpcDiscoveryBridge::convertToRpcNodes(const std::vector<ServiceInstance>& instances) {
    std::vector<RpcNode> nodes;
    nodes.reserve(instances.size());
    for(auto& inst : instances) {
        RpcNode node;
        node.id = inst.id;
        node.host = inst.host;
        node.port = inst.port;
        node.healthy = inst.healthy;
        node.metadata = inst.metadata;
        auto it = inst.metadata.find("weight");
        if(it != inst.metadata.end()) {
            try {
                node.weight = std::stoi(it->second);
            } catch(...) {
                node.weight = 1;
            }
        } else {
            node.weight = 1;
        }
        nodes.push_back(node);
    }
    return nodes;
}

} // namespace rpc
} // namespace zero

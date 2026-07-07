/**
 * @file rpc_discovery_bridge.h
 * @brief 服务发现 -> RPC 负载均衡器联动桥接
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_DISCOVERY_BRIDGE_H__
#define __ZERO_RPC_RPC_DISCOVERY_BRIDGE_H__

#include <memory>
#include <string>
#include <vector>
#include "zero/registry/service_discovery.h"
#include "zero/rpc/rpc_load_balancer.h"

namespace zero {
namespace rpc {

/**
 * @brief 将 ServiceDiscovery 的实例变更实时同步到 RpcLoadBalancer
 */
class RpcDiscoveryBridge : public std::enable_shared_from_this<RpcDiscoveryBridge> {
public:
    typedef std::shared_ptr<RpcDiscoveryBridge> ptr;

    /**
     * @brief 创建桥接并立即订阅服务变更
     */
    static RpcDiscoveryBridge::ptr bind(ServiceDiscovery::ptr discovery,
                                        RpcLoadBalancer::ptr lb,
                                        const std::string& serviceName);

    ~RpcDiscoveryBridge();

private:
    RpcDiscoveryBridge(ServiceDiscovery::ptr discovery,
                       RpcLoadBalancer::ptr lb,
                       const std::string& serviceName);

    void init();
    void onInstancesChanged(const std::vector<ServiceInstance>& instances);

    static std::vector<RpcNode> convertToRpcNodes(const std::vector<ServiceInstance>& instances);

    ServiceDiscovery::ptr m_discovery;
    RpcLoadBalancer::ptr m_lb;
    std::string m_serviceName;
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_RPC_DISCOVERY_BRIDGE_H__

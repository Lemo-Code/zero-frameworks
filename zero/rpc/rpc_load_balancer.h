/**
 * @file rpc_load_balancer.h
 * @brief RPC 负载均衡器
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_LOAD_BALANCER_H__
#define __ZERO_RPC_LOAD_BALANCER_H__

#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <map>
#include "zero/core/concurrency/mutex.h"

namespace zero {
namespace rpc {

/**
 * @brief RPC 服务端节点
 */
struct RpcNode {
    std::string id;
    std::string host;
    int port = 0;
    int32_t weight = 1;
    bool healthy = true;
    uint64_t activeRequests = 0;
    uint64_t totalRequests = 0;
    uint64_t failedRequests = 0;
    std::map<std::string, std::string> metadata; ///< zone, canary, version 等
};

/**
 * @brief 负载均衡上下文
 */
struct RpcLbContext {
    std::string sourceKey;   ///< 一致性哈希源地址 / 请求标识
    std::string zone;        ///< 调用方可用区
    bool canary = false;     ///< 是否命中灰度
    std::string canaryTag;   ///< 灰度标签
};

/**
 * @brief 负载均衡策略
 */
enum class LoadBalanceStrategy {
    RoundRobin,
    Random,
    LeastConnections,
    WeightedRoundRobin,
    WeightedRandom,
    ConsistentHash,
    Canary,
    SameZone
};

/**
 * @brief RPC 负载均衡器
 */
class RpcLoadBalancer {
public:
    typedef std::shared_ptr<RpcLoadBalancer> ptr;

    static RpcLoadBalancer::ptr create(LoadBalanceStrategy strategy = LoadBalanceStrategy::RoundRobin);

    void updateNodes(const std::vector<RpcNode>& nodes);

    /**
     * @brief 选择一个可用节点
     * @return 节点索引，无可用节点返回 -1
     */
    int selectNode();

    /**
     * @brief 带上下文选择节点（支持一致性哈希、灰度、同可用区）
     */
    int selectNode(const RpcLbContext& ctx);

    /**
     * @brief 根据失败率动态调整权重
     */
    void adjustWeightsByFailureRate(double failureRateThreshold = 0.5,
                                     int32_t minWeight = 1,
                                     int32_t maxWeight = 100);

    /**
     * @brief 标记请求开始
     */
    void markRequestStart(int index);

    /**
     * @brief 标记请求结束
     * @param[in] success 是否成功
     */
    void markRequestEnd(int index, bool success);

    /**
     * @brief 标记节点健康状态
     */
    void setNodeHealthy(int index, bool healthy);

    std::vector<RpcNode> getNodes() const;

private:
    explicit RpcLoadBalancer(LoadBalanceStrategy strategy);

    int selectRoundRobin();
    int selectRandom();
    int selectLeastConnections();
    int selectWeightedRoundRobin();
    int selectWeightedRandom();
    int selectConsistentHash(const RpcLbContext& ctx);
    int selectCanary(const RpcLbContext& ctx);
    int selectSameZone(const RpcLbContext& ctx);

    LoadBalanceStrategy m_strategy;
    std::vector<std::shared_ptr<struct InternalRpcNode>> m_nodes;
    mutable Mutex m_mutex;
    std::atomic<size_t> m_rrIndex{0};
    std::atomic<size_t> m_wrrIndex{0};
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_LOAD_BALANCER_H__

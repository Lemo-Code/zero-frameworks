/**
 * @file rpc_load_balancer.cc
 * @brief RPC 负载均衡器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_load_balancer.h"
#include <cstdlib>
#include <limits>

namespace zero {
namespace rpc {

struct InternalRpcNode {
    std::string id;
    std::string host;
    int port = 0;
    std::atomic<int32_t> weight{1};
    std::atomic<bool> healthy{true};
    std::atomic<uint64_t> activeRequests{0};
    std::atomic<uint64_t> totalRequests{0};
    std::atomic<uint64_t> failedRequests{0};
    std::map<std::string, std::string> metadata;
};

RpcLoadBalancer::RpcLoadBalancer(LoadBalanceStrategy strategy)
    : m_strategy(strategy) {
}

RpcLoadBalancer::ptr RpcLoadBalancer::create(LoadBalanceStrategy strategy) {
    return std::shared_ptr<RpcLoadBalancer>(new RpcLoadBalancer(strategy));
}

void RpcLoadBalancer::updateNodes(const std::vector<RpcNode>& nodes) {
    Mutex::Lock lock(m_mutex);
    m_nodes.clear();
    for(auto& n : nodes) {
        auto node = std::make_shared<InternalRpcNode>();
        node->id = n.id;
        node->host = n.host;
        node->port = n.port;
        node->weight = n.weight;
        node->healthy = n.healthy;
        node->metadata = n.metadata;
        m_nodes.push_back(node);
    }
}

int RpcLoadBalancer::selectNode() {
    RpcLbContext ctx;
    return selectNode(ctx);
}

int RpcLoadBalancer::selectNode(const RpcLbContext& ctx) {
    switch(m_strategy) {
        case LoadBalanceStrategy::RoundRobin:
            return selectRoundRobin();
        case LoadBalanceStrategy::Random:
            return selectRandom();
        case LoadBalanceStrategy::LeastConnections:
            return selectLeastConnections();
        case LoadBalanceStrategy::WeightedRoundRobin:
            return selectWeightedRoundRobin();
        case LoadBalanceStrategy::WeightedRandom:
            return selectWeightedRandom();
        case LoadBalanceStrategy::ConsistentHash:
            return selectConsistentHash(ctx);
        case LoadBalanceStrategy::Canary:
            return selectCanary(ctx);
        case LoadBalanceStrategy::SameZone:
            return selectSameZone(ctx);
        default:
            return selectRoundRobin();
    }
}

int RpcLoadBalancer::selectRoundRobin() {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    size_t start = m_rrIndex++;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        size_t idx = (start + i) % m_nodes.size();
        if(m_nodes[idx]->healthy.load()) {
            return (int)idx;
        }
    }
    return -1;
}

int RpcLoadBalancer::selectRandom() {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    std::vector<int> healthyIdx;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(m_nodes[i]->healthy.load()) {
            healthyIdx.push_back((int)i);
        }
    }
    if(healthyIdx.empty()) return -1;
    return healthyIdx[rand() % healthyIdx.size()];
}

int RpcLoadBalancer::selectLeastConnections() {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    int best = -1;
    uint64_t minActive = std::numeric_limits<uint64_t>::max();
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(!m_nodes[i]->healthy.load()) continue;
        uint64_t active = m_nodes[i]->activeRequests.load();
        if(active < minActive) {
            minActive = active;
            best = (int)i;
        }
    }
    return best;
}

int RpcLoadBalancer::selectWeightedRoundRobin() {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    int totalWeight = 0;
    for(auto& node : m_nodes) {
        if(node->healthy.load()) {
            totalWeight += node->weight.load();
        }
    }
    if(totalWeight <= 0) return -1;

    int target = (m_wrrIndex++ % totalWeight) + 1;
    int current = 0;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(!m_nodes[i]->healthy.load()) continue;
        current += m_nodes[i]->weight.load();
        if(current >= target) {
            return (int)i;
        }
    }
    return -1;
}

void RpcLoadBalancer::markRequestStart(int index) {
    Mutex::Lock lock(m_mutex);
    if(index < 0 || (size_t)index >= m_nodes.size()) return;
    m_nodes[index]->activeRequests++;
    m_nodes[index]->totalRequests++;
}

void RpcLoadBalancer::markRequestEnd(int index, bool success) {
    Mutex::Lock lock(m_mutex);
    if(index < 0 || (size_t)index >= m_nodes.size()) return;
    if(m_nodes[index]->activeRequests > 0) {
        m_nodes[index]->activeRequests--;
    }
    if(!success) {
        m_nodes[index]->failedRequests++;
    }
}

void RpcLoadBalancer::setNodeHealthy(int index, bool healthy) {
    Mutex::Lock lock(m_mutex);
    if(index < 0 || (size_t)index >= m_nodes.size()) return;
    m_nodes[index]->healthy = healthy;
}


int RpcLoadBalancer::selectWeightedRandom() {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    int totalWeight = 0;
    for(auto& node : m_nodes) {
        if(node->healthy.load()) {
            totalWeight += node->weight.load();
        }
    }
    if(totalWeight <= 0) return -1;
    int target = rand() % totalWeight + 1;
    int current = 0;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(!m_nodes[i]->healthy.load()) continue;
        current += m_nodes[i]->weight.load();
        if(current >= target) {
            return (int)i;
        }
    }
    return -1;
}

static uint64_t fnv1aHash(const std::string& s) {
    uint64_t h = 14695981039346656037ULL;
    for(char c : s) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
    return h;
}

int RpcLoadBalancer::selectConsistentHash(const RpcLbContext& ctx) {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    std::vector<int> healthyIdx;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(m_nodes[i]->healthy.load()) healthyIdx.push_back((int)i);
    }
    if(healthyIdx.empty()) return -1;
    uint64_t hash = fnv1aHash(ctx.sourceKey.empty() ? "" : ctx.sourceKey);
    return healthyIdx[hash % healthyIdx.size()];
}

int RpcLoadBalancer::selectCanary(const RpcLbContext& ctx) {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    std::vector<int> canaryIdx;
    std::vector<int> stableIdx;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(!m_nodes[i]->healthy.load()) continue;
        auto it = m_nodes[i]->metadata.find("canary");
        bool isCanary = (it != m_nodes[i]->metadata.end() && it->second == "true");
        if(isCanary) canaryIdx.push_back((int)i);
        else stableIdx.push_back((int)i);
    }
    if(ctx.canary && !canaryIdx.empty()) {
        uint64_t hash = fnv1aHash(ctx.canaryTag.empty() ? ctx.sourceKey : ctx.canaryTag);
        return canaryIdx[hash % canaryIdx.size()];
    }
    if(!stableIdx.empty()) {
        uint64_t hash = fnv1aHash(ctx.sourceKey.empty() ? "" : ctx.sourceKey);
        return stableIdx[hash % stableIdx.size()];
    }
    if(!canaryIdx.empty()) {
        return canaryIdx[0];
    }
    return -1;
}

int RpcLoadBalancer::selectSameZone(const RpcLbContext& ctx) {
    Mutex::Lock lock(m_mutex);
    if(m_nodes.empty()) return -1;
    std::vector<int> sameZoneIdx;
    std::vector<int> otherIdx;
    for(size_t i = 0; i < m_nodes.size(); ++i) {
        if(!m_nodes[i]->healthy.load()) continue;
        auto it = m_nodes[i]->metadata.find("zone");
        bool sameZone = (it != m_nodes[i]->metadata.end() && it->second == ctx.zone);
        if(sameZone) sameZoneIdx.push_back((int)i);
        else otherIdx.push_back((int)i);
    }
    std::vector<int>& candidates = !sameZoneIdx.empty() ? sameZoneIdx : otherIdx;
    if(candidates.empty()) return -1;
    uint64_t hash = fnv1aHash(ctx.sourceKey.empty() ? "" : ctx.sourceKey);
    return candidates[hash % candidates.size()];
}

void RpcLoadBalancer::adjustWeightsByFailureRate(double failureRateThreshold,
                                                  int32_t minWeight,
                                                  int32_t maxWeight) {
    Mutex::Lock lock(m_mutex);
    for(auto& node : m_nodes) {
        uint64_t total = node->totalRequests.load();
        uint64_t failed = node->failedRequests.load();
        if(total == 0) continue;
        double rate = static_cast<double>(failed) / static_cast<double>(total);
        int32_t newWeight = node->weight.load();
        if(rate > failureRateThreshold) {
            newWeight = std::max(minWeight, newWeight / 2);
        } else if(rate < failureRateThreshold / 2.0) {
            newWeight = std::min(maxWeight, newWeight + 1);
        }
        node->weight = newWeight;
    }
}

std::vector<RpcNode> RpcLoadBalancer::getNodes() const {
    Mutex::Lock lock(m_mutex);
    std::vector<RpcNode> result;
    for(auto& node : m_nodes) {
        RpcNode n;
        n.id = node->id;
        n.host = node->host;
        n.port = node->port;
        n.weight = node->weight.load();
        n.healthy = node->healthy.load();
        n.activeRequests = node->activeRequests.load();
        n.totalRequests = node->totalRequests.load();
        n.failedRequests = node->failedRequests.load();
        n.metadata = node->metadata;
        result.push_back(n);
    }
    return result;
}

} // namespace rpc
} // namespace zero

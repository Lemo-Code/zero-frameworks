/**
 * @file test_service_governance.cc
 * @brief Service governance tests: service discovery client, RPC load balancer enhancements, RPC circuit breaker (gtest)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include "zero/registry/service_registry.h"
#include "zero/registry/service_discovery.h"
#include "zero/rpc/rpc_load_balancer.h"
#include "zero/rpc/rpc_circuit_breaker.h"

using namespace zero;
using namespace zero::rpc;

// ============================================================================
// MockRegistry
// ============================================================================
class MockRegistry : public ServiceRegistry {
public:
    typedef std::shared_ptr<MockRegistry> ptr;

    bool registerService(const ServiceInstance& instance) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_services[instance.serviceName].push_back(instance);
        return true;
    }

    bool deregisterService(const std::string& serviceId) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_services) {
            auto& vec = kv.second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&serviceId](const ServiceInstance& i) { return i.id == serviceId; }), vec.end());
        }
        return true;
    }

    bool heartbeat(const std::string& serviceId) override {
        (void)serviceId;
        return true;
    }

    std::vector<ServiceInstance> discover(const std::string& serviceName) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_services[serviceName];
    }

    void watch(const std::string& serviceName, ServiceWatcher watcher) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchers[serviceName] = watcher;
    }

    void unwatch(const std::string& serviceName) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchers.erase(serviceName);
    }

    void notify(const std::string& serviceName, const std::vector<ServiceInstance>& instances) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_watchers.find(serviceName);
        if (it != m_watchers.end() && it->second) {
            it->second(instances);
        }
    }

private:
    std::mutex m_mutex;
    std::map<std::string, std::vector<ServiceInstance>> m_services;
    std::map<std::string, ServiceWatcher> m_watchers;
};

// ============================================================================
// ServiceDiscovery
// ============================================================================

TEST(ServiceGovernance, ServiceDiscovery) {
    auto registry = std::make_shared<MockRegistry>();
    HealthCheckConfig hc;
    hc.enabled = false;
    ServiceDiscovery discovery(registry, hc);

    ServiceInstance i1;
    i1.id = "user-1";
    i1.serviceName = "user-service";
    i1.host = "127.0.0.1";
    i1.port = 8001;
    i1.version = "v1";
    i1.metadata["zone"] = "bj";
    i1.healthy = true;

    ServiceInstance i2;
    i2.id = "user-2";
    i2.serviceName = "user-service";
    i2.host = "127.0.0.1";
    i2.port = 8002;
    i2.version = "v2";
    i2.metadata["zone"] = "sh";
    i2.healthy = true;

    registry->registerService(i1);
    registry->registerService(i2);

    auto all = discovery.discover("user-service");
    EXPECT_EQ(all.size(), 2u);

    auto v1 = discovery.discoverByVersion("user-service", "v1");
    EXPECT_EQ(v1.size(), 1u);
    EXPECT_EQ(v1[0].id, "user-1");

    auto bj = discovery.discoverByMetadata("user-service", "zone", "bj");
    EXPECT_EQ(bj.size(), 1u);
    EXPECT_EQ(bj[0].id, "user-1");

    // Mark unhealthy
    discovery.setInstanceHealthy("user-1", false);
    auto filtered = discovery.discover("user-service");
    EXPECT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].id, "user-2");
}

TEST(ServiceGovernance, ServiceDiscoveryWatch) {
    auto registry = std::make_shared<MockRegistry>();
    HealthCheckConfig hc;
    hc.enabled = false;
    ServiceDiscovery discovery(registry, hc);

    int notifyCount = 0;
    discovery.watch("order-service", [&notifyCount](const std::vector<ServiceInstance>& instances) {
        (void)instances;
        ++notifyCount;
    });

    ServiceInstance i1;
    i1.id = "order-1";
    i1.serviceName = "order-service";
    i1.host = "127.0.0.1";
    i1.port = 9001;
    i1.healthy = true;
    registry->registerService(i1);
    registry->notify("order-service", {i1});

    EXPECT_EQ(notifyCount, 1);
}

// ============================================================================
// RPC LoadBalancer
// ============================================================================

TEST(ServiceGovernance, RpcLbWeightedRandom) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::WeightedRandom);
    lb->updateNodes({
        {"n1", "127.0.0.1", 8001, 1, true, 0, 0, 0, {}},
        {"n2", "127.0.0.1", 8002, 9, true, 0, 0, 0, {}}
    });

    int n1Count = 0, n2Count = 0;
    for (int i = 0; i < 1000; ++i) {
        int idx = lb->selectNode();
        EXPECT_TRUE(idx == 0 || idx == 1);
        if (idx == 0) ++n1Count;
        else ++n2Count;
    }
    // Weight 9:1, n2 should be much larger than n1
    EXPECT_GT(n2Count, n1Count * 3);
}

TEST(ServiceGovernance, RpcLbConsistentHash) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::ConsistentHash);
    lb->updateNodes({
        {"n1", "127.0.0.1", 8001, 1, true},
        {"n2", "127.0.0.1", 8002, 1, true},
        {"n3", "127.0.0.1", 8003, 1, true}
    });

    RpcLbContext ctx;
    ctx.sourceKey = "user_12345";
    int idx1 = lb->selectNode(ctx);
    int idx2 = lb->selectNode(ctx);
    EXPECT_EQ(idx1, idx2);
    EXPECT_GE(idx1, 0);
    EXPECT_LT(idx1, 3);
}

TEST(ServiceGovernance, RpcLbCanary) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::Canary);
    lb->updateNodes({
        {"n1", "127.0.0.1", 8001, 1, true, 0, 0, 0, {{"canary", "true"}}},
        {"n2", "127.0.0.1", 8002, 1, true, 0, 0, 0, {{"canary", "false"}}},
        {"n3", "127.0.0.1", 8003, 1, true, 0, 0, 0, {}}
    });

    RpcLbContext canaryCtx;
    canaryCtx.canary = true;
    canaryCtx.canaryTag = "user_vip";
    int cidx = lb->selectNode(canaryCtx);
    EXPECT_EQ(cidx, 0); // Only n1 is canary

    RpcLbContext stableCtx;
    stableCtx.canary = false;
    int sidx = lb->selectNode(stableCtx);
    EXPECT_TRUE(sidx == 1 || sidx == 2);
}

TEST(ServiceGovernance, RpcLbSameZone) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::SameZone);
    lb->updateNodes({
        {"n1", "127.0.0.1", 8001, 1, true, 0, 0, 0, {{"zone", "bj"}}},
        {"n2", "127.0.0.1", 8002, 1, true, 0, 0, 0, {{"zone", "sh"}}},
        {"n3", "127.0.0.1", 8003, 1, true, 0, 0, 0, {{"zone", "bj"}}}
    });

    RpcLbContext ctx;
    ctx.zone = "bj";
    ctx.sourceKey = "req_1";
    int zidx = lb->selectNode(ctx);
    EXPECT_TRUE(zidx == 0 || zidx == 2);
}

TEST(ServiceGovernance, RpcLbDynamicWeight) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::WeightedRoundRobin);
    lb->updateNodes({
        {"n1", "127.0.0.1", 8001, 10, true},
        {"n2", "127.0.0.1", 8002, 10, true}
    });

    lb->markRequestStart(0);
    lb->markRequestEnd(0, false);
    lb->markRequestStart(0);
    lb->markRequestEnd(0, false);
    lb->adjustWeightsByFailureRate(0.5, 1, 100);

    auto nodes = lb->getNodes();
    EXPECT_LT(nodes[0].weight, 10); // n1 failure rate is high, weight should decrease
}

// ============================================================================
// RPC CircuitBreaker
// ============================================================================

TEST(ServiceGovernance, RpcCircuitBreaker) {
    RpcCircuitBreakerConfig config;
    config.failureThreshold = 3;
    config.successThreshold = 2;
    config.openDurationMs = 100;
    RpcCircuitBreaker cb(config);

    EXPECT_EQ(cb.state(), RpcCircuitState::Closed);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(cb.allowRequest());
        cb.recordFailure();
    }
    EXPECT_EQ(cb.state(), RpcCircuitState::Open);
    EXPECT_FALSE(cb.allowRequest());

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(cb.allowRequest()); // Enter half-open
    cb.recordSuccess();
    EXPECT_EQ(cb.state(), RpcCircuitState::HalfOpen);
    EXPECT_TRUE(cb.allowRequest());
    cb.recordSuccess();
    EXPECT_EQ(cb.state(), RpcCircuitState::Closed);
}

TEST(ServiceGovernance, RpcCircuitBreakerManager) {
    RpcCircuitBreakerManager mgr;
    EXPECT_TRUE(mgr.allowRequest("user-service#GetUser"));
    mgr.recordFailure("user-service#GetUser");
    mgr.recordFailure("user-service#GetUser");
    mgr.recordFailure("user-service#GetUser");
    mgr.recordFailure("user-service#GetUser");
    mgr.recordFailure("user-service#GetUser");
    EXPECT_FALSE(mgr.allowRequest("user-service#GetUser"));

    auto states = mgr.getStates();
    EXPECT_TRUE(states.find("user-service#GetUser") != states.end());
}

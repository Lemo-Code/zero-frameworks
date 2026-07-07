/**
 * @file test_rpc_etcd_integration.cc
 * @brief RPC + etcd service discovery real integration test (gtest)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include <iostream>
#include <thread>
#include <chrono>
#include "zero/registry/etcd_registry.h"
#include "zero/registry/service_discovery.h"
#include "zero/rpc/rpc_load_balancer.h"
#include "zero/rpc/rpc_discovery_bridge.h"

using namespace zero;
using namespace zero::rpc;

static bool pingEtcd(EtcdRegistry::ptr registry) {
    ServiceInstance probe;
    probe.id = "probe";
    probe.serviceName = "__probe__";
    probe.host = "127.0.0.1";
    probe.port = 1;
    return registry->registerService(probe);
}

TEST(RpcEtcdIntegration, DiscoveryAndLoadBalance) {
    std::vector<std::string> endpoints = {"http://127.0.0.1:2379"};
    auto registry = std::make_shared<EtcdRegistry>(endpoints);

    if (!pingEtcd(registry)) {
        GTEST_SKIP() << "etcd not available at 127.0.0.1:2379";
    }

    auto discovery = std::make_shared<ServiceDiscovery>(registry);
    discovery->start();

    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::RoundRobin);
    auto bridge = RpcDiscoveryBridge::bind(discovery, lb, "TestService");
    ASSERT_TRUE(bridge) << "Failed to create discovery bridge";

    // Register first instance
    ServiceInstance inst1;
    inst1.id = "TestService-1";
    inst1.serviceName = "TestService";
    inst1.host = "127.0.0.1";
    inst1.port = 8001;
    inst1.metadata["weight"] = "10";
    inst1.healthy = true;
    ASSERT_TRUE(registry->registerService(inst1)) << "registerService inst1 failed";

    // Wait for service discovery sync
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    auto nodes = lb->getNodes();
    ASSERT_EQ(nodes.size(), 1u) << "Expected 1 node, got " << nodes.size();
    EXPECT_EQ(nodes[0].port, 8001);
    EXPECT_EQ(nodes[0].weight, 10);

    int idx = lb->selectNode();
    EXPECT_GE(idx, 0) << "selectNode failed";

    // Register second instance
    ServiceInstance inst2;
    inst2.id = "TestService-2";
    inst2.serviceName = "TestService";
    inst2.host = "127.0.0.1";
    inst2.port = 8002;
    inst2.healthy = true;
    ASSERT_TRUE(registry->registerService(inst2)) << "registerService inst2 failed";

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    nodes = lb->getNodes();
    EXPECT_EQ(nodes.size(), 2u) << "Expected 2 nodes, got " << nodes.size();

    // Deregister first instance
    registry->deregisterService(inst1.id);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    nodes = lb->getNodes();
    EXPECT_EQ(nodes.size(), 1u) << "Expected 1 node after deregister, got " << nodes.size();

    discovery->stop();
}

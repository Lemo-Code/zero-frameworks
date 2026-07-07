/**
 * @file test_rpc_load_balancer.cc
 * @brief RPC 负载均衡器测试 — gtest style
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/rpc/rpc_load_balancer.h"

using namespace zero;
using namespace zero::rpc;

TEST(RpcLoadBalancer, RoundRobin) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::RoundRobin);
    std::vector<RpcNode> nodes;
    nodes.push_back({"n1", "127.0.0.1", 8001});
    nodes.push_back({"n2", "127.0.0.1", 8002});
    lb->updateNodes(nodes);

    int idx1 = lb->selectNode();
    int idx2 = lb->selectNode();
    int idx3 = lb->selectNode();

    EXPECT_EQ(idx1, 0);
    EXPECT_EQ(idx2, 1);
    EXPECT_EQ(idx3, 0);
}

TEST(RpcLoadBalancer, Random) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::Random);
    std::vector<RpcNode> nodes;
    nodes.push_back({"n1", "127.0.0.1", 8001});
    nodes.push_back({"n2", "127.0.0.1", 8002});
    lb->updateNodes(nodes);

    int idx = lb->selectNode();
    EXPECT_TRUE(idx == 0 || idx == 1);
}

TEST(RpcLoadBalancer, LeastConnections) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::LeastConnections);
    std::vector<RpcNode> nodes;
    nodes.push_back({"n1", "127.0.0.1", 8001});
    nodes.push_back({"n2", "127.0.0.1", 8002});
    lb->updateNodes(nodes);

    int idx = lb->selectNode();
    EXPECT_TRUE(idx == 0 || idx == 1);

    lb->markRequestStart(idx);
    int idx2 = lb->selectNode();
    EXPECT_NE(idx2, idx);
}

TEST(RpcLoadBalancer, WeightedRoundRobin) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::WeightedRoundRobin);
    std::vector<RpcNode> nodes;
    RpcNode n1, n2;
    n1.id = "n1"; n1.host = "127.0.0.1"; n1.port = 8001; n1.weight = 3;
    n2.id = "n2"; n2.host = "127.0.0.1"; n2.port = 8002; n2.weight = 1;
    nodes.push_back(n1);
    nodes.push_back(n2);
    lb->updateNodes(nodes);

    int countN1 = 0;
    for(int i = 0; i < 4; ++i) {
        if(lb->selectNode() == 0) countN1++;
    }
    EXPECT_EQ(countN1, 3);
}

TEST(RpcLoadBalancer, UnhealthyNode) {
    auto lb = RpcLoadBalancer::create(LoadBalanceStrategy::RoundRobin);
    std::vector<RpcNode> nodes;
    nodes.push_back({"n1", "127.0.0.1", 8001});
    nodes.push_back({"n2", "127.0.0.1", 8002});
    lb->updateNodes(nodes);

    lb->setNodeHealthy(0, false);
    for(int i = 0; i < 5; ++i) {
        EXPECT_EQ(lb->selectNode(), 1);
    }
}

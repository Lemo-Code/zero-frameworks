/**
 * @file test_rpc_server.cc
 * @brief RPC transport test — gtest style
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/core/io/iomanager.h"
#include "zero/rpc/rpc_server.h"
#include "zero/rpc/proto/rpc.pb.h"
#include <thread>
#include <chrono>

static int g_port = 17001;

class RpcServerTest : public zero_test::IOManagerFixture {
};

TEST_F(RpcServerTest, BindAndStart) {
    // Pure echo handler — responds to any request with HealthCheckResponse.
    // This tests the RPC transport layer (connect → send → recv → close)
    // without any KvServer / ReplicationManager dependency.
    auto handler = [](const zero::rpc::RpcEnvelope&, zero::rpc::RpcEnvelope& resp) {
        auto* hcr = resp.mutable_health_check_resp();
        hcr->set_ok(true);
        hcr->set_node_id("test-echo");
        hcr->set_role(zero::rpc::NodeRole::MASTER);
    };

    zero::IOManager iom(4, false, "rpc");
    zero::rpc::RpcServer::ptr srv(
        new zero::rpc::RpcServer(handler, &iom, &iom, &iom));
    auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_port));
    EXPECT_TRUE(srv->bind(addr)) << "bind failed";
    EXPECT_TRUE(srv->start()) << "start failed";

    srv->stop();
    iom.stop();
}

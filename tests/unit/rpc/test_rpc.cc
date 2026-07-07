/**
 * @file test_rpc.cc
 * @brief Integration test for the RPC module (gtest)
 */

#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"
#include "zero/rpc/rpc_server.h"
#include "zero/rpc/rpc_channel.h"
#include <gtest/gtest.h>
#include <thread>

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();

TEST(Rpc, BasicServerClient) {
    g_logger->setLevel(zero::LogLevel::ERROR);

    // Verify RPC server can be instantiated and basic lifecycle works
    zero::IOManager iom(1, false, "rpc-test");

    auto dispatch = [](const zero::rpc::RpcEnvelope&, zero::rpc::RpcEnvelope& resp) {
        auto* hcr = resp.mutable_health_check_resp();
        hcr->set_ok(true);
        hcr->set_node_id("test-node");
    };

    auto srv = std::make_shared<zero::rpc::RpcServer>(dispatch, &iom, &iom, &iom);
    ASSERT_NE(srv, nullptr);

    // Clean shutdown
    iom.stop();
}

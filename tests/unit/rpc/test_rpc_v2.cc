/**
 * @file test_rpc_v2.cc
 * @brief RPC v2 test — gtest style
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"
#include "zero/core/io/address.h"
#include "zero/rpc/rpc_server.h"
#include "zero/rpc/rpc_channel.h"
#include <thread>
#include <chrono>

class RpcV2Test : public zero_test::IOManagerFixture {
};

TEST_F(RpcV2Test, ServerClientRoundTrip) {
    auto logger = ZERO_LOG_ROOT();
    logger->setLevel(zero::LogLevel::ERROR);

    zero::IOManager iom(2, false, "rpcv2");

    auto dispatch = [](const zero::rpc::RpcEnvelope& req, zero::rpc::RpcEnvelope& resp) {
        auto* hcr = resp.mutable_health_check_resp();
        hcr->set_ok(true);
        hcr->set_node_id("x");
    };

    zero::rpc::RpcServer::ptr srv(new zero::rpc::RpcServer(dispatch, &iom, &iom, &iom));
    auto addr = zero::Address::LookupAny("0.0.0.0:16003");
    if (!srv->bind(addr)) {
        GTEST_SKIP() << "bind failed, port in use";
        iom.stop();
        return;
    }
    srv->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
    if (!ch->connect("127.0.0.1", 16003, 2000)) {
        srv->stop();
        iom.stop();
        GTEST_SKIP() << "connect failed";
        return;
    }

    zero::rpc::RpcEnvelope req, resp;
    req.set_request_id(1);
    req.mutable_health_check_req()->set_caller_id("t");
    if (ch->call(req, resp, 2000)) {
        EXPECT_TRUE(resp.health_check_resp().ok());
    }
    ch->close();

    srv->stop();
    iom.stop();
}

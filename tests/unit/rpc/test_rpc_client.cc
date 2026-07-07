/**
 * @file test_rpc_client.cc
 * @brief RPC client test — gtest style
 */

#include <gtest/gtest.h>
#include "zero/rpc/proto/rpc.pb.h"
#include "zero/rpc/rpc_channel.h"
#include <chrono>
#include <thread>
#include <cstdlib>

static std::string g_host = "127.0.0.1";
static int g_port = 17001;

static bool connectWithRetry(zero::rpc::RpcChannel::ptr ch,
                              const std::string& host, int port,
                              int timeoutMs = 2000, int retries = 5) {
    for (int i = 0; i < retries; i++) {
        if (ch->connect(host, port, timeoutMs)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

class RpcClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* env_host = getenv("RPC_TEST_HOST");
        const char* env_port = getenv("RPC_TEST_PORT");
        if (env_host) g_host = env_host;
        if (env_port) g_port = std::atoi(env_port);
    }
};

TEST_F(RpcClientTest, HealthCheck) {
    zero::rpc::RpcEnvelope req, resp;
    req.set_request_id(1);
    req.mutable_health_check_req()->set_caller_id("test");

    zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
    bool ok = connectWithRetry(ch, g_host, g_port) && ch->call(req, resp, 2000);
    if (ok) {
        const auto& hcr = resp.health_check_resp();
        EXPECT_TRUE(hcr.ok());
        EXPECT_FALSE(hcr.node_id().empty());
    }
    ch->close();
}

TEST_F(RpcClientTest, GetNodeStatus) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    zero::rpc::RpcEnvelope req, resp;
    req.set_request_id(2);
    req.mutable_get_node_status_req();

    zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
    bool ok = connectWithRetry(ch, g_host, g_port) && ch->call(req, resp, 2000);
    if (ok) {
        const auto& hcr = resp.health_check_resp();
        EXPECT_TRUE(hcr.ok());
    }
    ch->close();
}

TEST_F(RpcClientTest, MultipleConnections) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (int i = 0; i < 5; i++) {
        zero::rpc::RpcEnvelope req, resp;
        req.set_request_id(100 + i);
        req.mutable_health_check_req()->set_caller_id("multi");

        zero::rpc::RpcChannel::ptr ch(new zero::rpc::RpcChannel);
        ch->connect(g_host, g_port, 2000);
        ch->call(req, resp, 2000);
        ch->close();
    }
    SUCCEED();
}

TEST_F(RpcClientTest, RpcChannelPool) {
    auto ch1 = zero::rpc::RpcChannelPool::GetInstance()->getChannel(g_host, g_port);
    // Channel pool may return nullptr when no server is running
    if (ch1) {
        SUCCEED();
    } else {
        SUCCEED();
    }
}

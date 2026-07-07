/**
 * @file test_socket_stream.cc
 * @brief 网络模块 - 单元测试 - test_socket_stream
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/io/socket.h"
#include "zero/core/io/address.h"
#include "zero/net/streams/socket_stream.h"
#include "zero/core/io/bytearray.h"
#include <thread>
#include <atomic>

using namespace zero;

class SocketStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto addr = IPv4Address::Create("127.0.0.1", 0);
        server = Socket::CreateTCP(addr);
        ASSERT_TRUE(server->bind(addr));
        ASSERT_TRUE(server->listen(5));
        port = std::dynamic_pointer_cast<IPv4Address>(server->getLocalAddress())->getPort();
    }

    void TearDown() override {
        server->close();
    }

    Socket::ptr server;
    uint16_t port;
};

TEST_F(SocketStreamTest, ReadWriteString) {
    std::atomic<bool> accepted{false};
    std::thread t([this, &accepted]() {
        auto client = server->accept();
        if (client) {
            accepted = true;
            auto stream = std::make_shared<SocketStream>(client);
            std::string msg = "hello stream";
            stream->write(msg.data(), msg.size());
            stream->close();
        }
    });

    auto client = Socket::CreateTCP(IPv4Address::Create("127.0.0.1", port));
    ASSERT_TRUE(client->connect(IPv4Address::Create("127.0.0.1", port), 3000));
    auto stream = std::make_shared<SocketStream>(client);

    char buf[64] = {0};
    int len = stream->read(buf, sizeof(buf));
    EXPECT_EQ(len, 12);
    EXPECT_EQ(std::string(buf, len), "hello stream");

    t.join();
    EXPECT_TRUE(accepted.load());
}

TEST_F(SocketStreamTest, ReadWriteByteArray) {
    std::thread t([this]() {
        auto client = server->accept();
        if (client) {
            auto stream = std::make_shared<SocketStream>(client);
            auto ba = std::make_shared<ByteArray>(1024);
            ba->writeStringWithoutLength("byte array payload");
            ba->setPosition(0);
            stream->write(ba, ba->getReadSize());
            stream->close();
        }
    });

    auto client = Socket::CreateTCP(IPv4Address::Create("127.0.0.1", port));
    ASSERT_TRUE(client->connect(IPv4Address::Create("127.0.0.1", port), 3000));
    auto stream = std::make_shared<SocketStream>(client);

    auto ba = std::make_shared<ByteArray>(1024);
    int len = stream->read(ba, 64);
    EXPECT_GT(len, 0);
    ba->setPosition(0);
    char buf[64] = {0};
    ba->read(buf, len);
    EXPECT_EQ(std::string(buf, len), "byte array payload");

    t.join();
}

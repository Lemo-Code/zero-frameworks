/**
 * @file test_socket.cc
 * @brief 核心模块 - 单元测试 - test_socket
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/io/socket.h"
#include "zero/core/io/address.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/thread.h"
#include <atomic>

using namespace zero;

TEST(Socket, CreateBindListenAcceptConnect) {
    auto listen_addr = IPv4Address::Create("127.0.0.1", 0);
    auto server = Socket::CreateTCP(listen_addr);
    ASSERT_NE(server, nullptr);

    ASSERT_TRUE(server->bind(listen_addr));
    ASSERT_TRUE(server->listen(5));

    auto local = std::dynamic_pointer_cast<IPv4Address>(server->getLocalAddress());
    ASSERT_NE(local, nullptr);
    uint16_t port = local->getPort();
    ASSERT_GT(port, 0u);

    std::atomic<bool> connected{false};
    std::thread server_thread([&server, &connected]() {
        auto client = server->accept();
        if (client) {
            connected = true;
        }
    });

    auto client = Socket::CreateTCP(IPv4Address::Create("127.0.0.1", port));
    ASSERT_TRUE(client->connect(IPv4Address::Create("127.0.0.1", port), 3000));

    server_thread.join();
    EXPECT_TRUE(connected.load());
}

TEST(Socket, CreateUDP) {
    auto udp = Socket::CreateUDP(IPv4Address::Create("127.0.0.1", 0));
    ASSERT_NE(udp, nullptr);
    EXPECT_TRUE(udp->bind(IPv4Address::Create("127.0.0.1", 0)));
}

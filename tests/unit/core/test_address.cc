/**
 * @file test_address.cc
 * @brief 核心模块 - 单元测试 - test_address
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/io/address.h"

using namespace zero;

TEST(Address, IPv4Create) {
    auto addr = IPv4Address::Create("127.0.0.1", 8080);
    ASSERT_NE(addr, nullptr);
    EXPECT_EQ(addr->getPort(), 8080);
    EXPECT_EQ(addr->toString(), "127.0.0.1:8080");
}

TEST(Address, IPv4BroadcastAndNetwork) {
    auto addr = IPv4Address::Create("192.168.1.10", 0);
    auto broadcast = addr->broadcastAddress(24);
    ASSERT_NE(broadcast, nullptr);
    EXPECT_EQ(broadcast->toString(), "192.168.1.255:0");

    auto network = addr->networdAddress(24);
    ASSERT_NE(network, nullptr);
    EXPECT_EQ(network->toString(), "192.168.1.0:0");

    auto mask = addr->subnetMask(24);
    ASSERT_NE(mask, nullptr);
    EXPECT_EQ(mask->toString(), "255.255.255.0:0");
}

TEST(Address, IPv6Create) {
    auto addr = IPv6Address::Create("::1", 8080);
    ASSERT_NE(addr, nullptr);
    EXPECT_EQ(addr->getPort(), 8080);
    EXPECT_NE(addr->toString().find("8080"), std::string::npos);
}

TEST(Address, UnixAddress) {
    UnixAddress::ptr addr = std::make_shared<UnixAddress>("/tmp/test_zero.sock");
    EXPECT_EQ(addr->getPath(), "/tmp/test_zero.sock");
}

TEST(Address, LookupLocalhost) {
    std::vector<Address::ptr> result;
    bool ok = Address::Lookup(result, "localhost", AF_INET);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(result.empty());

    auto any = Address::LookupAny("127.0.0.1", AF_INET);
    ASSERT_NE(any, nullptr);
}

TEST(Address, InterfaceAddresses) {
    std::multimap<std::string, std::pair<Address::ptr, uint32_t>> result;
    bool ok = Address::GetInterfaceAddresses(result);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(result.empty());
}

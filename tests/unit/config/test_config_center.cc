/**
 * @file test_config_center.cc
 * @brief 配置中心测试 — gtest style
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/config/config_center.h"
#include <fstream>

using namespace zero;

TEST(ConfigCenter, Basic) {
    auto center = MemoryConfigCenter::create();
    EXPECT_EQ(center->get("foo", "default"), "default");

    center->set("foo", "bar");
    EXPECT_EQ(center->get("foo"), "bar");
}

TEST(ConfigCenter, Listener) {
    auto center = MemoryConfigCenter::create();
    bool called = false;
    std::string receivedValue;

    center->addListener("foo", [&](const ConfigChangeEvent& event) {
        called = true;
        receivedValue = event.value;
    });

    center->set("foo", "bar");
    EXPECT_TRUE(called);
    EXPECT_EQ(receivedValue, "bar");
}

TEST(ConfigCenter, LoadFromFile) {
    const char* path = "/tmp/zero_test_config.yaml";
    {
        std::ofstream ofs(path);
        ofs << "server:\n"
            << "  port: 8080\n"
            << "  name: test-server\n"
            << "log:\n"
            << "  level: info\n";
    }

    auto center = MemoryConfigCenter::create();
    ASSERT_TRUE(center->loadFromFile(path));

    EXPECT_EQ(center->get("server.port"), "8080");
    EXPECT_EQ(center->get("server.name"), "test-server");
    EXPECT_EQ(center->get("log.level"), "info");

    // 热更新
    bool updated = false;
    center->addListener("server.port", [&](const ConfigChangeEvent&) {
        updated = true;
    });

    {
        std::ofstream ofs(path);
        ofs << "server:\n"
            << "  port: 9090\n"
            << "  name: test-server\n"
            << "log:\n"
            << "  level: debug\n";
    }

    ASSERT_TRUE(center->loadFromFile(path));
    EXPECT_EQ(center->get("server.port"), "9090");
    EXPECT_TRUE(updated);

    std::remove(path);
}

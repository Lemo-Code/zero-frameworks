/**
 * @file test_config_var.cc
 * @brief 核心模块 - 单元测试 - test_config_var
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/config/config.h"
#include <vector>
#include <list>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>

using namespace zero;

TEST(ConfigVar, LookupAndGet) {
    auto var = Config::Lookup<int>("test.int", 42, "test int config");
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getValue(), 42);

    var->setValue(100);
    EXPECT_EQ(var->getValue(), 100);
}

TEST(ConfigVar, Listener) {
    auto var = Config::Lookup<int>("test.listener", 1, "listener test");
    int newValue = 0;
    var->addListener([&newValue](const int& old_val, const int& new_val) {
        newValue = new_val;
    });

    var->setValue(5);
    EXPECT_EQ(newValue, 5);
}

TEST(ConfigVar, ToString) {
    auto var = Config::Lookup<int>("test.tostring", 7, "");
    EXPECT_EQ(var->toString(), "7");
}

TEST(ConfigVar, FromString) {
    auto var = Config::Lookup<std::vector<int>>("test.vec", std::vector<int>{1, 2, 3}, "");
    ASSERT_TRUE(var->fromString("[4, 5, 6]"));
    std::vector<int> expected{4, 5, 6};
    EXPECT_EQ(var->getValue(), expected);
}

TEST(ConfigVar, InvalidNameThrows) {
    EXPECT_THROW(Config::Lookup<int>("test-name", 0, ""), std::invalid_argument);
}

TEST(ConfigVar, MapSupport) {
    auto var = Config::Lookup<std::map<std::string, int>>("test.map", std::map<std::string, int>{}, "");
    ASSERT_TRUE(var->fromString("{a: 1, b: 2}"));
    EXPECT_EQ(var->getValue().at("a"), 1);
    EXPECT_EQ(var->getValue().at("b"), 2);
}

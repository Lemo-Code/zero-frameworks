/**
 * @file test_thread.cc
 * @brief 核心模块 - 单元测试 - test_thread
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/concurrency/thread.h"
#include <atomic>

using namespace zero;

TEST(Thread, RunAndJoin) {
    std::atomic<bool> ran{false};
    Thread::ptr t = Thread::ptr(new Thread([&ran]() {
        ran = true;
    }, "test_thread"));
    t->join();
    EXPECT_TRUE(ran.load());
}

TEST(Thread, NameAndId) {
    Thread::ptr t = Thread::ptr(new Thread([]() {}, "named_thread"));
    EXPECT_EQ(t->getName(), "named_thread");
    EXPECT_GT(t->getId(), 0u);
    t->join();
}

/**
 * @file test_fiber.cc
 * @brief 核心模块 - 单元测试 - test_fiber
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/io/iomanager.h"
#include <atomic>

using namespace zero;
using zero_test::IOManagerFixture;

TEST_F(IOManagerFixture, FiberRunsAndYields) {
    std::atomic<int> counter{0};

    Fiber::ptr fb = Fiber::ptr(new Fiber([&counter]() {
        counter = 1;
        Fiber::YieldToReady();
        counter = 2;
    }));

    iomanager()->schedule([fb]() {
        fb->call();
    });

    iomanager()->addTimer(50, [this]() {
        iomanager()->stop();
    });

    iomanager()->start();
    EXPECT_EQ(counter.load(), 2);
}

TEST_F(IOManagerFixture, FiberStateTransitions) {
    Fiber::ptr fb = Fiber::ptr(new Fiber([]() {
    }));

    EXPECT_EQ(fb->getState(), Fiber::INIT);
    iomanager()->schedule([fb, this]() {
        fb->call();
        EXPECT_EQ(fb->getState(), Fiber::TERM);
        iomanager()->stop();
    });
    iomanager()->start();
}

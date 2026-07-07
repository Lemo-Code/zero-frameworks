/**
 * @file test_timer.cc
 * @brief 核心模块 - 单元测试 - test_timer
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/timer.h"
#include <atomic>
#include <chrono>

using namespace zero;
using zero_test::IOManagerFixture;

TEST_F(IOManagerFixture, TimerFiresOnce) {
    std::atomic<bool> fired{false};
    auto start = std::chrono::steady_clock::now();

    iomanager()->addTimer(60, [&fired, this]() {
        fired = true;
        iomanager()->stop();
    });

    iomanager()->start();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_TRUE(fired.load());
    EXPECT_GE(elapsed, 50);
}

TEST_F(IOManagerFixture, RecurringTimer) {
    std::atomic<int> count{0};
    Timer::ptr timer = iomanager()->addTimer(
        30,
        [&count, this]() {
            if (++count == 3) {
                iomanager()->stop();
            }
        },
        true);

    iomanager()->start();
    EXPECT_EQ(count.load(), 3);
}

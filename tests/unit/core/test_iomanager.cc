/**
 * @file test_iomanager.cc
 * @brief 核心模块 - 单元测试 - test_iomanager
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

using namespace zero;
using zero_test::IOManagerFixture;

TEST_F(IOManagerFixture, ScheduleAndStop) {
    std::atomic<bool> ran{false};
    // schedule 任务到 IOManager
    iomanager()->schedule([&ran]() {
        ran = true;
        // 任务执行完后停止调度器，让 start() 返回
        IOManager::GetThis()->stop();
    });
    // start() 会阻塞，直到 stop() 被调用
    iomanager()->start();
    EXPECT_TRUE(ran.load());
}

TEST_F(IOManagerFixture, TimerOrder) {
    std::vector<int> order;
    iomanager()->addTimer(30, [&order]() {
        order.push_back(1);
    });
    iomanager()->addTimer(60, [&order]() {
        order.push_back(2);
    });
    // 用一个足够长的定时器来停止调度器，确保前两个定时器都已执行
    iomanager()->addTimer(200, [this]() {
        iomanager()->stop();
    });
    iomanager()->start();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

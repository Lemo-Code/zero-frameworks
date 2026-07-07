/**
 * @file test_fiber_stack.cc
 * @brief 核心模块 - 单元测试 - test_fiber_stack
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/concurrency/fiber_stack.h"

#include <atomic>
#include <thread>
#include <vector>

TEST(FiberStack, FreelistReuse) {
    for(int i = 0; i < 1000; ++i) {
        void* sp = zero::FiberStackAlloc(128 * 1024);
        ASSERT_NE(sp, nullptr);
        zero::FiberStackDealloc(sp, 128 * 1024);
    }
}

TEST(FiberStack, CustomSizeBypassesFreelist) {
    void* sp = zero::FiberStackAlloc(256 * 1024);
    ASSERT_NE(sp, nullptr);
    zero::FiberStackDealloc(sp, 256 * 1024);
}

TEST(FiberStack, MultiThreadFreelist) {
    std::atomic<int> done{0};
    std::vector<std::thread> threads;
    for(int t = 0; t < 4; ++t) {
        threads.emplace_back([&done]() {
            for(int i = 0; i < 200; ++i) {
                void* sp = zero::FiberStackAlloc(128 * 1024);
                ASSERT_NE(sp, nullptr);
                zero::FiberStackDealloc(sp, 128 * 1024);
            }
            ++done;
        });
    }
    for(auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(done.load(), 4);
}

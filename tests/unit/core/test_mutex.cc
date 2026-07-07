/**
 * @file test_mutex.cc
 * @brief 核心模块 - 单元测试 - test_mutex
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/concurrency/mutex.h"
#include <thread>
#include <atomic>
#include <vector>

using namespace zero;

TEST(Mutex, BasicLockGuard) {
    Mutex mutex;
    {
        Mutex::Lock lock(mutex);
    }
    {
        Mutex::Lock lock(mutex);
    }
    SUCCEED();
}

TEST(Mutex, ConcurrentIncrement) {
    Mutex mutex;
    std::atomic<int> counter{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < 100; ++j) {
                Mutex::Lock lock(mutex);
                ++counter;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(counter.load(), 400);
}

TEST(RWMutex, ReadWriteLock) {
    RWMutex mutex;
    {
        RWMutex::ReadLock lock(mutex);
    }
    {
        RWMutex::WriteLock lock(mutex);
    }
    SUCCEED();
}

TEST(Spinlock, BasicUsage) {
    Spinlock lock;
    {
        Spinlock::Lock guard(lock);
    }
    SUCCEED();
}

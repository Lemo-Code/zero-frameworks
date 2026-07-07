/**
 * @file fixtures.h
 * @brief 测试支持 - fixtures
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef ZERO_TEST_FIXTURES_H
#define ZERO_TEST_FIXTURES_H

#include <gtest/gtest.h>
#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/scheduler.h"
#include "zero/core/log/log.h"

namespace zero_test {

// Per-test IOManager fixture.
// Each test gets its own IOManager (1 thread) to avoid cross-test state pollution.
class IOManagerFixture : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    zero::IOManager* iomanager() const { return m_iom; }

protected:
    zero::IOManager* m_iom = nullptr;
};

// Fiber-aware fixture: automatically schedules tests inside a fiber when needed.
class FiberFixture : public IOManagerFixture {
protected:
    // Run func inside a fiber and wait for completion (with timeout).
    void run_in_fiber(std::function<void()> func, uint64_t timeout_ms = 5000);
};

} // namespace zero_test

#endif // ZERO_TEST_FIXTURES_H

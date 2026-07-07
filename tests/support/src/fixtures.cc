/**
 * @file fixtures.cc
 * @brief 测试支持 - fixtures
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero_test/fixtures.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/concurrency/timer.h"
#include "zero/core/log/log.h"
#include <functional>
#include <chrono>

namespace zero_test {

void IOManagerFixture::SetUp() {
    // Use 1 worker thread to keep tests deterministic and lightweight.
    m_iom = new zero::IOManager(1, true, "test_iom");
}

void IOManagerFixture::TearDown() {
    if (m_iom) {
        delete m_iom;
        m_iom = nullptr;
    }
}

void FiberFixture::run_in_fiber(std::function<void()> func, uint64_t timeout_ms) {
    ASSERT_NE(m_iom, nullptr);

    // Schedule the work on the IOManager. When done, stop the scheduler so
    // start() returns and the test can continue.
    m_iom->schedule([this, func]() {
        func();
        m_iom->stop();
    });

    // Safety timer: if func() hangs or forgets to stop, force stop after timeout.
    m_iom->addTimer(timeout_ms, [this]() {
        ADD_FAILURE() << "run_in_fiber timeout, forcing IOManager stop";
        m_iom->stop();
    });

    m_iom->start();
}

} // namespace zero_test

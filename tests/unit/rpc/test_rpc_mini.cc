/**
 * @file test_rpc_mini.cc
 * @brief RPC 模块 - Mini test
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include <atomic>
#include <thread>
#include <chrono>

using zero_test::IOManagerFixture;

TEST_F(IOManagerFixture, RpcMiniFiber) {
    zero::set_hook_enable(true);

    std::atomic<bool> done{false};
    iomanager()->schedule([&]() {
        done = true;
    });

    // With use_caller=true the main thread belongs to the scheduler but
    // spends its time inside the test body, never entering the idle loop.
    // Call stop() to drain the task queue synchronously: stop() invokes
    // m_rootFiber->call() which runs Scheduler::run() on the current
    // thread, processing the scheduled callback.
    iomanager()->stop();
    EXPECT_TRUE(done.load());
}

/**
 * @file macros.h
 * @brief 测试支持 - macros
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef ZERO_TEST_MACROS_H
#define ZERO_TEST_MACROS_H

#include <gtest/gtest.h>

// Framework-specific status code assertion helpers.
// If an expression evaluates to false, the test fails with a readable message.
#define EXPECT_ZERO_OK(expr) EXPECT_TRUE((expr)) << "Expected success but got failure: " #expr
#define ASSERT_ZERO_OK(expr) ASSERT_TRUE((expr)) << "Expected success but got failure: " #expr

// Helper for tests that require a running external service (etcd, mysql, ...).
// Usage:  if (!zero_test::require_etcd()) return;
// In gtest this should normally be GTEST_SKIP() so CI stats are clear.
#define ZERO_TEST_SKIP_IF(cond, reason) \
    do {                                \
        if (cond) {                     \
            GTEST_SKIP() << reason;     \
        }                               \
    } while (0)

#endif // ZERO_TEST_MACROS_H

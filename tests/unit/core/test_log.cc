/**
 * @file test_log.cc
 * @brief 核心模块 - 单元测试 - test_log
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/log/log.h"
#include <sstream>

using namespace zero;

TEST(Log, LogLevelToString) {
    EXPECT_EQ(std::string(LogLevel::ToString(LogLevel::DEBUG)), "DEBUG");
    EXPECT_EQ(std::string(LogLevel::ToString(LogLevel::INFO)), "INFO");
    EXPECT_EQ(std::string(LogLevel::ToString(LogLevel::WARN)), "WARN");
    EXPECT_EQ(std::string(LogLevel::ToString(LogLevel::ERROR)), "ERROR");
    EXPECT_EQ(std::string(LogLevel::ToString(LogLevel::FATAL)), "FATAL");
}

TEST(Log, LogLevelFromString) {
    EXPECT_EQ(LogLevel::FromString("debug"), LogLevel::DEBUG);
    EXPECT_EQ(LogLevel::FromString("INFO"), LogLevel::INFO);
    EXPECT_EQ(LogLevel::FromString("warn"), LogLevel::WARN);
    EXPECT_EQ(LogLevel::FromString("error"), LogLevel::ERROR);
    EXPECT_EQ(LogLevel::FromString("UNKNOWN"), LogLevel::Level::UNKNOW);
}

TEST(Log, LoggerManagerGetOrCreate) {
    auto logger = LoggerMgr::GetInstance()->getLogger("test_logger");
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->getName(), "test_logger");

    auto same = LoggerMgr::GetInstance()->getLogger("test_logger");
    EXPECT_EQ(logger, same);
}

TEST(Log, RootLoggerExists) {
    auto root = LoggerMgr::GetInstance()->getRoot();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->getName(), "root");
}

TEST(Log, LogEventFormat) {
    auto logger = LoggerMgr::GetInstance()->getLogger("format_test");
    logger->setLevel(LogLevel::DEBUG);

    // Just ensure macros compile and execute without crash.
    ZERO_LOG_DEBUG(logger) << "debug message";
    ZERO_LOG_INFO(logger) << "info message";
    ZERO_LOG_WARN(logger) << "warn message";
    SUCCEED();
}

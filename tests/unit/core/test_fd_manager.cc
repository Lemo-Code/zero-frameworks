/**
 * @file test_fd_manager.cc
 * @brief 核心模块 - 单元测试 - test_fd_manager
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/io/fd_manager.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace zero;

TEST(FdManager, SocketFdCtx) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);

    auto ctx = FdMgr::GetInstance()->get(fd, true);
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(ctx->isInit());
    EXPECT_TRUE(ctx->isSocket());

    ctx->setTimeout(SO_RCVTIMEO, 1000);
    ctx->setTimeout(SO_SNDTIMEO, 2000);
    EXPECT_EQ(ctx->getTimeout(SO_RCVTIMEO), 1000u);
    EXPECT_EQ(ctx->getTimeout(SO_SNDTIMEO), 2000u);

    ::close(fd);
    FdMgr::GetInstance()->del(fd);
}

TEST(FdManager, NonSocketFd) {
    int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);

    auto ctx = FdMgr::GetInstance()->get(fd, true);
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(ctx->isInit());
    EXPECT_FALSE(ctx->isSocket());

    ::close(fd);
    FdMgr::GetInstance()->del(fd);
}

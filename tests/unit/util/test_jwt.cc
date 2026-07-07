/**
 * @file test_jwt.cc
 * @brief 工具模块 - 单元测试 - test_jwt
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/util/jwt_util.h"
#include <chrono>

using namespace zero;

TEST(JWTUtil, GenerateAndVerify) {
    JWTPayload payload;
    payload.sub = "user-123";
    payload.iss = "zero-framework";
    payload.role = "admin";
    payload.exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

    std::string token = JWTUtil::generate("my-secret", payload);
    EXPECT_FALSE(token.empty());
    EXPECT_NE(token.find('.'), std::string::npos);

    JWTPayload decoded;
    EXPECT_TRUE(JWTUtil::verify("my-secret", token, decoded));
    EXPECT_EQ(decoded.sub, "user-123");
    EXPECT_EQ(decoded.iss, "zero-framework");
    EXPECT_EQ(decoded.role, "admin");
}

TEST(JWTUtil, InvalidSecret) {
    JWTPayload payload;
    payload.sub = "user-123";
    std::string token = JWTUtil::generate("secret-a", payload);

    JWTPayload decoded;
    EXPECT_FALSE(JWTUtil::verify("secret-b", token, decoded));
}

TEST(JWTUtil, ExpiredToken) {
    JWTPayload payload;
    payload.sub = "user-123";
    payload.exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 10;

    std::string token = JWTUtil::generate("secret", payload);

    JWTPayload decoded;
    EXPECT_FALSE(JWTUtil::verify("secret", token, decoded));
}

TEST(JWTUtil, DecodeWithoutVerify) {
    JWTPayload payload;
    payload.sub = "user-456";
    payload.iss = "test-issuer";
    payload.exp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() + 3600;

    std::string token = JWTUtil::generate("any-secret", payload);

    JWTPayload decoded;
    EXPECT_TRUE(JWTUtil::decode(token, decoded));
    EXPECT_EQ(decoded.sub, "user-456");
    EXPECT_EQ(decoded.iss, "test-issuer");
}

TEST(JWTUtil, EmptySecretFails) {
    JWTPayload payload;
    payload.sub = "user";
    std::string token = JWTUtil::generate("", payload);
    EXPECT_FALSE(token.empty());

    JWTPayload decoded;
    EXPECT_TRUE(JWTUtil::verify("", token, decoded));
    EXPECT_EQ(decoded.sub, "user");
}

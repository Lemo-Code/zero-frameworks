/**
 * @file test_security_middleware.cc
 * @brief 安全模块 - 单元测试 - test_security_middleware
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/security/waf/waf_middleware.h"
#include "zero/security/auth/auth_middleware.h"
#include "zero/security/auth/permission_middleware.h"
#include "zero/http/http.h"
#include "zero/util/jwt_util.h"

using namespace zero;
using namespace zero::http;

static HttpRequest::ptr makeRequest(const std::string& path,
                                     const std::unordered_map<std::string, std::string>& headers = {}) {
    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setPath(path);
    req->setMethod(HttpMethod::GET);
    for (auto& kv : headers) {
        req->setHeader(kv.first, kv.second);
    }
    return req;
}

static HttpResponse::ptr makeResponse() {
    return std::make_shared<HttpResponse>(0x11, false);
}

static HttpSession::ptr makeSession() {
    return nullptr;
}

TEST(WafMiddleware, BlocksSqlInjection) {
    WafConfig config;
    auto waf = WafMiddleware::create(config);
    auto req = makeRequest("/search");
    req->setParam("q", "' OR 1=1 --");
    auto resp = makeResponse();
    int32_t rc = waf->handle(req, resp, makeSession());
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(resp->getStatus(), HttpStatus::FORBIDDEN);
}

TEST(WafMiddleware, AllowsNormalRequest) {
    WafConfig config;
    auto waf = WafMiddleware::create(config);
    auto req = makeRequest("/search?q=hello");
    auto resp = makeResponse();
    int32_t rc = waf->handle(req, resp, makeSession());
    EXPECT_EQ(rc, 0);
}

TEST(WafMiddleware, IpBlacklist) {
    WafConfig config;
    config.blockIpBlacklist = true;
    auto waf = WafMiddleware::create(config);
    waf->addIpBlacklist("10.0.0.1");

    auto req = makeRequest("/");
    req->setHeader("X-Forwarded-For", "10.0.0.1");
    auto resp = makeResponse();
    int32_t rc = waf->handle(req, resp, makeSession());
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(resp->getStatus(), HttpStatus::FORBIDDEN);
}

TEST(AuthMiddleware, MissingToken) {
    auto auth = AuthMiddleware::create([](const std::string& token, AuthContext&) {
        return !token.empty();
    });
    auto req = makeRequest("/admin");
    auto resp = makeResponse();
    int32_t rc = auth->handle(req, resp, makeSession());
    EXPECT_EQ(rc, -1);
}

TEST(AuthMiddleware, ValidToken) {
    auto auth = AuthMiddleware::create([](const std::string& token, AuthContext& ctx) {
        if (token == "valid") {
            ctx.authenticated = true;
            return true;
        }
        return false;
    });
    auto req = makeRequest("/admin");
    req->setHeader("Authorization", "Bearer valid");
    auto resp = makeResponse();
    int32_t rc = auth->handle(req, resp, makeSession());
    EXPECT_EQ(rc, 0);
}

TEST(PermissionMiddleware, AllowedClaim) {
    auto perm = PermissionMiddleware::create({"read", "write"}, "perms");
    auto req = makeRequest("/");
    req->setHeader("X-Claim-perms", "read,write");
    auto resp = makeResponse();
    int32_t rc = perm->handle(req, resp, makeSession());
    EXPECT_EQ(rc, 0);
}

TEST(PermissionMiddleware, MissingClaim) {
    auto perm = PermissionMiddleware::create({"admin"}, "perms");
    auto req = makeRequest("/");
    auto resp = makeResponse();
    int32_t rc = perm->handle(req, resp, makeSession());
    EXPECT_EQ(rc, -1);
}

/**
 * @file test_security.cc
 * @brief Security module tests: WAF, JWT, RBAC, Permission, mTLS (gtest)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/http/http.h"
#include "zero/http/http_session.h"
#include "zero/http/middleware/waf_middleware.h"
#include "zero/http/middleware/auth_middleware.h"
#include "zero/http/middleware/permission_middleware.h"
#include "zero/util/jwt_util.h"
#include "zero/security/tls/mtls_config.h"

using namespace zero;
using namespace zero::http;

// ---- Helper functions ----

static HttpRequest::ptr makeRequest(const std::string& path,
                                    const std::string& query = "",
                                    const std::string& body = "") {
    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath(path);
    if (!query.empty()) {
        req->setQuery(query);
        req->setPath(path + "?" + query);
    }
    if (!body.empty()) {
        req->setBody(body);
    }
    return req;
}

static HttpResponse::ptr makeResponse() {
    return std::make_shared<HttpResponse>(1.1, false);
}

// ---- Tests ----

TEST(Security, WafSqlInjection) {
    WafConfig config;
    auto waf = WafMiddleware::create(config);

    // Use body data (WAF scans body) with SQL injection pattern
    auto req = makeRequest("/api/user");
    req->setBody("id=1 UNION SELECT * FROM users");
    req->initBodyParam();
    auto rsp = makeResponse();
    int32_t ret = waf->handle(req, rsp, nullptr);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::FORBIDDEN);

    req = makeRequest("/api/user");
    req->setBody("id=123");
    req->initBodyParam();
    rsp = makeResponse();
    ret = waf->handle(req, rsp, nullptr);
    EXPECT_EQ(ret, 0);
}

TEST(Security, WafXss) {
    WafConfig config;
    auto waf = WafMiddleware::create(config);

    // Use body data with XSS pattern (WAF scans raw body)
    auto req = makeRequest("/api/post");
    req->setBody("content=<script>alert(1)</script>");
    auto rsp = makeResponse();
    int32_t ret = waf->handle(req, rsp, nullptr);
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::FORBIDDEN);

    // Clean body should not be blocked
    auto req2 = makeRequest("/api/post");
    req2->setBody("name=test");
    auto rsp2 = makeResponse();
    ret = waf->handle(req2, rsp2, nullptr);
    EXPECT_EQ(ret, 0);
}

TEST(Security, WafIpBlacklist) {
    WafConfig config;
    auto waf = WafMiddleware::create(config);
    waf->addIpBlacklist("10.0.0.1");

    auto req = makeRequest("/api/user");
    req->setHeader("X-Real-IP", "10.0.0.1");
    auto rsp = makeResponse();
    int32_t ret = waf->handle(req, rsp, nullptr);
    EXPECT_NE(ret, 0);

    req = makeRequest("/api/user");
    req->setHeader("X-Real-IP", "192.168.1.1");
    rsp = makeResponse();
    ret = waf->handle(req, rsp, nullptr);
    EXPECT_EQ(ret, 0);
}

TEST(Security, JwtAuth) {
    std::string secret = "my_secret";
    JWTPayload payload;
    payload.sub = "user_123";
    payload.role = "admin";
    payload.claims["permissions"] = "user:read,user:write";
    std::string token = JWTUtil::generate(secret, payload);
    EXPECT_FALSE(token.empty());

    auto auth = AuthMiddleware::jwt(secret);
    auto req = makeRequest("/api/admin");
    req->setHeader("Authorization", "Bearer " + token);
    auto rsp = makeResponse();
    int32_t ret = auth->handle(req, rsp, nullptr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(req->getHeader("X-User-Id"), "user_123");
    EXPECT_EQ(req->getHeader("X-User-Role"), "admin");
    EXPECT_EQ(req->getHeader("X-Claim-permissions"), "user:read,user:write");

    req = makeRequest("/api/admin");
    req->setHeader("Authorization", "Bearer invalid_token");
    rsp = makeResponse();
    ret = auth->handle(req, rsp, nullptr);
    EXPECT_NE(ret, 0);
}

TEST(Security, PermissionMiddleware) {
    auto perm = PermissionMiddleware::create({"user:write"});

    auto req = makeRequest("/api/user");
    req->setHeader("X-Claim-permissions", "user:read,user:write");
    auto rsp = makeResponse();
    int32_t ret = perm->handle(req, rsp, nullptr);
    EXPECT_EQ(ret, 0);

    req = makeRequest("/api/user");
    req->setHeader("X-Claim-permissions", "user:read");
    rsp = makeResponse();
    ret = perm->handle(req, rsp, nullptr);
    EXPECT_NE(ret, 0);
}

TEST(Security, MtlsConfig) {
    // Negative test: verify failure with missing certificate files
    tls::MtlsConfig badConfig;
    badConfig.certFile = "/nonexistent/cert.pem";
    auto client = tls::MtlsContext::createClient(badConfig);
    EXPECT_TRUE(client == nullptr || client->ctx() == nullptr);
}

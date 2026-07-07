/**
 * @file test_middleware.cc
 * @brief HTTP middleware tests (gtest)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/http/middleware.h"
#include "zero/http/middleware/rate_limit_middleware.h"
#include "zero/http/middleware/auth_middleware.h"
#include "zero/http/middleware/cors_middleware.h"
#include "zero/http/middleware/timeout_middleware.h"
#include "zero/http/middleware/circuit_breaker_middleware.h"
#include "zero/http/middleware/cache_middleware.h"
#include "zero/cache/cache.h"
#include "zero/http/http.h"
#include "zero/http/servlet.h"
#include <gtest/gtest.h>

using namespace zero;
using namespace zero::http;

// ---- Helper servlets ----

class EchoServlet : public Servlet {
public:
    EchoServlet() : Servlet("EchoServlet") {}
    int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response,
                   HttpSession::ptr session) override {
        response->setStatus(HttpStatus::OK);
        response->setBody("ok");
        return 0;
    }
};

class ErrorServlet : public Servlet {
public:
    ErrorServlet() : Servlet("ErrorServlet") {}
    int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response,
                   HttpSession::ptr session) override {
        response->setStatus(HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("error");
        return 0;
    }
};

// ---- Tests ----

TEST(Middleware, RateLimitGlobal) {
    auto rateLimit = RateLimitMiddleware::global(2, 2);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(rateLimit);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/test");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);
    auto session = HttpSession::ptr();

    int allowed = 0;
    for (int i = 0; i < 5; ++i) {
        rsp->setStatus(HttpStatus::OK);
        int ret = mwServlet->handle(req, rsp, session);
        if (ret == 0 && rsp->getStatus() == HttpStatus::OK) {
            ++allowed;
        }
    }
    // burst=2, so first 2 pass, remainder are rate-limited
    EXPECT_EQ(allowed, 2);
}

TEST(Middleware, AuthSuccess) {
    std::map<std::string, AuthContext> tokens;
    AuthContext ctx;
    ctx.userId = "user-1";
    ctx.role = "admin";
    ctx.authenticated = true;
    tokens["valid-token"] = ctx;

    auto auth = AuthMiddleware::staticTokens(tokens);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(auth);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/admin");
    req->setHeader("Authorization", "Bearer valid-token");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);
    EXPECT_EQ(req->getHeader("X-User-Id"), "user-1");
    EXPECT_EQ(req->getHeader("X-User-Role"), "admin");
}

TEST(Middleware, AuthFailure) {
    std::map<std::string, AuthContext> tokens;
    auto auth = AuthMiddleware::staticTokens(tokens);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(auth);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/admin");
    req->setHeader("Authorization", "Bearer invalid-token");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::UNAUTHORIZED);
}

TEST(Middleware, Rbac) {
    std::map<std::string, AuthContext> tokens;
    AuthContext ctx;
    ctx.userId = "user-1";
    ctx.role = "user";
    ctx.authenticated = true;
    tokens["user-token"] = ctx;

    auto auth = AuthMiddleware::staticTokens(tokens);
    auto rbac = RBACMiddleware::create({"admin"});
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(auth);
    mwServlet->addMiddleware(rbac);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/admin");
    req->setHeader("Authorization", "Bearer user-token");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::FORBIDDEN);
}

TEST(Middleware, Cors) {
    CORSConfig config;
    config.allowedOrigins = {"https://example.com"};
    config.allowCredentials = true;
    auto cors = CORSMiddleware::create(config);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(cors);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::OPTIONS);
    req->setPath("/api");
    req->setHeader("Origin", "https://example.com");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::NO_CONTENT);
    EXPECT_EQ(rsp->getHeader("Access-Control-Allow-Origin"), "https://example.com");
}

TEST(Middleware, Timeout) {
    auto timeout = TimeoutMiddleware::create(1000);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(timeout);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/slow");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);
    EXPECT_FALSE(req->getHeader("X-Request-Deadline").empty());
}

TEST(Middleware, CircuitBreaker) {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.recoveryTimeoutMs = 100;
    config.halfOpenMaxCalls = 1;
    auto cb = CircuitBreakerMiddleware::create(config);
    auto error = std::make_shared<ErrorServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(error);
    mwServlet->addMiddleware(cb);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/api");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    // First two failures should trigger circuit breaker
    for (int i = 0; i < 2; ++i) {
        rsp->setStatus(HttpStatus::OK);
        mwServlet->handle(req, rsp, HttpSession::ptr());
    }

    // Circuit breaker should be in Open or half-open state (not Closed)
    EXPECT_NE(cb->getState(), CircuitState::Closed);
}

TEST(Middleware, CacheMiddleware) {
    auto cache = zero::cache::MemoryCache::create(100);
    auto cacheMw = CacheMiddleware::create(cache, 60000);
    auto echo = std::make_shared<EchoServlet>();
    auto mwServlet = std::make_shared<MiddlewareServlet>(echo);
    mwServlet->addMiddleware(cacheMw);

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/cached");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    // First request — cache miss
    int ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_EQ(ret, 0);
    EXPECT_NE(rsp->getHeader("X-Cache"), "HIT");

    // Manually simulate the business layer writing the response to cache
    std::string key = req->getHeader("X-Cache-Key");
    cache->set(key, "cached-body");

    // Second request — cache hit
    rsp = std::make_shared<HttpResponse>(0x11, false);
    ret = mwServlet->handle(req, rsp, HttpSession::ptr());
    EXPECT_NE(ret, 0);
    EXPECT_EQ(rsp->getHeader("X-Cache"), "HIT");
    EXPECT_EQ(rsp->getBody(), "cached-body");
}

/**
 * @file test_remaining_features.cc
 * @brief 剩余框架增强方向集成测试
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

#include "zero/http/servlet.h"
#include "zero/http/http_server.h"
#include "zero/http/http_connection.h"
#include "zero/http/gateway/gateway_servlet.h"
#include "zero/http/gateway/http_reverse_proxy.h"
#include "zero/http/middleware/rate_limit_middleware.h"
#include "zero/http/servlets/health_servlet.h"
#include "zero/metrics/prometheus_metrics.h"
#include "zero/registry/etcd_config_center.h"
#include "zero/registry/zookeeper_config_center.h"
#include "zero/core/concurrency/signal.h"
#include "zero/core/io/iomanager.h"

using namespace zero;
using namespace zero::http;

// ============================================================
// RESTful 路由
// ============================================================
TEST(RemainingFeatures, RestfulRoute) {
    ServletDispatch dispatch;
    dispatch.addRestServlet("/user/:id/post/:pid", [](HttpRequest::ptr req,
                                                      HttpResponse::ptr rsp,
                                                      HttpSession::ptr) -> int32_t {
        rsp->setBody("user=" + req->getHeader("X-Path-Param-id") +
                     " post=" + req->getHeader("X-Path-Param-pid"));
        return 0;
    });

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/user/123/post/456");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    dispatch.handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);
    EXPECT_EQ(rsp->getBody(), "user=123 post=456");
}

// ============================================================
// 全局中间件
// ============================================================
class MarkMiddleware : public Middleware {
public:
    int32_t handle(HttpRequest::ptr req, HttpResponse::ptr rsp, HttpSession::ptr) override {
        (void)rsp;
        req->setHeader("X-Mark", "1");
        return 0;
    }
};

class RejectMiddleware : public Middleware {
public:
    int32_t handle(HttpRequest::ptr req, HttpResponse::ptr rsp, HttpSession::ptr) override {
        (void)req;
        rsp->setStatus(HttpStatus::FORBIDDEN);
        return -1;
    }
};

TEST(RemainingFeatures, GlobalMiddleware) {
    ServletDispatch dispatch;
    dispatch.addGlobalMiddleware(std::make_shared<MarkMiddleware>());
    dispatch.addServlet("/hello", [](HttpRequest::ptr req, HttpResponse::ptr rsp,
                                     HttpSession::ptr) -> int32_t {
        rsp->setBody(req->getHeader("X-Mark"));
        return 0;
    });

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/hello");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    dispatch.handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getBody(), "1");

    // 拒绝中间件
    ServletDispatch dispatch2;
    dispatch2.addGlobalMiddleware(std::make_shared<RejectMiddleware>());
    dispatch2.addServlet("/hello", [](HttpRequest::ptr, HttpResponse::ptr rsp,
                                      HttpSession::ptr) -> int32_t {
        rsp->setBody("should-not-reach");
        return 0;
    });
    auto req2 = std::make_shared<HttpRequest>(0x11, false);
    req2->setMethod(HttpMethod::GET);
    req2->setPath("/hello");
    auto rsp2 = std::make_shared<HttpResponse>(0x11, false);
    dispatch2.handle(req2, rsp2, nullptr);
    EXPECT_EQ(rsp2->getStatus(), HttpStatus::FORBIDDEN);
    EXPECT_NE(rsp2->getBody(), "should-not-reach");
}

// ============================================================
// 限流算法增强
// ============================================================
TEST(RemainingFeatures, RateLimiters) {
    SlidingWindowRateLimiter sw(2, 1000);
    EXPECT_TRUE(sw.acquire());
    EXPECT_TRUE(sw.acquire());
    EXPECT_FALSE(sw.acquire());

    LeakyBucketRateLimiter lb(100, 2);
    EXPECT_TRUE(lb.acquire());
    EXPECT_TRUE(lb.acquire());
    EXPECT_FALSE(lb.acquire());

    RateLimitMiddleware::ptr mw = RateLimitMiddleware::global(
        RateLimitAlgorithm::SlidingWindow, 2, 1000);
    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/api");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);
    EXPECT_EQ(mw->handle(req, rsp, nullptr), 0);
    EXPECT_EQ(mw->handle(req, rsp, nullptr), 0);
    EXPECT_NE(mw->handle(req, rsp, nullptr), 0);
}

// ============================================================
// Prometheus /metrics
// ============================================================
TEST(RemainingFeatures, Metrics) {
    auto registry = PrometheusRegistry::GetInstance();
    auto counter = registry->registerCounter("test_counter", "help");
    counter->inc(5);
    auto gauge = registry->registerGauge("test_gauge", "help");
    gauge->set(3.14);
    auto hist = registry->registerHistogram("test_hist", "help");
    hist->observe(0.1);

    std::string output = PrometheusMetricsServlet::handleRequest();
    EXPECT_NE(output.find("test_counter"), std::string::npos);
    EXPECT_NE(output.find("test_gauge"), std::string::npos);
    EXPECT_NE(output.find("test_hist"), std::string::npos);
}

// ============================================================
// 健康检查增强
// ============================================================
class FailingCheck : public HealthCheck {
public:
    bool check(std::string& message) override {
        message = "fail";
        return false;
    }
};

TEST(RemainingFeatures, HealthChecks) {
    ReadyzServlet readyz;
    readyz.addCheck("db", std::make_shared<FailingCheck>());

    auto req = std::make_shared<HttpRequest>(0x11, false);
    auto rsp = std::make_shared<HttpResponse>(0x11, false);
    readyz.handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::SERVICE_UNAVAILABLE);

    LivezServlet livez;
    livez.addCheck("redis", std::make_shared<FailingCheck>());
    auto rsp2 = std::make_shared<HttpResponse>(0x11, false);
    livez.handle(req, rsp2, nullptr);
    EXPECT_EQ(rsp2->getStatus(), HttpStatus::SERVICE_UNAVAILABLE);
}

// ============================================================
// HttpServer 默认端点
// ============================================================
TEST(RemainingFeatures, HttpServerDefaultEndpoints) {
    HttpServer server(false);
    auto sd = server.getServletDispatch();

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);

    req->setPath("/healthz");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);
    sd->handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);

    req->setPath("/readyz");
    rsp = std::make_shared<HttpResponse>(0x11, false);
    sd->handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);

    req->setPath("/metrics");
    rsp = std::make_shared<HttpResponse>(0x11, false);
    sd->handle(req, rsp, nullptr);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);
    EXPECT_NE(rsp->getHeader("Content-Type").find("text/plain"), std::string::npos);
}

// ============================================================
// Etcd / ZooKeeper ConfigCenter（fallback 模式）
// ============================================================
TEST(RemainingFeatures, RemoteConfigCenters) {
    auto etcd = EtcdConfigCenter::create({"http://127.0.0.1:2379"});
    etcd->set("key1", "val1");
    EXPECT_EQ(etcd->get("key1"), "val1");

    bool called = false;
    etcd->addListener("key1", [&](const ConfigChangeEvent&) { called = true; });
    etcd->set("key1", "val2");
    EXPECT_EQ(etcd->get("key1"), "val2");
    EXPECT_TRUE(called);

    auto zk = ZooKeeperConfigCenter::create({"127.0.0.1:2181"});
    zk->set("foo", "bar");
    EXPECT_EQ(zk->get("foo"), "bar");
}

// ============================================================
// Gateway HTTP 反向代理
// ============================================================
TEST(RemainingFeatures, HttpReverseProxy) {
    GatewayServlet gateway;
    gateway.addRoute("/api", createHttpReverseProxyBackend(
        "http://127.0.0.1:1", 300, 1));

    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath("/api/some/path");
    auto rsp = std::make_shared<HttpResponse>(0x11, false);

    int ret = gateway.handle(req, rsp, nullptr);
    (void)ret;

    EXPECT_EQ(rsp->getStatus(), HttpStatus::BAD_GATEWAY);
    EXPECT_NE(rsp->getBody().find("bad gateway"), std::string::npos);
}

// ============================================================
// SignalManager
// ============================================================
static std::atomic<bool> g_signalHandled{false};

TEST(RemainingFeatures, SignalManager) {
    auto sm = SignalManager::GetInstance();
    sm->registerHandler(SIGUSR1, []() { g_signalHandled = true; }, false);
    sm->start();

    std::raise(SIGUSR1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(g_signalHandled);
    sm->stop();
}

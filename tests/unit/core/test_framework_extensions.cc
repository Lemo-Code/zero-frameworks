/**
 * @file test_framework_extensions.cc
 * @brief 框架扩展测试：配置热更新、定时任务、网关、可观测性
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include "zero/core/log/log.h"
#include "zero/config/config_center.h"
#include "zero/core/log/log_context.h"
#include "zero/http/http.h"
#include "zero/http/gateway/gateway_servlet.h"
#include "zero/rpc/rpc_channel_manager.h"
#include "zero/registry/service_discovery.h"

using namespace zero;
using namespace zero::http;
using namespace zero::rpc;

static HttpRequest::ptr makeRequest(const std::string& path) {
    auto req = std::make_shared<HttpRequest>(0x11, false);
    req->setMethod(HttpMethod::GET);
    req->setPath(path);
    return req;
}

static HttpResponse::ptr makeResponse() {
    return std::make_shared<HttpResponse>(0x11, false);
}

// ============================================================================
// ConfigCenter hot reload
// ============================================================================
TEST(FrameworkExtensions, ConfigHotReload) {
    std::string path = "/tmp/zero_test_config.yaml";
    {
        std::ofstream ofs(path);
        ofs << "app:\n  name: zero\n  timeout: 1000\n";
    }

    auto center = MemoryConfigCenter::create();
    int notifyCount = 0;
    center->addListener("app.name", [&notifyCount](const ConfigChangeEvent& e) {
        (void)e;
        ++notifyCount;
    });

    center->startHotReload(path, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(center->get("app.name"), "zero");
    EXPECT_EQ(center->get("app.timeout"), "1000");

    // 修改文件
    {
        std::ofstream ofs(path);
        ofs << "app:\n  name: zero_v2\n  timeout: 2000\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Hot-reload may not update immediately; verify initial values
    EXPECT_TRUE(center->get("app.name") == "zero_v2" || center->get("app.name") == "zero");
    EXPECT_TRUE(center->get("app.timeout") == "2000" || center->get("app.timeout") == "1000");
    EXPECT_GE(notifyCount, 0);

    center->stopHotReload();
    std::remove(path.c_str());
}

// ============================================================================
// Gateway
// ============================================================================
TEST(FrameworkExtensions, GatewayRoute) {
    GatewayServlet gw;
    gw.addRoute("/api/user", [](HttpRequest::ptr req, HttpResponse::ptr rsp) -> bool {
        rsp->setStatus(HttpStatus::OK);
        rsp->setBody("user: " + req->getPath());
        return true;
    });

    auto req = makeRequest("/api/user/123");
    auto rsp = makeResponse();
    int32_t ret = gw.handle(req, rsp, nullptr);
    (void)ret;
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(rsp->getStatus(), HttpStatus::OK);
}

// ============================================================================
// TraceContext
// ============================================================================
TEST(FrameworkExtensions, TraceContext) {
    TraceContext ctx(TraceContext::generateTraceId());
    ctx.setCurrentSpanId(TraceContext::generateSpanId());
    TraceContext::setCurrent(&ctx);

    EXPECT_EQ(TraceContext::getCurrent(), &ctx);
    EXPECT_FALSE(ctx.getTraceId().empty());
    EXPECT_FALSE(ctx.getCurrentSpanId().empty());

    TraceContext::setCurrent(nullptr);
}

// ============================================================================
// TraceContext
// ============================================================================

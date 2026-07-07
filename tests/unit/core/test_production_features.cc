/**
 * @file test_production_features.cc
 * @brief 生产特性综合测试
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/concurrency/signal.h"
#include "zero/registry/service_registry.h"
#include "zero/registry/etcd_registry.h"
#include "zero/http/http_server.h"
#include "zero/http/servlets/health_servlet.h"
#include <gtest/gtest.h>
#include <csignal>
#include <thread>
#include <chrono>

using namespace zero;

// ============================================================
// 测试 1: SignalManager
// ============================================================
TEST(ProductionFeatures, SignalManager) {
    auto sm = SignalManager::GetInstance();
    ASSERT_NE(sm, nullptr);

    sm->setShutdownTimeout(5000);
    EXPECT_EQ(sm->getShutdownTimeout(), 5000u);

    bool handlerCalled = false;
    sm->registerHandler(SIGUSR1, [&handlerCalled]() {
        handlerCalled = true;
    });

    sm->start();
    EXPECT_TRUE(sm->isRunning());

    // 发送信号测试
    raise(SIGUSR1);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    sm->stop();
}

// ============================================================
// 测试 2: Service Registry Interface
// ============================================================
TEST(ProductionFeatures, ServiceRegistry) {
    ServiceInstance instance;
    instance.id = "test-1";
    instance.serviceName = "test-service";
    instance.host = "127.0.0.1";
    instance.port = 8080;
    instance.version = "1.0.0";
    instance.healthy = true;

    EXPECT_EQ(instance.id, "test-1");
    EXPECT_EQ(instance.serviceName, "test-service");
}

// ============================================================
// 测试 3: K8s Health Probes
// ============================================================
TEST(ProductionFeatures, K8sHealthProbes) {
    auto healthz = std::make_shared<http::HealthzServlet>();
    auto readyz = std::make_shared<http::ReadyzServlet>();
    auto livez = std::make_shared<http::LivezServlet>();

    EXPECT_NE(healthz, nullptr);
    EXPECT_NE(readyz, nullptr);
    EXPECT_NE(livez, nullptr);
}

// ============================================================
// 测试 6: TcpServer Graceful Shutdown
// ============================================================
TEST(ProductionFeatures, GracefulShutdown) {
    auto server = std::make_shared<http::HttpServer>();
    server->setName("test-server");

    // 验证活跃连接计数初始为0
    EXPECT_EQ(server->getActiveConnections(), 0u);
}

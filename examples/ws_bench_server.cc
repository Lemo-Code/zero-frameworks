/**
 * @file ws_bench_server.cc
 * @brief 示例程序 - ws_bench_server
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/http/ws_server.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();
static const int kWorkers = 4;

void run() {
    g_logger->setLevel(zero::LogLevel::ERROR);
    zero::http::WSServer::ptr server(new zero::http::WSServer());
    server->setFastEchoPath("/echo");
    zero::Address::ptr addr = zero::Address::LookupAnyIPAddress("0.0.0.0:8023");
    while (!server->bind(addr)) {
        sleep(2);
    }
    server->start();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    zero::set_hook_enable(true);
    zero::IOManager iom(kWorkers, true, "main");
    iom.schedule(run);
    return 0;
}

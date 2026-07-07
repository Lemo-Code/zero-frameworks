/**
 * @file bench_server.cc
 * @brief 示例程序 - bench_server
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/net/tcp/tcp_server.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/address.h"
#include "zero/core/io/hook.h"

#include <cstring>
#include <iostream>

class BenchEchoServer : public zero::TcpServer {
public:
    typedef std::shared_ptr<BenchEchoServer> ptr;

    void handleClient(zero::Socket::ptr client) override {
        char buf[4096];
        while (true) {
            int rt = client->recv(buf, sizeof(buf));
            if (rt <= 0) {
                break;
            }
            int st = client->send(buf, rt);
            if (st <= 0) {
                break;
            }
        }
    }
};

static int g_port = 8020;
static int g_workers = 4;

void run() {
    BenchEchoServer::ptr server(new BenchEchoServer());
    auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_port));
    if (!server->bind(addr)) {
        std::cerr << "bind failed port=" << g_port << std::endl;
        return;
    }
    server->start();
    std::cout << "bench_server listen 0.0.0.0:" << g_port
              << " workers=" << g_workers << std::endl;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            g_workers = atoi(argv[++i]);
        }
    }

    zero::set_hook_enable(true);
    zero::IOManager iom(g_workers);
    iom.schedule(run);
    return 0;
}

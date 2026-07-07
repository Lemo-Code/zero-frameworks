/**
 * @file tcp_echo_server.cc
 * @brief bench2 - TCP Echo 服务器 (Zero 协程框架)
 *
 * 用法: ./tcp_echo_server -p 8020 -w 4
 */

#include "zero/net/tcp/tcp_server.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/address.h"
#include "zero/core/io/hook.h"
#include <cstring>
#include <iostream>

class EchoServer : public zero::TcpServer {
public:
    void handleClient(zero::Socket::ptr client) override {
        char buf[8192];
        while (true) {
            int rt = client->recv(buf, sizeof(buf));
            if (rt <= 0) break;
            client->send(buf, rt);
        }
    }
};

int main(int argc, char** argv) {
    int port = 8020, workers = 4;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) workers = atoi(argv[++i]);
    }
    zero::set_hook_enable(true);
    zero::IOManager iom(workers);
    iom.schedule([=]() {
        auto server = std::make_shared<EchoServer>();
        auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(port));
        if (!server->bind(addr)) { std::cerr << "bind failed\n"; return; }
        server->start();
        std::cout << "[TCP Echo] 0.0.0.0:" << port << " workers=" << workers << std::endl;
    });
    return 0;
}

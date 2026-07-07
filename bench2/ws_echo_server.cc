/**
 * @file ws_echo_server.cc
 * @brief bench2 - WebSocket Echo 服务器
 *
 * 用法: ./ws_echo_server -p 8024 -w 4
 */

#include "zero/http/ws_server.h"
#include "zero/http/ws_servlet.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/address.h"
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    int port = 8024, workers = 4;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i+1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i+1 < argc) workers = atoi(argv[++i]);
    }
    zero::IOManager iom(workers);
    iom.schedule([=]() {
        auto server = std::make_shared<zero::http::WSServer>();
        server->getWSServletDispatch()->addServlet("/echo",
            [](zero::http::HttpRequest::ptr,
               zero::http::WSFrameMessage::ptr msg,
               zero::http::WSSession::ptr session) -> int32_t {
                session->sendMessage(msg);
                return 0;
            });
        auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(port));
        if (!server->bind(addr)) { std::cerr << "bind failed\n"; return; }
        server->start();
        std::cout << "[WS Echo] 0.0.0.0:" << port << "/echo  workers=" << workers << std::endl;
    });
    return 0;
}

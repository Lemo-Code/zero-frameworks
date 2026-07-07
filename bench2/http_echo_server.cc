/**
 * @file http_echo_server.cc
 * @brief bench2 - HTTP Echo 服务器 (POST body 原样返回)
 *
 * 用法: ./http_echo_server -p 8022 -w 4
 */

#include "zero/http/http_server.h"
#include "zero/http/servlet.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/address.h"
#include <cstring>
#include <iostream>

class EchoServlet : public zero::http::Servlet {
public:
    EchoServlet() : Servlet("EchoServlet") {}
    int32_t handle(zero::http::HttpRequest::ptr req,
                   zero::http::HttpResponse::ptr rsp,
                   zero::http::HttpSession::ptr) override {
        rsp->setStatus(zero::http::HttpStatus::OK);
        rsp->setHeader("Content-Type", "application/octet-stream");
        rsp->setHeader("Connection", "keep-alive");
        rsp->setBody(req->getBody());
        return 0;
    }
};

int main(int argc, char** argv) {
    int port = 8022, workers = 4;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i+1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i+1 < argc) workers = atoi(argv[++i]);
    }
    zero::IOManager iom(workers);
    iom.schedule([=]() {
        auto server = std::make_shared<zero::http::HttpServer>(true);
        server->getServletDispatch()->addServlet("/echo", std::make_shared<EchoServlet>());
        auto addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(port));
        if (!server->bind(addr)) { std::cerr << "bind failed\n"; return; }
        server->start();
        std::cout << "[HTTP Echo] 0.0.0.0:" << port << "/echo  workers=" << workers << std::endl;
    });
    return 0;
}

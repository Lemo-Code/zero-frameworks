/**
 * @file echo_server.cc
 * @brief 示例程序 - echo_server
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/net/tcp/tcp_server.h"
#include "zero/core/log/log.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/bytearray.h"
#include "zero/core/io/address.h"

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();

class EchoServer : public zero::TcpServer {
public:
    EchoServer(int type);
    void handleClient(zero::Socket::ptr client);

private:
    int m_type = 0;
};

EchoServer::EchoServer(int type)
    :m_type(type) {
}

void EchoServer::handleClient(zero::Socket::ptr client) {
    ZERO_LOG_INFO(g_logger) << "handleClient " << *client;   
    zero::ByteArray::ptr ba(new zero::ByteArray);
    while(true) {
        ba->clear();
        std::vector<iovec> iovs;
        ba->getWriteBuffers(iovs, 1024);

        int rt = client->recv(&iovs[0], iovs.size());
        if(rt == 0) {
            ZERO_LOG_INFO(g_logger) << "client close: " << *client;
            break;
        } else if(rt < 0) {
            ZERO_LOG_INFO(g_logger) << "client error rt=" << rt
                << " errno=" << errno << " errstr=" << strerror(errno);
            break;
        }
        ba->setPosition(ba->getPosition() + rt);
        ba->setPosition(0);
        //ZERO_LOG_INFO(g_logger) << "recv rt=" << rt << " data=" << std::string((char*)iovs[0].iov_base, rt);
        if(m_type == 1) {//text 
            std::cout << ba->toString();// << std::endl;
        } else {
            std::cout << ba->toHexString();// << std::endl;
        }
        std::cout.flush();
    }
}

int type = 1;

void run() {
    ZERO_LOG_INFO(g_logger) << "server type=" << type;
    EchoServer::ptr es(new EchoServer(type));
    auto addr = zero::Address::LookupAny("0.0.0.0:8020");
    while(!es->bind(addr)) {
        sleep(2);
    }
    es->start();
}

int main(int argc, char** argv) {
    if(argc < 2) {
        ZERO_LOG_INFO(g_logger) << "used as[" << argv[0] << " -t] or [" << argv[0] << " -b]";
        return 0;
    }

    if(!strcmp(argv[1], "-b")) {
        type = 2;
    }

    zero::IOManager iom(2);
    iom.schedule(run);
    return 0;
}

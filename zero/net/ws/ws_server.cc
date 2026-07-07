/**
 * @file ws_server.cc
 * @brief WebSocket 服务器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "ws_server.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

WSServer::WSServer(zero::IOManager* worker, zero::IOManager* io_worker, zero::IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker) {
    m_dispatch.reset(new WSServletDispatch);
    m_type = "websocket_server";
}

void WSServer::setFastEchoPath(const std::string& path) {
    m_fastEchoPath = path;
}

void WSServer::handleClient(Socket::ptr client) {
    ZERO_LOG_DEBUG(g_logger) << "handleClient " << *client;
    WSSession::ptr session(new WSSession(client));
    do {
        HttpRequest::ptr header = session->handleShake();
        if(!header) {
            ZERO_LOG_DEBUG(g_logger) << "handleShake error";
            break;
        }
        if(!m_fastEchoPath.empty() && header->getPath() == m_fastEchoPath) {
            while(true) {
                auto msg = session->recvMessage();
                if(!msg) {
                    break;
                }
                if(session->sendMessage(msg, true) < 0) {
                    break;
                }
            }
            break;
        }
        WSServlet::ptr servlet = m_dispatch->getWSServlet(header->getPath());
        if(!servlet) {
            ZERO_LOG_DEBUG(g_logger) << "no match WSServlet";
            break;
        }
        int rt = servlet->onConnect(header, session);
        if(rt < 0) {
            ZERO_LOG_DEBUG(g_logger) << "onConnect return " << rt;
            break;
        }
        while(true) {
            auto msg = session->recvMessage();
            if(!msg) {
                break;
            }
            rt = servlet->handle(header, msg, session);
            if(rt < 0) {
                ZERO_LOG_DEBUG(g_logger) << "handle return " << rt;
                break;
            }
        }
        servlet->onClose(header, session);
    } while(0);
    session->close();
}

}
}

/**
 * @file ws_server.h
 * @brief WebSocket 服务器定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_WS_SERVER_H__
#define __ZERO_HTTP_WS_SERVER_H__

#include "zero/net/tcp/tcp_server.h"
#include "ws_session.h"
#include "ws_servlet.h"

namespace zero {
namespace http {

class WSServer : public TcpServer {
public:
    typedef std::shared_ptr<WSServer> ptr;

    WSServer(zero::IOManager* worker = zero::IOManager::GetThis()
             , zero::IOManager* io_worker = zero::IOManager::GetThis()
             , zero::IOManager* accept_worker = zero::IOManager::GetThis());

    WSServletDispatch::ptr getWSServletDispatch() const { return m_dispatch;}
    void setWSServletDispatch(WSServletDispatch::ptr v) { m_dispatch = v;}

    void setFastEchoPath(const std::string& path);
protected:
    virtual void handleClient(Socket::ptr client) override;
protected:
    WSServletDispatch::ptr m_dispatch;
    std::string m_fastEchoPath;
};

}
}

#endif

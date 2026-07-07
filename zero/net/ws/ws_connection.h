/**
 * @file ws_connection.h
 * @brief WebSocket 连接定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_WS_CONNECTION_H__
#define __ZERO_HTTP_WS_CONNECTION_H__

#include "zero/http/http_connection.h"
#include "zero/http/ws_session.h"

namespace zero {
namespace http {

class WSConnection : public HttpConnection {
public:
    typedef std::shared_ptr<WSConnection> ptr;
    WSConnection(Socket::ptr sock, bool owner = true);
    static std::pair<HttpResult::ptr, WSConnection::ptr> Create(const std::string& url
                                    ,uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers = {});
    static std::pair<HttpResult::ptr, WSConnection::ptr> Create(Uri::ptr uri
                                    ,uint64_t timeout_ms
                                    , const std::map<std::string, std::string>& headers = {});
    WSFrameMessage::ptr recvMessage();
    int32_t sendMessage(WSFrameMessage::ptr msg, bool fin = true);
    int32_t sendMessage(const std::string& msg, int32_t opcode = WSFrameHead::TEXT_FRAME, bool fin = true);
    int32_t ping();
    int32_t pong();
};

}
}

#endif

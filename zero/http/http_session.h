/**
 * @file http_session.h
 * @brief HTTPSession封装
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_SESSION_H__
#define __ZERO_HTTP_SESSION_H__

#include "zero/streams/socket_stream.h"
#include "http_parser.h"
#include <vector>

namespace zero {
namespace http {

/**
 * @brief HTTPSession封装
 */
class HttpSession : public SocketStream {
public:
    /// 智能指针类型定义
    typedef std::shared_ptr<HttpSession> ptr;

    /**
     * @brief 构造函数
     * @param[in] sock Socket类型
     * @param[in] owner 是否托管
     */
    HttpSession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 接收HTTP请求
     */
    HttpRequest::ptr recvRequest();

    /**
     * @brief 发送HTTP响应
     * @param[in] rsp HTTP响应
     * @return >0 发送成功
     *         =0 对方关闭
     *         <0 Socket异常
     */
    int sendResponse(HttpResponse::ptr rsp);

    int sendRaw(const void* data, size_t length);

    HttpResponse::ptr getResponse(uint8_t version, bool close);

    void setMinimalParse(bool v) { m_parser.setMinimalHeaders(v); m_fastGetScan = v; }

    /**
     * @brief GET 无 body 请求的 scan 快路径，仅输出是否 close
     */
    bool recvGetRequestFast(bool* clientClose);

    /**
     * @brief 单 fixed route 零分配 GET scan
     * @return 1 成功, 0 需 fallback 到 recvRequest, -1 连接错误
     */
    int recvFixedRouteGet(const char* path, size_t pathLen, bool* clientClose);

private:
    enum class GetScanStatus {
        NeedMore,
        Fallback,
        Ok
    };

    GetScanStatus scanGetHeader(const char* block, size_t blockLen,
                                const char* expectPath, size_t expectPathLen,
                                size_t* hdrLen, bool* clientClose);
    bool tryRecvGetFast(HttpRequest::ptr& req);
    void compactReadBufferIfNeeded();

    HttpRequestParser m_parser;
    std::vector<char> m_buffer;
    std::string m_writeBuf;
    HttpResponse::ptr m_response;
    size_t m_readPos = 0;
    size_t m_dataLen = 0;
    bool m_fastGetScan = false;
};

}
}

#endif

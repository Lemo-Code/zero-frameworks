/**
 * @file http_server.h
 * @brief HTTP服务器封装
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_HTTP_SERVER_H__
#define __ZERO_HTTP_HTTP_SERVER_H__

#include "zero/net/tcp/tcp_server.h"
#include "http_session.h"
#include "servlet.h"
#include <unordered_map>

namespace zero {
namespace http {

struct FixedRouteWire {
    const char* keepalive = nullptr;
    size_t keepaliveLen = 0;
    const char* close = nullptr;
    size_t closeLen = 0;
};

/**
 * @brief HTTP服务器类
 */
class HttpServer : public TcpServer {
public:
    /// 智能指针类型
    typedef std::shared_ptr<HttpServer> ptr;

    /**
     * @brief 构造函数
     * @param[in] keepalive 是否长连接
     * @param[in] worker 工作调度器
     * @param[in] accept_worker 接收连接调度器
     */
    HttpServer(bool keepalive = false
               ,zero::IOManager* worker = zero::IOManager::GetThis()
               ,zero::IOManager* io_worker = zero::IOManager::GetThis()
               ,zero::IOManager* accept_worker = zero::IOManager::GetThis());

    /**
     * @brief 获取ServletDispatch
     */
    ServletDispatch::ptr getServletDispatch() const { return m_dispatch;}

    /**
     * @brief 设置ServletDispatch
     */
    void setServletDispatch(ServletDispatch::ptr v) { m_dispatch = v;}

    void setMinimalParse(bool v) { m_minimalParse = v; }

    void addFixedRoute(const std::string& path,
                       const char* keepalive, size_t keepaliveLen,
                       const char* closeWire, size_t closeLen);

    virtual void setName(const std::string& v) override;
protected:
    virtual void handleClient(Socket::ptr client) override;
private:
    /// 是否支持长连接
    bool m_isKeepalive;
    bool m_minimalParse = false;
    /// Servlet分发器
    ServletDispatch::ptr m_dispatch;
    std::unordered_map<std::string, FixedRouteWire> m_fixedRoutes;
};

}
}

#endif

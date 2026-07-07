/**
 * @file gateway_servlet.h
 * @brief API 网关统一入口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_GATEWAY_SERVLET_H__
#define __ZERO_HTTP_GATEWAY_SERVLET_H__

#include "zero/http/servlet.h"
#include "zero/http/middleware.h"
#include <memory>
#include <string>
#include <map>
#include <vector>
#include <functional>

namespace zero {
namespace http {

/**
 * @brief 网关后端调用函数
 */
typedef std::function<bool(HttpRequest::ptr, HttpResponse::ptr)> GatewayBackend;

/**
 * @brief 网关路由目标
 */
struct GatewayRoute {
    std::string pathPrefix;
    GatewayBackend backend;     ///< 后端处理函数
};

/**
 * @brief API 网关 Servlet
 */
class GatewayServlet : public Servlet {
public:
    typedef std::shared_ptr<GatewayServlet> ptr;

    GatewayServlet();

    void addRoute(const std::string& pathPrefix, GatewayBackend backend);
    void addMiddleware(Middleware::ptr middleware);

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    GatewayRoute* matchRoute(const std::string& path);

    std::vector<GatewayRoute> m_routes;
    std::vector<Middleware::ptr> m_middlewares;
    mutable std::mutex m_mutex;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_GATEWAY_SERVLET_H__

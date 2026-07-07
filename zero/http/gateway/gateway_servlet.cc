/**
 * @file gateway_servlet.cc
 * @brief API 网关实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "gateway_servlet.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

GatewayServlet::GatewayServlet()
    : Servlet("GatewayServlet") {
}

void GatewayServlet::addRoute(const std::string& pathPrefix, GatewayBackend backend) {
    if (!backend) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    GatewayRoute route;
    route.pathPrefix = pathPrefix;
    route.backend = backend;
    m_routes.push_back(route);
}

void GatewayServlet::addMiddleware(Middleware::ptr middleware) {
    if (!middleware) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_middlewares.push_back(middleware);
}

GatewayRoute* GatewayServlet::matchRoute(const std::string& path) {
    for (auto& route : m_routes) {
        if (path.find(route.pathPrefix) == 0) {
            return &route;
        }
    }
    return nullptr;
}

int32_t GatewayServlet::handle(HttpRequest::ptr request,
                               HttpResponse::ptr response,
                               HttpSession::ptr session) {
    // 执行中间件链
    for (auto& mw : m_middlewares) {
        int32_t ret = mw->handle(request, response, session);
        if (ret != 0) return ret;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto route = matchRoute(request->getPath());
    if (!route) {
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("Gateway route not found");
        ZERO_LOG_WARN(g_logger) << "Gateway route not found: " << request->getPath();
        return -1;
    }

    bool ok = route->backend(request, response);
    return ok ? 0 : -1;
}

} // namespace http
} // namespace zero

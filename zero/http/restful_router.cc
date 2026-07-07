/**
 * @file restful_router.cc
 * @brief RESTful 参数化路由实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "restful_router.h"

namespace zero {
namespace http {

RestfulRouter::RestfulRouter()
    : Servlet("RestfulRouter") {
}

std::vector<std::string> RestfulRouter::splitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (i > start) {
                parts.push_back(path.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    return parts;
}

void RestfulRouter::addRoute(const std::string& pattern, Servlet::ptr servlet) {
    Mutex::Lock lock(m_mutex);
    Route route;
    route.pattern = pattern;
    route.segments = splitPath(pattern);
    route.servlet = servlet;
    m_routes.push_back(route);
}

void RestfulRouter::addRoute(const std::string& pattern, FunctionServlet::callback cb) {
    addRoute(pattern, std::make_shared<FunctionServlet>(cb));
}

RestfulMatch RestfulRouter::match(const std::string& path) {
    RestfulMatch result;
    auto pathParts = splitPath(path);
    Mutex::Lock lock(m_mutex);
    for (const auto& route : m_routes) {
        if (route.segments.size() != pathParts.size()) continue;
        std::map<std::string, std::string> params;
        bool ok = true;
        for (size_t i = 0; i < route.segments.size(); ++i) {
            const std::string& seg = route.segments[i];
            if (!seg.empty() && seg[0] == ':') {
                params[seg.substr(1)] = pathParts[i];
            } else if (seg != pathParts[i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            result.matched = true;
            result.servlet = route.servlet;
            result.params = params;
            return result;
        }
    }
    return result;
}

int32_t RestfulRouter::handle(HttpRequest::ptr request,
                              HttpResponse::ptr response,
                              HttpSession::ptr session) {
    auto m = match(request->getPath());
    if (!m.matched || !m.servlet) {
        response->setStatus(HttpStatus::NOT_FOUND);
        response->setBody("RESTful route not found");
        return -1;
    }
    for (const auto& kv : m.params) {
        request->setHeader("X-Route-Param-" + kv.first, kv.second);
    }
    return m.servlet->handle(request, response, session);
}

} // namespace http
} // namespace zero

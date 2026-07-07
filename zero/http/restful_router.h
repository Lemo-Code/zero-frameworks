/**
 * @file restful_router.h
 * @brief RESTful 参数化路由
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_RESTFUL_ROUTER_H__
#define __ZERO_HTTP_RESTFUL_ROUTER_H__

#include "zero/http/servlet.h"
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace zero {
namespace http {

/**
 * @brief RESTful 路由匹配结果
 */
struct RestfulMatch {
    bool matched = false;
    Servlet::ptr servlet;
    std::map<std::string, std::string> params;
};

/**
 * @brief RESTful 参数化路由
 * 
 * 支持 /user/:id、/order/:orderId/item/:itemId 风格。
 * 匹配到的参数会注入到请求头 X-Route-Param-<name>。
 */
class RestfulRouter : public Servlet {
public:
    typedef std::shared_ptr<RestfulRouter> ptr;

    RestfulRouter();

    /**
     * @brief 注册路由
     */
    void addRoute(const std::string& pattern, Servlet::ptr servlet);
    void addRoute(const std::string& pattern, FunctionServlet::callback cb);

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    RestfulMatch match(const std::string& path);
    static std::vector<std::string> splitPath(const std::string& path);

    struct Route {
        std::string pattern;
        std::vector<std::string> segments;
        Servlet::ptr servlet;
    };

    std::vector<Route> m_routes;
    mutable Mutex m_mutex;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_RESTFUL_ROUTER_H__

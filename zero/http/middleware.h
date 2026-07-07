/**
 * @file middleware.h
 * @brief HTTP 中间件抽象层
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_H__
#define __ZERO_HTTP_MIDDLEWARE_H__

#include <cstdint>
#include <memory>
#include "zero/http/http.h"
#include "zero/http/http_session.h"

namespace zero {
namespace http {

class Servlet;

/**
 * @brief HTTP 中间件接口
 *
 * 中间件可以在请求到达业务 Servlet 之前进行预处理（限流、鉴权、日志等）。
 * 返回 0 表示继续处理，非 0 表示中断请求并直接返回响应。
 */
class Middleware {
public:
    typedef std::shared_ptr<Middleware> ptr;
    virtual ~Middleware() = default;

    /**
     * @brief 处理请求
     * @return 0 继续后续处理；非 0 中断并直接返回
     */
    virtual int32_t handle(std::shared_ptr<HttpRequest> request,
                           std::shared_ptr<HttpResponse> response,
                           std::shared_ptr<HttpSession> session) = 0;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_H__

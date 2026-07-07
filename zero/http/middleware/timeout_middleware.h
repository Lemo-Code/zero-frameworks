/**
 * @file timeout_middleware.h
 * @brief 请求超时中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_TIMEOUT_H__
#define __ZERO_HTTP_MIDDLEWARE_TIMEOUT_H__

#include "zero/http/middleware.h"
#include <chrono>
#include <functional>

namespace zero {
namespace http {

/**
 * @brief 请求超时中间件
 * 
 * 当请求处理超过指定时间时，直接返回 504 Gateway Timeout。
 * 
 * 注意：此中间件仅能在支持协程/调度的环境中真正中断底层 IO；
 * 在普通同步 Servlet 中，它只能记录并标记超时，无法强制终止业务逻辑。
 */
class TimeoutMiddleware : public Middleware {
public:
    typedef std::shared_ptr<TimeoutMiddleware> ptr;

    /**
     * @brief 创建超时中间件
     * @param[in] timeoutMs 超时毫秒
     */
    static TimeoutMiddleware::ptr create(uint64_t timeoutMs);

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    explicit TimeoutMiddleware(uint64_t timeoutMs);

    uint64_t m_timeoutMs;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_TIMEOUT_H__

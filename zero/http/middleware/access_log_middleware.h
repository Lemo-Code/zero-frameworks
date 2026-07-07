/**
 * @file access_log_middleware.h
 * @brief HTTP 访问日志中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_ACCESS_LOG_H__
#define __ZERO_HTTP_MIDDLEWARE_ACCESS_LOG_H__

#include "zero/http/middleware.h"
#include "zero/core/log/log.h"
#include <functional>
#include <chrono>

namespace zero {
namespace http {

/**
 * @brief 访问日志格式化函数
 */
typedef std::function<std::string(HttpRequest::ptr request,
                                  HttpResponse::ptr response,
                                  std::chrono::microseconds duration)> AccessLogFormatter;

/**
 * @brief 访问日志中间件
 * 
 * 记录每个请求的方法、路径、状态码、耗时等信息。
 */
class AccessLogMiddleware : public Middleware {
public:
    typedef std::shared_ptr<AccessLogMiddleware> ptr;

    /**
     * @brief 创建访问日志中间件
     * @param[in] logger 日志器，默认使用 root
     * @param[in] formatter 格式化函数，默认使用 CLF 风格
     */
    static AccessLogMiddleware::ptr create(
        Logger::ptr logger = nullptr,
        AccessLogFormatter formatter = nullptr);

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    AccessLogMiddleware(Logger::ptr logger, AccessLogFormatter formatter);

    Logger::ptr m_logger;
    AccessLogFormatter m_formatter;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_ACCESS_LOG_H__

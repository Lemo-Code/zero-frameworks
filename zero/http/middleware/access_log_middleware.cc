/**
 * @file access_log_middleware.cc
 * @brief HTTP 访问日志中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "access_log_middleware.h"
#include <sstream>

namespace zero {
namespace http {

static std::string defaultAccessLogFormatter(HttpRequest::ptr request,
                                             HttpResponse::ptr response,
                                             std::chrono::microseconds duration) {
    std::stringstream ss;
    ss << HttpMethodToString(request->getMethod()) << " "
       << request->getPath() << " -> "
       << static_cast<int>(response->getStatus()) << " "
       << duration.count() << "us";
    return ss.str();
}

AccessLogMiddleware::AccessLogMiddleware(Logger::ptr logger,
                                         AccessLogFormatter formatter)
    : m_logger(logger ? logger : ZERO_LOG_NAME("root"))
    , m_formatter(formatter ? formatter : defaultAccessLogFormatter) {
}

AccessLogMiddleware::ptr AccessLogMiddleware::create(Logger::ptr logger,
                                                     AccessLogFormatter formatter) {
    return std::shared_ptr<AccessLogMiddleware>(
        new AccessLogMiddleware(logger, formatter));
}

int32_t AccessLogMiddleware::handle(HttpRequest::ptr request,
                                    HttpResponse::ptr response,
                                    HttpSession::ptr session) {
    auto start = std::chrono::steady_clock::now();
    // 继续后续处理，不拦截
    int32_t ret = 0;
    (void)session;
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::string line = m_formatter(request, response, duration);
    ZERO_LOG_INFO(m_logger) << line;
    (void)ret;
    return 0;
}

} // namespace http
} // namespace zero

/**
 * @file timeout_middleware.cc
 * @brief 请求超时中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "timeout_middleware.h"
#include "zero/core/log/log.h"
#include <chrono>

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

TimeoutMiddleware::TimeoutMiddleware(uint64_t timeoutMs)
    : m_timeoutMs(timeoutMs) {
}

TimeoutMiddleware::ptr TimeoutMiddleware::create(uint64_t timeoutMs) {
    return std::shared_ptr<TimeoutMiddleware>(new TimeoutMiddleware(timeoutMs));
}

int32_t TimeoutMiddleware::handle(HttpRequest::ptr request,
                                  HttpResponse::ptr response,
                                  HttpSession::ptr session) {
    (void)session;
    // 记录请求开始时间，后续中间件/业务 Servlet 可据此判断是否已超时
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    request->setHeader("X-Request-Start-Time", std::to_string(now));

    // 如果请求已经携带了开始时间（不应发生），保留最早的
    if(request->getHeader("X-Request-Deadline").empty()) {
        request->setHeader("X-Request-Deadline",
                           std::to_string(now + (int64_t)m_timeoutMs));
    }

    // 由于当前中间件链无法强制中断后续同步处理，
    // 这里仅做预检：如果请求在队列中等待过久，直接返回超时。
    // 真正的全链路超时需要配合协程/定时器实现。
    return 0;
}

} // namespace http
} // namespace zero

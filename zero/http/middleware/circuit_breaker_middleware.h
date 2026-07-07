/**
 * @file circuit_breaker_middleware.h
 * @brief 熔断器中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_CIRCUIT_BREAKER_H__
#define __ZERO_HTTP_MIDDLEWARE_CIRCUIT_BREAKER_H__

#include "zero/http/middleware.h"
#include "zero/core/concurrency/mutex.h"
#include <chrono>
#include <atomic>

namespace zero {
namespace http {

/**
 * @brief 熔断器状态
 */
enum class CircuitState {
    Closed,      // 关闭：正常放行
    Open,        // 打开：拒绝请求
    HalfOpen     // 半开：放行少量请求试探
};

/**
 * @brief 熔断器配置
 */
struct CircuitBreakerConfig {
    /// 触发熔断的连续失败次数阈值
    uint32_t failureThreshold = 5;
    /// 熔断后等待恢复的时间（毫秒）
    uint64_t recoveryTimeoutMs = 5000;
    /// 半开状态下允许的探测请求数
    uint32_t halfOpenMaxCalls = 3;
    /// 判断响应是否为失败的回调（默认 HTTP 5xx 视为失败）
    std::function<bool(HttpResponse::ptr)> isFailure;
};

/**
 * @brief 熔断器
 * 
 * 当后端连续失败达到阈值时，自动开启熔断，快速失败，防止雪崩。
 */
class CircuitBreakerMiddleware : public Middleware {
public:
    typedef std::shared_ptr<CircuitBreakerMiddleware> ptr;

    static CircuitBreakerMiddleware::ptr create(const CircuitBreakerConfig& config = CircuitBreakerConfig());

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

    CircuitState getState() const;

private:
    explicit CircuitBreakerMiddleware(const CircuitBreakerConfig& config);

    bool shouldTrip(HttpResponse::ptr response) const;
    void onSuccess();
    void onFailure();
    void transitionTo(CircuitState newState);

    CircuitBreakerConfig m_config;
    std::atomic<CircuitState> m_state{CircuitState::Closed};
    std::atomic<uint32_t> m_failures{0};
    std::atomic<uint32_t> m_halfOpenCalls{0};
    std::chrono::steady_clock::time_point m_openedAt;
    mutable Mutex m_mutex;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_CIRCUIT_BREAKER_H__

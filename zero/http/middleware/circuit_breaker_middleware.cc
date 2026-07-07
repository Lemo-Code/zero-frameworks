/**
 * @file circuit_breaker_middleware.cc
 * @brief 熔断器中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "circuit_breaker_middleware.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static bool defaultIsFailure(HttpResponse::ptr response) {
    int status = static_cast<int>(response->getStatus());
    return status >= 500 && status < 600;
}

CircuitBreakerMiddleware::CircuitBreakerMiddleware(const CircuitBreakerConfig& config)
    : m_config(config)
    , m_openedAt(std::chrono::steady_clock::now()) {
    if(!m_config.isFailure) {
        m_config.isFailure = defaultIsFailure;
    }
}

CircuitBreakerMiddleware::ptr CircuitBreakerMiddleware::create(const CircuitBreakerConfig& config) {
    return std::shared_ptr<CircuitBreakerMiddleware>(new CircuitBreakerMiddleware(config));
}

CircuitState CircuitBreakerMiddleware::getState() const {
    return m_state.load();
}

void CircuitBreakerMiddleware::transitionTo(CircuitState newState) {
    Mutex::Lock lock(m_mutex);
    CircuitState old = m_state.load();
    if(old == newState) return;
    m_state = newState;
    if(newState == CircuitState::Open) {
        m_openedAt = std::chrono::steady_clock::now();
        m_halfOpenCalls = 0;
        ZERO_LOG_WARN(g_logger) << "Circuit breaker opened";
    } else if(newState == CircuitState::HalfOpen) {
        m_halfOpenCalls = 0;
        ZERO_LOG_INFO(g_logger) << "Circuit breaker half-open";
    } else {
        m_failures = 0;
        m_halfOpenCalls = 0;
        ZERO_LOG_INFO(g_logger) << "Circuit breaker closed";
    }
}

bool CircuitBreakerMiddleware::shouldTrip(HttpResponse::ptr response) const {
    return m_config.isFailure && m_config.isFailure(response);
}

void CircuitBreakerMiddleware::onSuccess() {
    CircuitState state = m_state.load();
    if(state == CircuitState::HalfOpen) {
        uint32_t calls = m_halfOpenCalls.fetch_add(1) + 1;
        if(calls >= m_config.halfOpenMaxCalls) {
            transitionTo(CircuitState::Closed);
        }
    } else if(state == CircuitState::Closed) {
        m_failures = 0;
    }
}

void CircuitBreakerMiddleware::onFailure() {
    CircuitState state = m_state.load();
    if(state == CircuitState::HalfOpen) {
        transitionTo(CircuitState::Open);
    } else if(state == CircuitState::Closed) {
        uint32_t f = m_failures.fetch_add(1) + 1;
        if(f >= m_config.failureThreshold) {
            transitionTo(CircuitState::Open);
        }
    }
}

int32_t CircuitBreakerMiddleware::handle(HttpRequest::ptr request,
                                         HttpResponse::ptr response,
                                         HttpSession::ptr session) {
    (void)session;

    CircuitState state = m_state.load();
    if(state == CircuitState::Open) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_openedAt).count();
        if((uint64_t)elapsed >= m_config.recoveryTimeoutMs) {
            transitionTo(CircuitState::HalfOpen);
            state = CircuitState::HalfOpen;
        } else {
            ZERO_LOG_WARN(g_logger) << "Circuit breaker open, reject: " << request->getPath();
            response->setStatus(HttpStatus::SERVICE_UNAVAILABLE);
            response->setHeader("Content-Type", "application/json");
            response->setBody("{\"error\":\"circuit breaker open\"}");
            return -1;
        }
    }

    // 放行请求，但记录后续结果
    // 由于中间件链无法直接获得后续 Servlet 的返回结果，
    // 这里通过 response 状态码间接判断：如果后续处理已经设置了状态码，则据此统计。
    // 更精确的实现需要改造中间件链支持 post-process 回调。
    if(response->getStatus() != HttpStatus::OK) {
        // 已经有错误状态，可能是前面中间件设置的
        if(shouldTrip(response)) {
            onFailure();
        } else {
            onSuccess();
        }
        return 0;
    }

    // 没有预设状态码，交给后续处理；这里无法直接得知结果，所以只做状态预检。
    // 实际精确统计建议在业务 Servlet 中显式调用 onSuccess/onFailure。
    return 0;
}

} // namespace http
} // namespace zero

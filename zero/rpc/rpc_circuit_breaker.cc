/**
 * @file rpc_circuit_breaker.cc
 * @brief RPC 细粒度熔断器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_circuit_breaker.h"

namespace zero {
namespace rpc {

static uint64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

RpcCircuitBreaker::RpcCircuitBreaker(const RpcCircuitBreakerConfig& config)
    : m_config(config) {
}

bool RpcCircuitBreaker::allowRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == RpcCircuitState::Closed) {
        return true;
    }
    if (m_state == RpcCircuitState::Open) {
        if (nowMs() - m_openedAt >= m_config.openDurationMs) {
            transitionTo(RpcCircuitState::HalfOpen);
            m_halfOpenCalls = 1;
            return true;
        }
        return false;
    }
    // HalfOpen
    if (m_halfOpenCalls < m_config.halfOpenMaxCalls) {
        ++m_halfOpenCalls;
        return true;
    }
    return false;
}

void RpcCircuitBreaker::recordSuccess() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == RpcCircuitState::HalfOpen) {
        ++m_successes;
        if (m_successes >= m_config.successThreshold) {
            transitionTo(RpcCircuitState::Closed);
        }
    } else if (m_state == RpcCircuitState::Closed) {
        if (m_failures > 0) --m_failures;
    }
}

void RpcCircuitBreaker::recordFailure() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state == RpcCircuitState::HalfOpen) {
        transitionTo(RpcCircuitState::Open);
        return;
    }
    ++m_failures;
    if (m_failures >= m_config.failureThreshold) {
        transitionTo(RpcCircuitState::Open);
    }
}

RpcCircuitState RpcCircuitBreaker::state() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

std::string RpcCircuitBreaker::stateString() const {
    switch (state()) {
        case RpcCircuitState::Closed: return "closed";
        case RpcCircuitState::Open: return "open";
        case RpcCircuitState::HalfOpen: return "half-open";
    }
    return "unknown";
}

void RpcCircuitBreaker::transitionTo(RpcCircuitState state) {
    m_state = state;
    if (state == RpcCircuitState::Closed) {
        m_failures = 0;
        m_successes = 0;
        m_halfOpenCalls = 0;
    } else if (state == RpcCircuitState::Open) {
        m_openedAt = nowMs();
        m_successes = 0;
        m_halfOpenCalls = 0;
    } else if (state == RpcCircuitState::HalfOpen) {
        m_failures = 0;
        m_successes = 0;
        m_halfOpenCalls = 0;
    }
}

RpcCircuitBreakerManager::RpcCircuitBreakerManager(const RpcCircuitBreakerConfig& defaultConfig)
    : m_defaultConfig(defaultConfig) {
}

RpcCircuitBreaker::ptr RpcCircuitBreakerManager::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_breakers.find(key);
    if (it != m_breakers.end()) {
        return it->second;
    }
    auto configIt = m_configs.find(key);
    auto config = (configIt != m_configs.end()) ? configIt->second : m_defaultConfig;
    auto cb = std::make_shared<RpcCircuitBreaker>(config);
    m_breakers[key] = cb;
    return cb;
}

bool RpcCircuitBreakerManager::allowRequest(const std::string& key) {
    return get(key)->allowRequest();
}

void RpcCircuitBreakerManager::recordSuccess(const std::string& key) {
    get(key)->recordSuccess();
}

void RpcCircuitBreakerManager::recordFailure(const std::string& key) {
    get(key)->recordFailure();
}

void RpcCircuitBreakerManager::setConfig(const std::string& key, const RpcCircuitBreakerConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_configs[key] = config;
}

std::map<std::string, std::string> RpcCircuitBreakerManager::getStates() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::map<std::string, std::string> result;
    for (const auto& kv : m_breakers) {
        result[kv.first] = kv.second->stateString();
    }
    return result;
}

} // namespace rpc
} // namespace zero

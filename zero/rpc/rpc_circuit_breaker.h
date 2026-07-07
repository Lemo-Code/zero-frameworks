/**
 * @file rpc_circuit_breaker.h
 * @brief RPC 细粒度熔断器（按服务/方法维度）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_CIRCUIT_BREAKER_H__
#define __ZERO_RPC_CIRCUIT_BREAKER_H__

#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>

namespace zero {
namespace rpc {

/**
 * @brief 熔断器状态
 */
enum class RpcCircuitState {
    Closed,     ///< 关闭，正常请求
    Open,       ///< 打开，快速失败
    HalfOpen    ///< 半开，试探请求
};

/**
 * @brief 熔断器配置
 */
struct RpcCircuitBreakerConfig {
    int failureThreshold = 5;           ///< 连续失败阈值
    int successThreshold = 2;           ///< 半开成功阈值
    uint64_t openDurationMs = 5000;     ///< 打开状态持续时间
    uint64_t halfOpenMaxCalls = 3;      ///< 半开状态最大试探请求数
};

/**
 * @brief 单个熔断器
 */
class RpcCircuitBreaker {
public:
    typedef std::shared_ptr<RpcCircuitBreaker> ptr;

    explicit RpcCircuitBreaker(const RpcCircuitBreakerConfig& config = RpcCircuitBreakerConfig());

    /**
     * @brief 是否允许请求通过
     */
    bool allowRequest();

    /**
     * @brief 记录成功
     */
    void recordSuccess();

    /**
     * @brief 记录失败
     */
    void recordFailure();

    RpcCircuitState state() const;
    std::string stateString() const;

private:
    void transitionTo(RpcCircuitState state);

    RpcCircuitBreakerConfig m_config;
    mutable std::mutex m_mutex;
    RpcCircuitState m_state = RpcCircuitState::Closed;
    int m_failures = 0;
    int m_successes = 0;
    uint64_t m_halfOpenCalls = 0;
    uint64_t m_openedAt = 0;
};

/**
 * @brief 熔断器管理器（按 key 管理，key = service#method）
 */
class RpcCircuitBreakerManager {
public:
    typedef std::shared_ptr<RpcCircuitBreakerManager> ptr;

    explicit RpcCircuitBreakerManager(const RpcCircuitBreakerConfig& defaultConfig = RpcCircuitBreakerConfig());

    RpcCircuitBreaker::ptr get(const std::string& key);

    bool allowRequest(const std::string& key);
    void recordSuccess(const std::string& key);
    void recordFailure(const std::string& key);

    void setConfig(const std::string& key, const RpcCircuitBreakerConfig& config);

    std::map<std::string, std::string> getStates() const;

private:
    RpcCircuitBreakerConfig m_defaultConfig;
    mutable std::mutex m_mutex;
    std::map<std::string, RpcCircuitBreaker::ptr> m_breakers;
    std::map<std::string, RpcCircuitBreakerConfig> m_configs;
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_CIRCUIT_BREAKER_H__

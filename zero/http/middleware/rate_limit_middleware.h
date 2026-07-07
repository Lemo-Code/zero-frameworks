/**
 * @file rate_limit_middleware.h
 * @brief 限流中间件（令牌桶算法）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_RATE_LIMIT_H__
#define __ZERO_HTTP_MIDDLEWARE_RATE_LIMIT_H__

#include "zero/http/middleware.h"
#include "zero/core/concurrency/mutex.h"
#include <chrono>
#include <deque>
#include <unordered_map>
#include <functional>

namespace zero {
namespace http {

/**
 * @brief 限流键提取函数
 *
 * 默认按客户端 IP 限流；用户可以自定义（如按用户 ID、API Key 等）。
 */
typedef std::function<std::string(HttpRequest::ptr)> RateLimitKeyExtractor;

/**
 * @brief 限流算法类型
 */
enum class RateLimitAlgorithm {
    TokenBucket,    ///< 令牌桶
    SlidingWindow,  ///< 滑动窗口
    LeakyBucket     ///< 漏桶
};

/**
 * @brief 限流器接口
 */
class IRateLimiter {
public:
    typedef std::shared_ptr<IRateLimiter> ptr;
    virtual ~IRateLimiter() = default;
    virtual bool acquire() = 0;
};

/**
 * @brief 令牌桶限流器
 */
class TokenBucketRateLimiter : public IRateLimiter {
public:
    typedef std::shared_ptr<TokenBucketRateLimiter> ptr;

    /**
     * @brief 构造函数
     * @param[in] rate 每秒产生令牌数
     * @param[in] burst 桶容量（最大突发流量）
     */
    TokenBucketRateLimiter(double rate, double burst);

    bool acquire() override;

private:
    double m_rate;
    double m_burst;
    double m_tokens;
    std::chrono::steady_clock::time_point m_lastTime;
    Mutex m_mutex;
};

/**
 * @brief 滑动窗口限流器
 */
class SlidingWindowRateLimiter : public IRateLimiter {
public:
    typedef std::shared_ptr<SlidingWindowRateLimiter> ptr;

    /**
     * @brief 构造函数
     * @param[in] maxRequests 窗口内最大请求数
     * @param[in] windowMs 窗口大小（毫秒）
     */
    SlidingWindowRateLimiter(int maxRequests, int windowMs);

    bool acquire() override;

private:
    int m_maxRequests;
    int m_windowMs;
    std::deque<std::chrono::steady_clock::time_point> m_records;
    Mutex m_mutex;
};

/**
 * @brief 漏桶限流器
 */
class LeakyBucketRateLimiter : public IRateLimiter {
public:
    typedef std::shared_ptr<LeakyBucketRateLimiter> ptr;

    /**
     * @brief 构造函数
     * @param[in] rate 每秒漏出速率
     * @param[in] capacity 桶容量
     */
    LeakyBucketRateLimiter(double rate, double capacity);

    bool acquire() override;

private:
    double m_rate;
    double m_capacity;
    double m_water;
    std::chrono::steady_clock::time_point m_lastTime;
    Mutex m_mutex;
};

/**
 * @brief 限流中间件
 * 
 * 如果请求被限流，返回 HTTP 429 Too Many Requests。
 */
class RateLimitMiddleware : public Middleware {
public:
    typedef std::shared_ptr<RateLimitMiddleware> ptr;

    /**
     * @brief 全局限流（默认令牌桶）
     * @param[in] rate 每秒请求数
     * @param[in] burst 突发容量
     */
    static RateLimitMiddleware::ptr global(double rate, double burst);

    /**
     * @brief 全局限流
     * @param[in] algo 限流算法
     * @param[in] rate 每秒请求数 / 窗口最大请求数（取决于算法）
     * @param[in] burst 突发容量 / 窗口大小毫秒（取决于算法）
     */
    static RateLimitMiddleware::ptr global(RateLimitAlgorithm algo, double rate, double burst);

    /**
     * @brief 按 key 限流（默认令牌桶）
     * @param[in] rate 每秒请求数
     * @param[in] burst 突发容量
     * @param[in] extractor 限流键提取函数
     */
    static RateLimitMiddleware::ptr perKey(double rate, double burst,
                                           RateLimitKeyExtractor extractor);

    /**
     * @brief 按 key 限流
     * @param[in] algo 限流算法
     */
    static RateLimitMiddleware::ptr perKey(RateLimitAlgorithm algo, double rate, double burst,
                                           RateLimitKeyExtractor extractor);

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    RateLimitMiddleware(RateLimitAlgorithm algo, double rate, double burst,
                        RateLimitKeyExtractor extractor);

    static IRateLimiter::ptr createLimiter(RateLimitAlgorithm algo, double rate, double burst);

    RateLimitAlgorithm m_algo;
    double m_rate;
    double m_burst;
    RateLimitKeyExtractor m_extractor;
    IRateLimiter::ptr m_globalLimiter;
    std::unordered_map<std::string, IRateLimiter::ptr> m_limiters;
    Mutex m_mutex;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_RATE_LIMIT_H__

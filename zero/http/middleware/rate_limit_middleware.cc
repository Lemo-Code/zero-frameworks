/**
 * @file rate_limit_middleware.cc
 * @brief 限流中间件实现（令牌桶算法）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rate_limit_middleware.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// ============================================================
// TokenBucketRateLimiter
// ============================================================

TokenBucketRateLimiter::TokenBucketRateLimiter(double rate, double burst)
    : m_rate(rate)
    , m_burst(burst)
    , m_tokens(burst)
    , m_lastTime(std::chrono::steady_clock::now()) {
}

bool TokenBucketRateLimiter::acquire() {
    Mutex::Lock lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - m_lastTime).count() / 1000000.0;
    m_lastTime = now;

    m_tokens += elapsed * m_rate;
    if(m_tokens > m_burst) {
        m_tokens = m_burst;
    }

    if(m_tokens < 1.0) {
        return false;
    }
    m_tokens -= 1.0;
    return true;
}

// ============================================================
// SlidingWindowRateLimiter
// ============================================================

SlidingWindowRateLimiter::SlidingWindowRateLimiter(int maxRequests, int windowMs)
    : m_maxRequests(maxRequests)
    , m_windowMs(windowMs) {
}

bool SlidingWindowRateLimiter::acquire() {
    Mutex::Lock lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    while(!m_records.empty()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_records.front()).count();
        if(elapsed >= m_windowMs) {
            m_records.pop_front();
        } else {
            break;
        }
    }
    if((int)m_records.size() >= m_maxRequests) {
        return false;
    }
    m_records.push_back(now);
    return true;
}

// ============================================================
// LeakyBucketRateLimiter
// ============================================================

LeakyBucketRateLimiter::LeakyBucketRateLimiter(double rate, double capacity)
    : m_rate(rate)
    , m_capacity(capacity)
    , m_water(0.0)
    , m_lastTime(std::chrono::steady_clock::now()) {
}

bool LeakyBucketRateLimiter::acquire() {
    Mutex::Lock lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - m_lastTime).count() / 1000000.0;
    m_lastTime = now;

    m_water -= elapsed * m_rate;
    if(m_water < 0.0) {
        m_water = 0.0;
    }

    if(m_water + 1.0 > m_capacity) {
        return false;
    }
    m_water += 1.0;
    return true;
}

// ============================================================
// RateLimitMiddleware
// ============================================================

RateLimitMiddleware::RateLimitMiddleware(RateLimitAlgorithm algo, double rate, double burst,
                                         RateLimitKeyExtractor extractor)
    : m_algo(algo)
    , m_rate(rate)
    , m_burst(burst)
    , m_extractor(extractor) {
    if(!m_extractor) {
        m_globalLimiter = createLimiter(algo, rate, burst);
    }
}

IRateLimiter::ptr RateLimitMiddleware::createLimiter(RateLimitAlgorithm algo,
                                                     double rate, double burst) {
    switch(algo) {
        case RateLimitAlgorithm::SlidingWindow:
            return std::make_shared<SlidingWindowRateLimiter>(static_cast<int>(rate),
                                                               static_cast<int>(burst));
        case RateLimitAlgorithm::LeakyBucket:
            return std::make_shared<LeakyBucketRateLimiter>(rate, burst);
        case RateLimitAlgorithm::TokenBucket:
        default:
            return std::make_shared<TokenBucketRateLimiter>(rate, burst);
    }
}

RateLimitMiddleware::ptr RateLimitMiddleware::global(double rate, double burst) {
    return global(RateLimitAlgorithm::TokenBucket, rate, burst);
}

RateLimitMiddleware::ptr RateLimitMiddleware::global(RateLimitAlgorithm algo,
                                                     double rate, double burst) {
    return std::shared_ptr<RateLimitMiddleware>(
        new RateLimitMiddleware(algo, rate, burst, nullptr));
}

RateLimitMiddleware::ptr RateLimitMiddleware::perKey(double rate, double burst,
                                                     RateLimitKeyExtractor extractor) {
    return perKey(RateLimitAlgorithm::TokenBucket, rate, burst, extractor);
}

RateLimitMiddleware::ptr RateLimitMiddleware::perKey(RateLimitAlgorithm algo,
                                                     double rate, double burst,
                                                     RateLimitKeyExtractor extractor) {
    if(!extractor) {
        return global(algo, rate, burst);
    }
    return std::shared_ptr<RateLimitMiddleware>(
        new RateLimitMiddleware(algo, rate, burst, extractor));
}

int32_t RateLimitMiddleware::handle(HttpRequest::ptr request,
                                    HttpResponse::ptr response,
                                    HttpSession::ptr session) {
    bool allowed = false;
    if(m_extractor) {
        std::string key = m_extractor(request);
        IRateLimiter::ptr limiter;
        {
            Mutex::Lock lock(m_mutex);
            auto it = m_limiters.find(key);
            if(it == m_limiters.end()) {
                limiter = createLimiter(m_algo, m_rate, m_burst);
                m_limiters[key] = limiter;
            } else {
                limiter = it->second;
            }
        }
        allowed = limiter->acquire();
    } else {
        allowed = m_globalLimiter->acquire();
    }

    if(!allowed) {
        ZERO_LOG_WARN(g_logger) << "Rate limit exceeded: " << request->getPath();
        response->setStatus(HttpStatus::TOO_MANY_REQUESTS);
        response->setHeader("Content-Type", "application/json");
        response->setBody("{\"error\":\"too many requests\"}");
        return -1;
    }
    return 0;
}

} // namespace http
} // namespace zero

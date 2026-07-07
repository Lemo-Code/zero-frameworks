/**
 * @file log_filter.cc
 * @brief 日志过滤器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_filter.h"
#include "log.h"
#include <random>
#include <ctime>

namespace zero {

// ======================== LevelFilter ========================

FilterResult LevelFilter::decide(std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) {
    if (level >= m_threshold) {
        return m_accept_on_match ? FilterResult::ACCEPT : FilterResult::DENY;
    }
    return m_accept_on_match ? FilterResult::DENY : FilterResult::ACCEPT;
}

// ======================== CategoryFilter ========================

CategoryFilter::CategoryFilter(const std::string& pattern, bool is_regex, bool accept_on_match)
    : m_pattern(pattern)
    , m_is_regex(is_regex)
    , m_accept_on_match(accept_on_match) {
    if (is_regex) {
        m_regex_pattern = pattern;
        m_regex = std::regex(pattern);
    } else {
        m_regex_pattern = wildcardToRegex(pattern);
        m_regex = std::regex(m_regex_pattern);
    }
}

std::string CategoryFilter::wildcardToRegex(const std::string& wildcard) {
    std::string result = "^";
    for (char c : wildcard) {
        switch (c) {
            case '*': result += ".*"; break;
            case '?': result += "."; break;
            case '.': result += "\\."; break;
            case '+': result += "\\+"; break;
            case '[': result += "\\["; break;
            case ']': result += "\\]"; break;
            case '(': result += "\\("; break;
            case ')': result += "\\)"; break;
            case '\\': result += "\\\\"; break;
            default: result += c;
        }
    }
    result += "$";
    return result;
}

FilterResult CategoryFilter::decide(std::shared_ptr<Logger> logger, LogLevel::Level, LogEvent::ptr) {
    if (!logger) return FilterResult::NEUTRAL;

    bool matched = std::regex_match(logger->getName(), m_regex);
    if (matched) {
        return m_accept_on_match ? FilterResult::ACCEPT : FilterResult::DENY;
    }
    return m_accept_on_match ? FilterResult::DENY : FilterResult::ACCEPT;
}

// ======================== ThreadFilter ========================

FilterResult ThreadFilter::decide(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) {
    if (!event) return FilterResult::NEUTRAL;
    bool matched = (event->getThreadId() == m_thread_id);
    return matched == m_accept_on_match ? FilterResult::ACCEPT : FilterResult::DENY;
}

// ======================== RateLimitFilter ========================

RateLimitFilter::RateLimitFilter(size_t max_per_second, size_t burst_size)
    : m_max_per_second(max_per_second)
    , m_burst_size(burst_size > 0 ? burst_size : max_per_second)
    , m_tokens(m_burst_size)
    , m_accepted(0)
    , m_denied(0)
    , m_last_refill(std::chrono::steady_clock::now()) {
}

void RateLimitFilter::setRate(size_t max_per_second) {
    MutexType::Lock lock(m_mutex);
    m_max_per_second = max_per_second;
    m_burst_size = max_per_second;
    size_t current = m_tokens.load(std::memory_order_acquire);
    if (current > m_burst_size) {
        m_tokens.store(m_burst_size, std::memory_order_release);
    }
}

FilterResult RateLimitFilter::decide(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) {
    // 快速路径: 如果限速为0，全部通过
    if (m_max_per_second == 0) {
        return FilterResult::ACCEPT;
    }

    // 补充令牌
    auto now = std::chrono::steady_clock::now();
    {
        MutexType::Lock lock(m_mutex);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_refill).count();
        if (elapsed >= 100) {  // 每100ms补充一次
            size_t new_tokens = static_cast<size_t>(elapsed * m_max_per_second / 1000);
            size_t current = m_tokens.load(std::memory_order_acquire);
            size_t refilled = std::min(current + new_tokens, m_burst_size);
            m_tokens.store(refilled, std::memory_order_release);
            m_last_refill = now;
        }
    }

    // 尝试获取令牌
    size_t current = m_tokens.load(std::memory_order_acquire);
    while (current > 0) {
        if (m_tokens.compare_exchange_weak(current, current - 1,
                std::memory_order_release, std::memory_order_relaxed)) {
            m_accepted.fetch_add(1, std::memory_order_relaxed);
            return FilterResult::ACCEPT;
        }
    }

    m_denied.fetch_add(1, std::memory_order_relaxed);
    return FilterResult::DENY;
}

// ======================== SampleFilter ========================

SampleFilter::SampleFilter(int sample_rate)
    : m_sample_rate(std::max(1, std::min(100, sample_rate)))
    , m_counter(0) {
}

FilterResult SampleFilter::decide(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) {
    if (m_sample_rate >= 100) return FilterResult::ACCEPT;
    if (m_sample_rate <= 0) return FilterResult::DENY;

    uint64_t count = m_counter.fetch_add(1, std::memory_order_relaxed);
    // 使用简单的确定性采样
    if ((count % 100) < static_cast<uint64_t>(m_sample_rate)) {
        return FilterResult::ACCEPT;
    }
    return FilterResult::DENY;
}

// ======================== AndFilter ========================

void AndFilter::addFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    m_filters.push_back(filter);
}

void AndFilter::removeFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    auto it = std::find(m_filters.begin(), m_filters.end(), filter);
    if (it != m_filters.end()) {
        m_filters.erase(it);
    }
}

void AndFilter::clearFilters() {
    MutexType::Lock lock(m_mutex);
    m_filters.clear();
}

FilterResult AndFilter::decide(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    MutexType::Lock lock(m_mutex);
    if (m_filters.empty()) return FilterResult::ACCEPT;

    bool has_neutral = false;
    for (auto& f : m_filters) {
        FilterResult r = f->decide(logger, level, event);
        if (r == FilterResult::DENY) return FilterResult::DENY;
        if (r == FilterResult::NEUTRAL) has_neutral = true;
    }
    return has_neutral ? FilterResult::NEUTRAL : FilterResult::ACCEPT;
}

// ======================== OrFilter ========================

void OrFilter::addFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    m_filters.push_back(filter);
}

void OrFilter::removeFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    auto it = std::find(m_filters.begin(), m_filters.end(), filter);
    if (it != m_filters.end()) {
        m_filters.erase(it);
    }
}

void OrFilter::clearFilters() {
    MutexType::Lock lock(m_mutex);
    m_filters.clear();
}

FilterResult OrFilter::decide(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    MutexType::Lock lock(m_mutex);
    if (m_filters.empty()) return FilterResult::ACCEPT;

    for (auto& f : m_filters) {
        FilterResult r = f->decide(logger, level, event);
        if (r == FilterResult::ACCEPT) return FilterResult::ACCEPT;
    }
    return FilterResult::NEUTRAL;
}

// ======================== TimeRangeFilter ========================

TimeRangeFilter::TimeRangeFilter(int start_hour, int start_min, int end_hour, int end_min)
    : m_start_minutes(start_hour * 60 + start_min)
    , m_end_minutes(end_hour * 60 + end_min) {
}

FilterResult TimeRangeFilter::decide(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) {
    if (!event) return FilterResult::NEUTRAL;

    time_t t = event->getTime();
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    int current_minutes = tm_info.tm_hour * 60 + tm_info.tm_min;

    if (m_start_minutes <= m_end_minutes) {
        // 正常时间段 (如 09:00 - 17:00)
        return (current_minutes >= m_start_minutes && current_minutes <= m_end_minutes)
               ? FilterResult::ACCEPT : FilterResult::DENY;
    } else {
        // 跨天时间段 (如 22:00 - 06:00)
        return (current_minutes >= m_start_minutes || current_minutes <= m_end_minutes)
               ? FilterResult::ACCEPT : FilterResult::DENY;
    }
}

} // namespace zero

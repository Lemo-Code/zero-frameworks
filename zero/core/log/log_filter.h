/**
 * @file log_filter.h
 * @brief 日志过滤器组件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_FILTER_H__
#define __ZERO_LOG_FILTER_H__

#include <string>
#include <memory>
#include <vector>
#include <regex>
#include <atomic>
#include <chrono>
#include "log_level.h"
#include "log_event.h"
#include "zero/core/concurrency/mutex.h"
#include "zero/core/base/noncopyable.h"

namespace zero {

class Logger;
class LogEvent;

/**
 * @brief 日志过滤器结果
 */
enum class FilterResult {
    DENY  = 0,    /// 拒绝
    NEUTRAL = 1,  /// 中立 (继续下一个过滤器)
    ACCEPT = 2    /// 接受
};

/**
 * @brief 日志过滤器抽象基类
 */
class LogFilter {
public:
    typedef std::shared_ptr<LogFilter> ptr;

    virtual ~LogFilter() {}

    /**
     * @brief 过滤决策
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     * @return 过滤结果
     */
    virtual FilterResult decide(std::shared_ptr<Logger> logger,
                                 LogLevel::Level level,
                                 LogEvent::ptr event) = 0;

    /**
     * @brief 获取过滤器名称
     */
    virtual std::string getName() const = 0;

    /**
     * @brief 获取过滤器描述
     */
    virtual std::string getDescription() const { return ""; }
};

// ======================== 具体过滤器实现 ========================

/**
 * @brief 级别过滤器
 * @details 当日志级别 >= 配置的阈值时接受
 */
class LevelFilter : public LogFilter {
public:
    typedef std::shared_ptr<LevelFilter> ptr;

    explicit LevelFilter(LogLevel::Level threshold, bool accept_on_match = true)
        : m_threshold(threshold), m_accept_on_match(accept_on_match) {}

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "LevelFilter"; }
    std::string getDescription() const override {
        return "Threshold: " + std::string(LogLevel::ToString(m_threshold));
    }

    void setThreshold(LogLevel::Level level) { m_threshold = level; }
    LogLevel::Level getThreshold() const { return m_threshold; }

private:
    LogLevel::Level m_threshold;
    bool m_accept_on_match;
};

/**
 * @brief Logger名称过滤器 (支持正则)
 */
class CategoryFilter : public LogFilter {
public:
    typedef std::shared_ptr<CategoryFilter> ptr;

    /**
     * @brief 构造函数
     * @param[in] pattern 匹配模式 (支持*通配符和正则表达式)
     * @param[in] is_regex 是否为正则表达式
     * @param[in] accept_on_match 匹配时接受还是拒绝
     */
    CategoryFilter(const std::string& pattern, bool is_regex = false,
                    bool accept_on_match = true);

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "CategoryFilter"; }
    std::string getDescription() const override { return "Pattern: " + m_pattern; }

private:
    std::string m_pattern;
    std::string m_regex_pattern;
    std::regex m_regex;
    bool m_is_regex;
    bool m_accept_on_match;

    /**
     * @brief 将通配符模式转为正则
     */
    static std::string wildcardToRegex(const std::string& wildcard);
};

/**
 * @brief 线程过滤器
 */
class ThreadFilter : public LogFilter {
public:
    typedef std::shared_ptr<ThreadFilter> ptr;

    ThreadFilter(uint32_t thread_id, bool accept_on_match = true)
        : m_thread_id(thread_id), m_accept_on_match(accept_on_match) {}

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "ThreadFilter"; }

private:
    uint32_t m_thread_id;
    bool m_accept_on_match;
};

/**
 * @brief 令牌桶限速过滤器
 * @details 基于令牌桶算法的限速，控制每秒日志条数
 */
class RateLimitFilter : public LogFilter {
public:
    typedef std::shared_ptr<RateLimitFilter> ptr;
    typedef Spinlock MutexType;

    /**
     * @brief 构造函数
     * @param[in] max_per_second 每秒允许通过的最大日志数
     * @param[in] burst_size 突发容量 (默认= max_per_second)
     */
    explicit RateLimitFilter(size_t max_per_second, size_t burst_size = 0);

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "RateLimitFilter"; }
    std::string getDescription() const override {
        return "Max/sec: " + std::to_string(m_max_per_second);
    }

    void setRate(size_t max_per_second);
    size_t getRate() const { return m_max_per_second; }
    size_t getCurrentTokens() const { return m_tokens.load(std::memory_order_acquire); }
    uint64_t getAcceptedCount() const { return m_accepted.load(std::memory_order_acquire); }
    uint64_t getDeniedCount() const { return m_denied.load(std::memory_order_acquire); }

private:
    size_t m_max_per_second;
    size_t m_burst_size;
    std::atomic<size_t> m_tokens;
    std::atomic<uint64_t> m_accepted;
    std::atomic<uint64_t> m_denied;
    std::chrono::steady_clock::time_point m_last_refill;
    mutable Spinlock m_mutex;
};

/**
 * @brief 采样过滤器
 * @details 按比例采样日志 (如1%采样率)
 */
class SampleFilter : public LogFilter {
public:
    typedef std::shared_ptr<SampleFilter> ptr;

    /**
     * @param[in] sample_rate 采样率 (1-100, 100=全部通过)
     */
    explicit SampleFilter(int sample_rate = 100);

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "SampleFilter"; }

private:
    int m_sample_rate;
    std::atomic<uint64_t> m_counter;
};

/**
 * @brief 复合过滤器 - AND逻辑 (所有子过滤器都接受)
 */
class AndFilter : public LogFilter {
public:
    typedef std::shared_ptr<AndFilter> ptr;
    typedef Spinlock MutexType;

    void addFilter(LogFilter::ptr filter);
    void removeFilter(LogFilter::ptr filter);
    void clearFilters();

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "AndFilter"; }

private:
    mutable Spinlock m_mutex;
    std::vector<LogFilter::ptr> m_filters;
};

/**
 * @brief 复合过滤器 - OR逻辑 (任一子过滤器接受)
 */
class OrFilter : public LogFilter {
public:
    typedef std::shared_ptr<OrFilter> ptr;
    typedef Spinlock MutexType;

    void addFilter(LogFilter::ptr filter);
    void removeFilter(LogFilter::ptr filter);
    void clearFilters();

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "OrFilter"; }

private:
    mutable Spinlock m_mutex;
    std::vector<LogFilter::ptr> m_filters;
};

/**
 * @brief 时间段过滤器
 * @details 只在指定的时间窗口内接受日志
 */
class TimeRangeFilter : public LogFilter {
public:
    typedef std::shared_ptr<TimeRangeFilter> ptr;

    /**
     * @param[in] start_hour 开始小时 (0-23)
     * @param[in] start_min  开始分钟 (0-59)
     * @param[in] end_hour   结束小时 (0-23)
     * @param[in] end_min    结束分钟 (0-59)
     */
    TimeRangeFilter(int start_hour, int start_min, int end_hour, int end_min);

    FilterResult decide(std::shared_ptr<Logger> logger,
                         LogLevel::Level level,
                         LogEvent::ptr event) override;

    std::string getName() const override { return "TimeRangeFilter"; }

private:
    int m_start_minutes;  // 转换为从0点开始的分钟数
    int m_end_minutes;
};

} // namespace zero

#endif // __ZERO_LOG_FILTER_H__

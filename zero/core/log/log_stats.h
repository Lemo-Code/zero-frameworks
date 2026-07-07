/**
 * @file log_stats.h
 * @brief 日志统计与监控
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_STATS_H__
#define __ZERO_LOG_STATS_H__

#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <map>
#include <chrono>
#include "log_level.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {

/**
 * @brief 日志统计计数器
 */
struct LogCounters {
    std::atomic<uint64_t> total_logs{0};      // 日志总数
    std::atomic<uint64_t> sync_writes{0};     // 同步写入次数
    std::atomic<uint64_t> async_writes{0};    // 异步写入次数
    std::atomic<uint64_t> dropped{0};         // 丢弃次数
    std::atomic<uint64_t> blocked{0};         // 阻塞次数
    std::atomic<uint64_t> filter_denied{0};   // 被过滤器拒绝次数
    std::atomic<uint64_t> write_errors{0};    // 写入错误次数
    std::atomic<uint64_t> total_bytes{0};     // 总写入字节数
    std::atomic<uint64_t> max_queue_size{0};  // 最大队列深度

    /// 各级别计数
    std::atomic<uint64_t> level_counts[10];   // 按LogLevel枚举索引

    void reset();
    std::string toString() const;
};

/**
 * @brief 滑动窗口QPS计算器
 * @details 使用环形缓冲区记录每秒的日志数
 */
class QPSCalculator {
public:
    explicit QPSCalculator(size_t window_size_sec = 60);

    /**
     * @brief 记录一次日志事件
     */
    void record(uint64_t timestamp_sec);

    /**
     * @brief 获取当前QPS (基于整个窗口)
     */
    double getCurrentQPS() const;

    /**
     * @brief 获取瞬时QPS (最近1秒)
     */
    double getInstantQPS() const;

    /**
     * @brief 获取峰值QPS
     */
    double getPeakQPS() const { return m_peak_qps.load(std::memory_order_acquire); }

    /**
     * @brief 获取指定时间窗口内的QPS
     */
    double getQPSForWindow(size_t window_sec) const;

    /**
     * @brief 重置统计
     */
    void reset();

private:
    size_t m_window_size;
    std::vector<std::atomic<uint64_t>> m_buckets;
    std::atomic<uint64_t> m_current_bucket{0};
    std::atomic<uint64_t> m_last_bucket_time{0};
    std::atomic<double> m_peak_qps{0.0};
    mutable Mutex m_mutex;
};

/**
 * @brief 日志延迟直方图
 */
class LogLatencyHistogram {
public:
    /**
     * @param[in] num_buckets 桶数量
     * @param[in] max_latency_us 最大延迟(微秒)
     */
    LogLatencyHistogram(size_t num_buckets = 100, uint64_t max_latency_us = 10000);

    /**
     * @brief 记录一次延迟
     */
    void record(uint64_t latency_us);

    /**
     * @brief 获取百分位延迟
     * @param[in] percentile 百分位 (0-100)
     */
    uint64_t getPercentile(double percentile) const;

    /**
     * @brief 获取平均延迟
     */
    double getAverage() const;

    /**
     * @brief 获取最小/最大延迟
     */
    uint64_t getMin() const { return m_min.load(std::memory_order_acquire); }
    uint64_t getMax() const { return m_max.load(std::memory_order_acquire); }

    /**
     * @brief 获取计数
     */
    uint64_t getCount() const { return m_count.load(std::memory_order_acquire); }

    /**
     * @brief 获取P50/P90/P99/P999
     */
    uint64_t getP50() const { return getPercentile(50); }
    uint64_t getP90() const { return getPercentile(90); }
    uint64_t getP99() const { return getPercentile(99); }
    uint64_t getP999() const { return getPercentile(99.9); }

    void reset();

private:
    uint64_t m_max_latency_us;
    size_t m_num_buckets;
    std::vector<std::atomic<uint64_t>> m_buckets;
    std::atomic<uint64_t> m_count{0};
    std::atomic<uint64_t> m_total_us{0};
    std::atomic<uint64_t> m_min{UINT64_MAX};
    std::atomic<uint64_t> m_max{0};
};

/**
 * @brief 日志统计管理器 (全局单例)
 */
class LogStatsManager {
public:
    typedef Spinlock MutexType;

    /**
     * @brief 获取单例
     */
    static LogStatsManager& GetInstance();

    /**
     * @brief 记录日志事件
     */
    void recordLog(LogLevel::Level level, size_t msg_size, bool is_async,
                   bool was_dropped, bool was_blocked, uint64_t latency_us = 0);

    /**
     * @brief 记录队列深度
     */
    void recordQueueDepth(size_t depth);

    /**
     * @brief 获取全局计数器
     */
    const LogCounters& getCounters() const { return m_counters; }

    /**
     * @brief 获取全局QPS计算器
     */
    QPSCalculator& getQPSCalc() { return m_qps_calc; }

    /**
     * @brief 获取延迟直方图
     */
    LogLatencyHistogram& getLatencyHistogram() { return m_latency_hist; }

    /**
     * @brief 获取格式化报告
     */
    std::string getReport() const;

    /**
     * @brief 重置所有统计
     */
    void reset();

    /**
     * @brief 是否启用统计
     */
    void setEnabled(bool enabled) { m_enabled.store(enabled, std::memory_order_release); }
    bool isEnabled() const { return m_enabled.load(std::memory_order_acquire); }

private:
    LogStatsManager();

    LogCounters m_counters;
    QPSCalculator m_qps_calc;
    LogLatencyHistogram m_latency_hist{1000, 100000};
    std::atomic<bool> m_enabled{true};
};

// ======================== 便捷宏 ========================

/**
 * @brief 获取日志统计报告
 */
#define ZERO_LOG_STATS_REPORT() zero::LogStatsManager::GetInstance().getReport()

/**
 * @brief 获取当前日志QPS
 */
#define ZERO_LOG_STATS_QPS() zero::LogStatsManager::GetInstance().getQPSCalc().getCurrentQPS()

} // namespace zero

#endif // __ZERO_LOG_STATS_H__

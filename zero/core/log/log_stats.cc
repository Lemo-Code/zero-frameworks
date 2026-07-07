/**
 * @file log_stats.cc
 * @brief 日志统计实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_stats.h"
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cmath>

namespace zero {

// ======================== LogCounters ========================

void LogCounters::reset() {
    total_logs.store(0);
    sync_writes.store(0);
    async_writes.store(0);
    dropped.store(0);
    blocked.store(0);
    filter_denied.store(0);
    write_errors.store(0);
    total_bytes.store(0);
    max_queue_size.store(0);
    for (int i = 0; i < 10; ++i) {
        level_counts[i].store(0);
    }
}

std::string LogCounters::toString() const {
    std::stringstream ss;
    ss << "total=" << total_logs.load()
       << " sync=" << sync_writes.load()
       << " async=" << async_writes.load()
       << " dropped=" << dropped.load()
       << " blocked=" << blocked.load()
       << " denied=" << filter_denied.load()
       << " errors=" << write_errors.load()
       << " bytes=" << total_bytes.load()
       << " max_queue=" << max_queue_size.load();
    return ss.str();
}

// ======================== QPSCalculator ========================

QPSCalculator::QPSCalculator(size_t window_size_sec)
    : m_window_size(window_size_sec)
    , m_buckets(window_size_sec) {
    for (auto& b : m_buckets) {
        b.store(0);
    }
}

void QPSCalculator::record(uint64_t timestamp_sec) {
    uint64_t bucket_idx = timestamp_sec % m_window_size;
    uint64_t last_time = m_last_bucket_time.load(std::memory_order_acquire);

    if (timestamp_sec != last_time) {
        // 新的秒，如果跳过了某些桶则清零
        if (timestamp_sec > last_time + 1) {
            for (uint64_t t = last_time + 1; t < timestamp_sec; ++t) {
                m_buckets[t % m_window_size].store(0);
            }
        }
        m_buckets[bucket_idx].store(1);
        m_last_bucket_time.store(timestamp_sec, std::memory_order_release);

        // 更新峰值
        double current = getInstantQPS();
        double peak = m_peak_qps.load(std::memory_order_acquire);
        while (current > peak) {
            if (m_peak_qps.compare_exchange_weak(peak, current,
                    std::memory_order_release, std::memory_order_relaxed)) {
                break;
            }
        }
    } else {
        m_buckets[bucket_idx].fetch_add(1, std::memory_order_relaxed);
    }
}

double QPSCalculator::getCurrentQPS() const {
    return getQPSForWindow(m_window_size);
}

double QPSCalculator::getInstantQPS() const {
    uint64_t last_time = m_last_bucket_time.load(std::memory_order_acquire);
    if (last_time == 0) return 0.0;
    uint64_t idx = last_time % m_window_size;
    return static_cast<double>(m_buckets[idx].load(std::memory_order_acquire));
}

void QPSCalculator::reset() {
    for (auto& b : m_buckets) {
        b.store(0);
    }
    m_current_bucket.store(0);
    m_last_bucket_time.store(0);
    m_peak_qps.store(0.0);
}

double QPSCalculator::getQPSForWindow(size_t window_sec) const {
    window_sec = std::min(window_sec, m_window_size);
    uint64_t total = 0;
    uint64_t last_time = m_last_bucket_time.load(std::memory_order_acquire);

    for (size_t i = 0; i < window_sec; ++i) {
        uint64_t t = (last_time >= i) ? (last_time - i) : 0;
        if (t == 0) break;
        total += m_buckets[t % m_window_size].load(std::memory_order_acquire);
    }

    return window_sec > 0 ? static_cast<double>(total) / window_sec : 0.0;
}

// ======================== LogLatencyHistogram ========================

LogLatencyHistogram::LogLatencyHistogram(size_t num_buckets, uint64_t max_latency_us)
    : m_max_latency_us(max_latency_us)
    , m_num_buckets(num_buckets)
    , m_buckets(num_buckets) {
    for (auto& b : m_buckets) {
        b.store(0);
    }
}

void LogLatencyHistogram::record(uint64_t latency_us) {
    m_count.fetch_add(1, std::memory_order_relaxed);
    m_total_us.fetch_add(latency_us, std::memory_order_relaxed);

    // 更新最小值
    uint64_t old_min = m_min.load(std::memory_order_acquire);
    while (latency_us < old_min) {
        if (m_min.compare_exchange_weak(old_min, latency_us,
                std::memory_order_release, std::memory_order_relaxed)) {
            break;
        }
    }

    // 更新最大值
    uint64_t old_max = m_max.load(std::memory_order_acquire);
    while (latency_us > old_max) {
        if (m_max.compare_exchange_weak(old_max, latency_us,
                std::memory_order_release, std::memory_order_relaxed)) {
            break;
        }
    }

    // 放入桶
    size_t bucket_idx;
    if (latency_us >= m_max_latency_us) {
        bucket_idx = m_num_buckets - 1;
    } else {
        bucket_idx = static_cast<size_t>(
            latency_us * m_num_buckets / m_max_latency_us);
    }
    m_buckets[bucket_idx].fetch_add(1, std::memory_order_relaxed);
}

uint64_t LogLatencyHistogram::getPercentile(double percentile) const {
    uint64_t total = m_count.load(std::memory_order_acquire);
    if (total == 0) return 0;

    uint64_t target = static_cast<uint64_t>(total * percentile / 100.0);
    uint64_t accumulated = 0;

    for (size_t i = 0; i < m_num_buckets; ++i) {
        accumulated += m_buckets[i].load(std::memory_order_acquire);
        if (accumulated >= target) {
            return (i + 1) * m_max_latency_us / m_num_buckets;
        }
    }
    return m_max_latency_us;
}

double LogLatencyHistogram::getAverage() const {
    uint64_t cnt = m_count.load(std::memory_order_acquire);
    if (cnt == 0) return 0;
    return static_cast<double>(m_total_us.load(std::memory_order_acquire)) / cnt;
}

void LogLatencyHistogram::reset() {
    m_count.store(0);
    m_total_us.store(0);
    m_min.store(UINT64_MAX);
    m_max.store(0);
    for (auto& b : m_buckets) {
        b.store(0);
    }
}

// ======================== LogStatsManager ========================

LogStatsManager& LogStatsManager::GetInstance() {
    static LogStatsManager instance;
    return instance;
}

LogStatsManager::LogStatsManager()
    : m_qps_calc(60) {
}

void LogStatsManager::recordLog(LogLevel::Level level, size_t msg_size, bool is_async,
                                  bool was_dropped, bool was_blocked, uint64_t latency_us) {
    if (!m_enabled.load(std::memory_order_acquire)) return;

    m_counters.total_logs.fetch_add(1, std::memory_order_relaxed);
    m_counters.total_bytes.fetch_add(msg_size, std::memory_order_relaxed);

    if (is_async) {
        m_counters.async_writes.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_counters.sync_writes.fetch_add(1, std::memory_order_relaxed);
    }

    if (was_dropped) {
        m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
    }
    if (was_blocked) {
        m_counters.blocked.fetch_add(1, std::memory_order_relaxed);
    }

    // 级别计数
    int level_idx = static_cast<int>(level);
    if (level_idx >= 0 && level_idx < 10) {
        m_counters.level_counts[level_idx].fetch_add(1, std::memory_order_relaxed);
    }

    // QPS
    m_qps_calc.record(time(nullptr));

    // 延迟
    if (latency_us > 0) {
        m_latency_hist.record(latency_us);
    }
}

void LogStatsManager::recordQueueDepth(size_t depth) {
    if (!m_enabled.load(std::memory_order_acquire)) return;

    uint64_t old_max = m_counters.max_queue_size.load(std::memory_order_acquire);
    while (depth > old_max) {
        if (m_counters.max_queue_size.compare_exchange_weak(old_max, depth,
                std::memory_order_release, std::memory_order_relaxed)) {
            break;
        }
    }
}

std::string LogStatsManager::getReport() const {
    std::stringstream ss;
    ss << "========================================\n";
    ss << "        Log Statistics Report\n";
    ss << "========================================\n";
    ss << m_counters.toString() << "\n";
    ss << "----------------------------------------\n";
    ss << "QPS Current:  " << std::fixed << std::setprecision(1)
       << m_qps_calc.getCurrentQPS() << "\n";
    ss << "QPS Instant:  " << std::fixed << std::setprecision(1)
       << m_qps_calc.getInstantQPS() << "\n";
    ss << "QPS Peak:     " << std::fixed << std::setprecision(1)
       << m_qps_calc.getPeakQPS() << "\n";
    ss << "----------------------------------------\n";
    ss << "Latency P50:  " << m_latency_hist.getP50() << " us\n";
    ss << "Latency P90:  " << m_latency_hist.getP90() << " us\n";
    ss << "Latency P99:  " << m_latency_hist.getP99() << " us\n";
    ss << "Latency P999: " << m_latency_hist.getP999() << " us\n";
    ss << "Latency Avg:  " << std::fixed << std::setprecision(1)
       << m_latency_hist.getAverage() << " us\n";
    ss << "Latency Min:  " << m_latency_hist.getMin() << " us\n";
    ss << "Latency Max:  " << m_latency_hist.getMax() << " us\n";
    ss << "Latency Count:" << m_latency_hist.getCount() << "\n";
    ss << "========================================\n";
    return ss.str();
}

void LogStatsManager::reset() {
    m_counters.reset();
    m_qps_calc.reset();
    m_latency_hist.reset();
}

} // namespace zero

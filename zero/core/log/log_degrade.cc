/**
 * @file log_degrade.cc
 * @brief 日志降级控制实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_degrade.h"
#include <chrono>
#include <algorithm>

namespace zero {

DegradeManager::DegradeManager() {
    // 默认不降级 - 所有级别使用NONE策略
    m_global_strategy.strategy = DegradeStrategy::NONE;
}

void DegradeManager::setWatermarkConfig(const WatermarkConfig& cfg) {
    MutexType::Lock lock(m_mutex);
    m_watermark_cfg = cfg;
}

void DegradeManager::setLevelStrategy(LogLevel::Level level, const DegradeConfig& cfg) {
    MutexType::Lock lock(m_mutex);
    m_level_strategies[level] = cfg;
}

DegradeConfig DegradeManager::getLevelStrategy(LogLevel::Level level) const {
    MutexType::Lock lock(m_mutex);
    auto it = m_level_strategies.find(level);
    if (it != m_level_strategies.end()) {
        return it->second;
    }
    return m_global_strategy;
}

void DegradeManager::setGlobalStrategy(const DegradeConfig& cfg) {
    MutexType::Lock lock(m_mutex);
    m_global_strategy = cfg;
}

bool DegradeManager::shouldDegrade(LogLevel::Level level, double queue_usage) {
    if (m_degrade_disabled.load(std::memory_order_acquire)) {
        return false;  // 降级已禁用
    }

    if (!m_watermark_cfg.enabled) {
        return false;  // 水位线降级未启用
    }

    // 检查是否达到降级水位
    if (queue_usage < m_watermark_cfg.degrade_watermark) {
        return false;
    }

    DegradeConfig cfg = getLevelStrategy(level);

    // 如果策略是NONE，不降级
    if (cfg.strategy == DegradeStrategy::NONE) {
        return false;
    }

    // 如果是关键级别且配置了保留
    if (cfg.keep_critical && level >= LogLevel::ERROR) {
        return false;
    }

    // 如果达到严重水位，强制降级
    if (queue_usage >= m_watermark_cfg.critical_watermark) {
        m_total_degraded.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // 正常降级逻辑
    bool result = false;
    switch (cfg.strategy) {
        case DegradeStrategy::DROP:
            result = true;
            break;
        case DegradeStrategy::SAMPLE:
        case DegradeStrategy::RATE_LIMIT:
            // 采样/限流由applyDegrade处理
            result = false;
            break;
        case DegradeStrategy::FALLBACK:
        case DegradeStrategy::REORDER:
            result = false;
            break;
        default:
            break;
    }

    if (result) {
        m_total_degraded.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

bool DegradeManager::applyDegrade(LogLevel::Level level, double queue_usage,
                                    DegradeStrategy& strategy, int& sample_rate) {
    if (m_degrade_disabled.load(std::memory_order_acquire)) {
        strategy = DegradeStrategy::NONE;
        return false;
    }

    DegradeConfig cfg = getLevelStrategy(level);
    strategy = cfg.strategy;

    if (strategy == DegradeStrategy::NONE) {
        return false;  // 不降级
    }

    // 关键级别保护
    if (cfg.keep_critical && level >= LogLevel::ERROR) {
        strategy = DegradeStrategy::NONE;
        return false;
    }

    // 检查水位
    if (queue_usage < m_watermark_cfg.degrade_watermark) {
        strategy = DegradeStrategy::NONE;
        return false;
    }

    bool should_drop = false;

    switch (strategy) {
        case DegradeStrategy::DROP:
            should_drop = true;
            break;

        case DegradeStrategy::SAMPLE:
            sample_rate = cfg.sample_rate;
            // 随机采样: 使用计数器模拟
            {
                static thread_local uint64_t counter = 0;
                ++counter;
                if ((counter % 100) >= static_cast<uint64_t>(sample_rate)) {
                    should_drop = true;
                }
            }
            break;

        case DegradeStrategy::RATE_LIMIT:
            // 令牌桶限速
            {
                uint64_t current = m_rate_limit_tokens.load(std::memory_order_acquire);
                if (current > 0) {
                    m_rate_limit_tokens.fetch_sub(1, std::memory_order_release);
                    should_drop = false;
                } else {
                    should_drop = true;
                }
            }
            break;

        case DegradeStrategy::FALLBACK:
            // 切换到备用Appender - 由上层处理
            should_drop = false;
            break;

        case DegradeStrategy::REORDER:
            // 低级别丢弃
            if (level < cfg.min_keep_level) {
                should_drop = true;
            }
            break;

        default:
            break;
    }

    if (should_drop) {
        m_total_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    m_total_degraded.fetch_add(1, std::memory_order_relaxed);

    return should_drop;
}

void DegradeManager::resetStats() {
    m_total_degraded.store(0);
    m_total_dropped.store(0);
}

} // namespace zero

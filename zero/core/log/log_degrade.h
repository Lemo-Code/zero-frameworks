/**
 * @file log_degrade.h
 * @brief 日志降级控制
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_DEGRADE_H__
#define __ZERO_LOG_DEGRADE_H__

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>
#include "log_level.h"
#include "log_appender.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {

/**
 * @brief 降级策略类型
 */
enum class DegradeStrategy {
    NONE         = 0,  /// 不降级 - 所有日志必须写入
    DROP         = 1,  /// 丢弃 - 超过阈值后丢弃日志
    SAMPLE       = 2,  /// 采样 - 按比例保留
    FALLBACK     = 3,  /// 回退 - 切换到备用Appender
    RATE_LIMIT   = 4,  /// 限流 - 令牌桶限速
    REORDER      = 5,  /// 重排 - 优先保留高级别日志
};

/**
 * @brief 降级水位线配置
 */
struct WatermarkConfig {
    /// 队列使用率告警水位 (0.0-1.0, 默认0.7即70%)
    double warn_watermark = 0.7;

    /// 队列使用率降级水位 (0.0-1.0, 默认0.85即85%)
    double degrade_watermark = 0.85;

    /// 队列使用率强制丢弃水位 (0.0-1.0, 默认0.95即95%)
    double critical_watermark = 0.95;

    /// 是否启用水位线降级
    bool enabled = false;  // 默认关闭 - 不支持降级
};

/**
 * @brief 降级策略配置
 */
struct DegradeConfig {
    /// 降级策略
    DegradeStrategy strategy = DegradeStrategy::NONE;

    /// 采样率 (1-100, strategy=SAMPLE时有效)
    int sample_rate = 10;

    /// 令牌桶速率 (条/秒, strategy=RATE_LIMIT时有效)
    size_t tokens_per_sec = 10000;

    /// 令牌桶突发容量
    size_t burst_size = 10000;

    /// 保留的最低日志级别 (strategy=REORDER时有效, 低于此级别的丢弃)
    LogLevel::Level min_keep_level = LogLevel::WARN;

    /// 备用Appender (strategy=FALLBACK时有效)
    LogAppender::ptr fallback_appender;

    /// 限流时是否仍然保留ERROR及以上级别
    bool keep_critical = true;
};

/**
 * @brief 降级管理器 - 每个Appender独立配置
 */
class DegradeManager {
public:
    typedef std::shared_ptr<DegradeManager> ptr;
    typedef Spinlock MutexType;

    DegradeManager();

    /**
     * @brief 配置水位线
     */
    void setWatermarkConfig(const WatermarkConfig& cfg);
    const WatermarkConfig& getWatermarkConfig() const { return m_watermark_cfg; }

    /**
     * @brief 配置各级别的降级策略
     * @details 可以为不同日志级别配置不同的降级策略
     */
    void setLevelStrategy(LogLevel::Level level, const DegradeConfig& cfg);
    DegradeConfig getLevelStrategy(LogLevel::Level level) const;

    /**
     * @brief 设置全局降级策略 (覆盖所有级别)
     */
    void setGlobalStrategy(const DegradeConfig& cfg);

    /**
     * @brief 检查是否应该降级
     * @param[in] level 日志级别
     * @param[in] queue_usage 当前队列使用率 (0.0-1.0)
     * @return true=应该降级(拒绝/采样), false=正常处理
     */
    bool shouldDegrade(LogLevel::Level level, double queue_usage);

    /**
     * @brief 获取降级后的实际动作
     * @param[out] action 执行的动作描述
     * @return 是否应该丢弃当前日志
     */
    bool applyDegrade(LogLevel::Level level, double queue_usage,
                      DegradeStrategy& strategy, int& sample_rate);

    /**
     * @brief 重置降级统计
     */
    void resetStats();

    /**
     * @brief 获取降级次数统计
     */
    uint64_t getTotalDegradedCount() const { return m_total_degraded.load(std::memory_order_acquire); }
    uint64_t getTotalDroppedCount() const { return m_total_dropped.load(std::memory_order_acquire); }

    /**
     * @brief 完全禁用降级 (所有日志必须写入)
     */
    void disableAll() { m_degrade_disabled.store(true, std::memory_order_release); }

    /**
     * @brief 启用降级
     */
    void enableAll() { m_degrade_disabled.store(false, std::memory_order_release); }

    /**
     * @brief 降级是否被禁用
     */
    bool isDisabled() const { return m_degrade_disabled.load(std::memory_order_acquire); }

private:
    WatermarkConfig m_watermark_cfg;
    std::map<LogLevel::Level, DegradeConfig> m_level_strategies;
    DegradeConfig m_global_strategy;
    std::atomic<uint64_t> m_total_degraded{0};
    std::atomic<uint64_t> m_total_dropped{0};
    std::atomic<uint64_t> m_rate_limit_tokens{0};
    std::atomic<bool> m_degrade_disabled{true};  // 默认禁用降级
    mutable Spinlock m_mutex;
};

} // namespace zero

#endif // __ZERO_LOG_DEGRADE_H__

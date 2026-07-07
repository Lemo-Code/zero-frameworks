/**
 * @file async_log.h
 * @brief 异步日志系统 - 高性能单线程落盘
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_ASYNC_LOG_H__
#define __ZERO_ASYNC_LOG_H__

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <functional>
#include "log_buffer.h"
#include "log_appender.h"
#include "log_event.h"
#include "log_formatter.h"
#include "zero/core/base/noncopyable.h"

namespace zero {

// 前置声明
class AsyncLogWriter;

/**
 * @brief 异步日志配置参数 (所有参数均可通过配置文件设置)
 */
struct AsyncLogConfig {
    /// 环形缓冲区大小 (字节, 默认64MB)
    size_t queue_size = 64 * 1024 * 1024;

    /// 批量写入大小 (每次write调用的最大消息数, 默认256)
    size_t batch_size = 256;

    /// 刷新间隔 (毫秒, 消费者线程等待新数据的超时时间, 默认5ms)
    uint32_t flush_interval_ms = 5;

    /// 最大刷新间隔 (毫秒, 即使未达到batch_size也强制刷新, 默认100ms)
    uint32_t max_flush_interval_ms = 100;

    /// 关闭超时 (毫秒, 等待所有消息落盘的最大时间, 默认3000ms)
    uint32_t shutdown_timeout_ms = 3000;

    /// 环形缓冲区满时的策略
    AsyncOverflowPolicy overflow_policy = AsyncOverflowPolicy::BLOCK;

    /// 是否启用批量合并写入 (减少系统调用)
    bool enable_batching = true;

    /// Writer线程CPU亲和性 (-1=不绑定)
    int writer_cpu_affinity = -1;

    /// Writer线程优先级 (nice值, 0=默认)
    int writer_nice = 0;

    /// Writer线程名称
    std::string writer_thread_name = "async_log_writer";

    /// 是否在每条消息后自动flush (仅调试用，严重影响性能)
    bool flush_on_every_write = false;
};

/**
 * @brief 异步日志Writer - 单线程消费者
 * @details 负责从环形缓冲区批量读取日志并写入目标Appender
 */
class AsyncLogWriter : Noncopyable {
public:
    typedef std::shared_ptr<AsyncLogWriter> ptr;

    /**
     * @brief 构造函数
     * @param[in] appender 目标Appender (不支持异步的Appender在此处包装)
     * @param[in] config 异步配置参数
     */
    AsyncLogWriter(LogAppender::ptr appender, const AsyncLogConfig& config = AsyncLogConfig());

    /**
     * @brief 析构函数 - 等待所有日志落盘后关闭
     */
    ~AsyncLogWriter();

    /**
     * @brief 启动Writer线程
     */
    void start();

    /**
     * @brief 停止Writer线程 (阻塞等待直到所有日志落盘或超时)
     */
    void stop();

    /**
     * @brief 写入日志到环形缓冲区 (生产者调用)
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] event 日志事件
     */
    void append(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 批量写入日志到环形缓冲区
     * @param[in] data 预格式化的日志数据
     * @param[in] len 数据长度
     * @return true=写入成功, false=队列满且策略为丢弃
     */
    bool appendRaw(const char* data, size_t len);

    /**
     * @brief 获取统计信息
     */
    size_t getQueueSize() const { return m_buffer->size(); }
    uint64_t getWrittenCount() const { return m_written_count.load(std::memory_order_acquire); }
    uint64_t getDroppedCount() const { return m_buffer->getDropCount(); }
    uint64_t getBlockedCount() const { return m_buffer->getBlockCount(); }
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    /**
     * @brief 获取配置
     */
    const AsyncLogConfig& getConfig() const { return m_config; }
    void updateConfig(const AsyncLogConfig& config);

private:
    /**
     * @brief Writer线程主循环
     */
    void writerLoop();

    /**
     * @brief 格式化单条日志
     */
    std::string formatLog(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /// 环形缓冲区
    std::unique_ptr<MpscLogRingBuffer> m_buffer;
    /// 目标Appender
    LogAppender::ptr m_appender;
    /// 配置参数
    AsyncLogConfig m_config;
    /// LogEntry构建器 (线程局部)
    // 注意：这里不能是thread_local，因为append可能在不同线程调用
    // 使用栈上分配

    /// Writer线程
    std::unique_ptr<std::thread> m_writer_thread;

    /// 运行标志
    std::atomic<bool> m_running;
    /// 已写入计数
    std::atomic<uint64_t> m_written_count;
    /// 上次强制刷新时间
    std::chrono::steady_clock::time_point m_last_flush;
};

/**
 * @brief 异步Appender包装器
 * @details 将任何同步Appender包装为异步模式
 *          内部使用AsyncLogWriter实现异步写入
 *
 *          使用示例:
 *          @code
 *          auto file_appender = std::make_shared<FileLogAppender>("/var/log/app.log");
 *          auto async_wrapper = std::make_shared<AsyncAppenderWrapper>(file_appender, config);
 *          logger->addAppender(async_wrapper);
 *          @endcode
 */
class AsyncAppenderWrapper : public LogAppender {
public:
    typedef std::shared_ptr<AsyncAppenderWrapper> ptr;

    /**
     * @brief 构造函数
     * @param[in] appender 被包装的Appender
     * @param[in] config 异步配置参数
     */
    AsyncAppenderWrapper(LogAppender::ptr appender,
                          const AsyncLogConfig& config = AsyncLogConfig());

    /**
     * @brief 析构函数 - 等待所有日志落盘
     */
    ~AsyncAppenderWrapper();

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "AsyncAppenderWrapper"; }
    void flush() override;
    void close() override;

    /**
     * @brief 获取内部Writer
     */
    AsyncLogWriter::ptr getWriter() const { return m_writer; }

private:
    AsyncLogWriter::ptr m_writer;
    LogAppender::ptr m_inner_appender;
};

/**
 * @brief 异步Appender工厂
 * @details 便捷函数，为任何Appender创建异步版本
 */
namespace AsyncAppenderFactory {
    /**
     * @brief 创建异步版本
     * @param[in] appender 源Appender
     * @param[in] config 异步配置 (为空则使用默认配置)
     */
    LogAppender::ptr CreateAsync(LogAppender::ptr appender,
                                  const AsyncLogConfig& config = AsyncLogConfig());
}

} // namespace zero

#endif // __ZERO_ASYNC_LOG_H__

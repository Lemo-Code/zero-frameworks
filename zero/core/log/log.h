/**
 * @file log.h
 * @brief 日志系统主头文件 - 聚合所有子模块
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_H__
#define __ZERO_LOG_H__

#include <string>
#include <stdint.h>
#include <memory>
#include <list>
#include <sstream>
#include <fstream>
#include <vector>
#include <stdarg.h>
#include <map>
#include <functional>

#include "zero/core/util/util.h"
#include "zero/core/base/singleton.h"
#include "zero/core/concurrency/thread.h"
#include "zero/core/concurrency/mutex.h"

// 子模块头文件
#include "log_level.h"
#include "log_event.h"
#include "log_formatter.h"
#include "log_appender.h"
#include "log_filter.h"
#include "log_context.h"
#include "log_color.h"
#include "log_buffer.h"
#include "async_log.h"
#include "log_stats.h"
#include "log_degrade.h"
#include "log_config.h"

// ======================== 向后兼容的日志宏 ========================

/**
 * @brief 使用流式方式将日志级别level的日志写入到logger
 */
#define ZERO_LOG_LEVEL(logger, level) \
    if(logger->getLevel() <= level) \
        zero::LogEventWrap(zero::LogEvent::ptr(new zero::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, zero::GetThreadId(),\
                zero::GetFiberId(), time(0), zero::Thread::GetName()))).getSS()

#define ZERO_LOG_DEBUG(logger) ZERO_LOG_LEVEL(logger, zero::LogLevel::DEBUG)
#define ZERO_LOG_INFO(logger)  ZERO_LOG_LEVEL(logger, zero::LogLevel::INFO)
#define ZERO_LOG_WARN(logger)  ZERO_LOG_LEVEL(logger, zero::LogLevel::WARN)
#define ZERO_LOG_ERROR(logger) ZERO_LOG_LEVEL(logger, zero::LogLevel::ERROR)
#define ZERO_LOG_FATAL(logger) ZERO_LOG_LEVEL(logger, zero::LogLevel::FATAL)

// ======================== 扩展级别的日志宏 ========================

#define ZERO_LOG_TRACE(logger)    ZERO_LOG_LEVEL(logger, zero::LogLevel::TRACE)
#define ZERO_LOG_NOTICE(logger)   ZERO_LOG_LEVEL(logger, zero::LogLevel::NOTICE)
#define ZERO_LOG_CRITICAL(logger) ZERO_LOG_LEVEL(logger, zero::LogLevel::CRITICAL)
#define ZERO_LOG_ALERT(logger)    ZERO_LOG_LEVEL(logger, zero::LogLevel::ALERT)
#define ZERO_LOG_EMERGENCY(logger) ZERO_LOG_LEVEL(logger, zero::LogLevel::EMERGENCY)

// ======================== 格式化日志宏 (printf风格) ========================

#define ZERO_LOG_FMT_LEVEL(logger, level, fmt, ...) \
    if(logger->getLevel() <= level) \
        zero::LogEventWrap(zero::LogEvent::ptr(new zero::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, zero::GetThreadId(),\
                zero::GetFiberId(), time(0), zero::Thread::GetName()))).getEvent()->format(fmt, __VA_ARGS__)

#define ZERO_LOG_FMT_DEBUG(logger, fmt, ...)    ZERO_LOG_FMT_LEVEL(logger, zero::LogLevel::DEBUG, fmt, __VA_ARGS__)
#define ZERO_LOG_FMT_INFO(logger, fmt, ...)     ZERO_LOG_FMT_LEVEL(logger, zero::LogLevel::INFO, fmt, __VA_ARGS__)
#define ZERO_LOG_FMT_WARN(logger, fmt, ...)     ZERO_LOG_FMT_LEVEL(logger, zero::LogLevel::WARN, fmt, __VA_ARGS__)
#define ZERO_LOG_FMT_ERROR(logger, fmt, ...)    ZERO_LOG_FMT_LEVEL(logger, zero::LogLevel::ERROR, fmt, __VA_ARGS__)
#define ZERO_LOG_FMT_FATAL(logger, fmt, ...)    ZERO_LOG_FMT_LEVEL(logger, zero::LogLevel::FATAL, fmt, __VA_ARGS__)

// ======================== 增强宏 - 带函数名 ========================

#define ZERO_LOG_LEVEL_FUNC(logger, level) \
    if(logger->getLevel() <= level) \
        zero::LogEventWrap(zero::LogEvent::ptr(new zero::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, zero::GetThreadId(),\
                zero::GetFiberId(), time(0), zero::Thread::GetName()))).getSS()

// ======================== 条件日志宏 ========================

#define ZERO_LOG_IF(condition, logger, level) \
    if((condition) && logger->getLevel() <= level) \
        zero::LogEventWrap(zero::LogEvent::ptr(new zero::LogEvent(logger, level, \
                        __FILE__, __LINE__, 0, zero::GetThreadId(),\
                zero::GetFiberId(), time(0), zero::Thread::GetName()))).getSS()

// ======================== 高频日志宏 (每N次输出一次) ========================

#define ZERO_LOG_EVERY_N(n, logger, level) \
    for(static uint64_t _zero_cnt = 0; ; ) \
        if(++_zero_cnt % (n) != 0) break; \
        else if(logger->getLevel() <= level) \
            zero::LogEventWrap(zero::LogEvent::ptr(new zero::LogEvent(logger, level, \
                            __FILE__, __LINE__, 0, zero::GetThreadId(),\
                    zero::GetFiberId(), time(0), zero::Thread::GetName()))).getSS()

// ======================== Logger获取宏 ========================

/// 获取根日志器
#define ZERO_LOG_ROOT() zero::LoggerMgr::GetInstance()->getRoot()

/// 获取命名日志器
#define ZERO_LOG_NAME(name) zero::LoggerMgr::GetInstance()->getLogger(name)

// ======================== 类声明 ========================

namespace zero {

// 前置声明
class Logger;
class LoggerManager;

/**
 * @brief 日志器 - 管理一组Appender和过滤器
 */
class Logger : public std::enable_shared_from_this<Logger> {
friend class LoggerManager;
public:
    typedef std::shared_ptr<Logger> ptr;
    typedef Spinlock MutexType;

    /**
     * @brief 构造函数
     * @param[in] name 日志器名称
     */
    Logger(const std::string& name = "root");

    /**
     * @brief 析构函数
     */
    ~Logger();

    // ===== 日志写入接口 =====
    void log(LogLevel::Level level, LogEvent::ptr event);
    void trace(LogEvent::ptr event);
    void debug(LogEvent::ptr event);
    void info(LogEvent::ptr event);
    void notice(LogEvent::ptr event);
    void warn(LogEvent::ptr event);
    void error(LogEvent::ptr event);
    void critical(LogEvent::ptr event);
    void alert(LogEvent::ptr event);
    void fatal(LogEvent::ptr event);
    void emergency(LogEvent::ptr event);

    // ===== Appender管理 =====
    void addAppender(LogAppender::ptr appender);
    void delAppender(LogAppender::ptr appender);
    void clearAppenders();
    const std::list<LogAppender::ptr>& getAppenders() const { return m_appenders; }

    // ===== 过滤器管理 =====
    void addFilter(LogFilter::ptr filter);
    void removeFilter(LogFilter::ptr filter);
    void clearFilters();
    bool applyFilters(LogLevel::Level level, LogEvent::ptr event);

    // ===== 属性管理 =====
    LogLevel::Level getLevel() const { return m_level; }
    void setLevel(LogLevel::Level val) { m_level = val; }
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    // ===== 格式化器 =====
    void setFormatter(LogFormatter::ptr val);
    void setFormatter(const std::string& val);
    LogFormatter::ptr getFormatter();

    // ===== 配置导出 =====
    std::string toYamlString();

    // ===== 统计信息 =====
    uint64_t getLogCount() const { return m_log_count.load(std::memory_order_acquire); }

private:
    /// 日志名称
    std::string m_name;
    /// 日志级别
    LogLevel::Level m_level;
    /// Mutex
    mutable MutexType m_mutex;
    /// 日志目标集合
    std::list<LogAppender::ptr> m_appenders;
    /// 日志格式器
    LogFormatter::ptr m_formatter;
    /// 主日志器
    Logger::ptr m_root;
    /// 过滤器链
    std::vector<LogFilter::ptr> m_filters;
    /// 统计计数
    std::atomic<uint64_t> m_log_count{0};
};

/**
 * @brief 日志器管理类
 */
class LoggerManager {
public:
    typedef Spinlock MutexType;

    LoggerManager();

    /**
     * @brief 获取日志器 (不存在则创建)
     */
    Logger::ptr getLogger(const std::string& name);

    /**
     * @brief 获取日志器 (不存在返回nullptr)
     */
    Logger::ptr lookupLogger(const std::string& name) const;

    /**
     * @brief 删除日志器
     */
    void removeLogger(const std::string& name);

    /**
     * @brief 返回主日志器
     */
    Logger::ptr getRoot() const { return m_root; }

    /**
     * @brief 初始化
     */
    void init();

    /**
     * @brief 获取所有日志器
     */
    std::map<std::string, Logger::ptr> getAllLoggers() const;

    /**
     * @brief 将所有的日志器配置转成YAML String
     */
    std::string toYamlString();

    /**
     * @brief 遍历所有日志器
     */
    void visit(std::function<void(Logger::ptr)> cb);

private:
    mutable MutexType m_mutex;
    std::map<std::string, Logger::ptr> m_loggers;
    Logger::ptr m_root;
};

/// 日志器管理类单例
typedef zero::Singleton<LoggerManager> LoggerMgr;

} // namespace zero

#endif // __ZERO_LOG_H__

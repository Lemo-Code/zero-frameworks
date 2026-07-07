/**
 * @file log_event.h
 * @brief 扩展的日志事件定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_EVENT_H__
#define __ZERO_LOG_EVENT_H__

#include <string>
#include <stdint.h>
#include <memory>
#include <sstream>
#include <vector>
#include <map>
#include <functional>
#include "log_level.h"

namespace zero {

class Logger;
class LogEvent;

/**
 * @brief 日志事件包装器 (RAII)
 * @details 在析构时自动触发Logger::log，支持流式写入
 */
class LogEventWrap {
public:
    typedef std::shared_ptr<LogEvent> EventPtr;

    /**
     * @brief 构造函数
     * @param[in] e 日志事件
     */
    LogEventWrap(EventPtr e);

    /**
     * @brief 析构函数 - 触发日志输出
     */
    ~LogEventWrap();

    /**
     * @brief 获取日志事件
     */
    EventPtr getEvent() const { return m_event; }

    /**
     * @brief 获取日志内容流 (用于流式写入)
     */
    std::stringstream& getSS();

private:
    EventPtr m_event;
};

/**
 * @brief 日志事件 - 携带完整的日志上下文
 */
class LogEvent {
public:
    typedef std::shared_ptr<LogEvent> ptr;
    typedef std::weak_ptr<LogEvent> weak_ptr;

    /**
     * @brief 键值对标签
     */
    typedef std::map<std::string, std::string> TagMap;

    /**
     * @brief 构造函数 (兼容原有接口)
     * @param[in] logger 日志器
     * @param[in] level 日志级别
     * @param[in] file 文件名
     * @param[in] line 文件行号
     * @param[in] elapse 程序启动依赖的耗时(毫秒)
     * @param[in] thread_id 线程id
     * @param[in] fiber_id 协程id
     * @param[in] time 日志时间(秒)
     * @param[in] thread_name 线程名称
     */
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level,
             const char* file, int32_t line, uint32_t elapse,
             uint32_t thread_id, uint32_t fiber_id, uint64_t time,
             const std::string& thread_name);

    /**
     * @brief 扩展构造函数 - 包含微秒时间戳和结构化数据
     */
    LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level,
             const char* file, int32_t line, uint32_t elapse,
             uint32_t thread_id, uint32_t fiber_id, uint64_t time,
             uint64_t time_us,
             const std::string& thread_name);

    // ======================== 基础访问器 ========================
    const char* getFile() const { return m_file; }
    int32_t getLine() const { return m_line; }
    uint32_t getElapse() const { return m_elapse; }
    uint32_t getThreadId() const { return m_threadId; }
    uint32_t getFiberId() const { return m_fiberId; }
    uint64_t getTime() const { return m_time; }
    uint64_t getTimeUs() const { return m_timeUs; }
    const std::string& getThreadName() const { return m_threadName; }
    std::string getContent() const { return m_ss.str(); }
    std::stringstream& getSS() { return m_ss; }
    std::shared_ptr<Logger> getLogger() const { return m_logger.lock(); }
    LogLevel::Level getLevel() const { return m_level; }

    // ======================== 扩展访问器 ========================
    uint32_t getProcessId() const { return m_processId; }
    const std::string& getHostname() const { return m_hostname; }
    const std::string& getTraceId() const { return m_traceId; }
    const std::string& getSpanId() const { return m_spanId; }
    const std::string& getFunctionName() const { return m_functionName; }
    const TagMap& getTags() const { return m_tags; }
    const std::map<std::string, std::string>& getMDCContext() const { return m_mdc_context; }
    const std::string& getAppName() const { return m_appName; }

    // ======================== 设置器 ========================
    void setTraceId(const std::string& trace_id) { m_traceId = trace_id; }
    void setSpanId(const std::string& span_id) { m_spanId = span_id; }
    void setFunctionName(const std::string& func) { m_functionName = func; }
    void setAppName(const std::string& name) { m_appName = name; }

    /**
     * @brief 添加标签
     */
    void addTag(const std::string& key, const std::string& value);
    void addTag(const std::string& key, int64_t value);
    void addTag(const std::string& key, double value);

    /**
     * @brief 设置MDC上下文快照
     */
    void setMDCContext(const std::map<std::string, std::string>& ctx) { m_mdc_context = ctx; }

    /**
     * @brief 格式化写入日志内容
     */
    void format(const char* fmt, ...);

    /**
     * @brief 格式化写入日志内容 (va_list版本)
     */
    void format(const char* fmt, va_list al);

    /**
     * @brief 直接写入内容
     */
    void write(const std::string& content) { m_ss << content; }

    /**
     * @brief 以JSON格式输出事件的所有字段
     */
    std::string toJson() const;

    /**
     * @brief 获取函数签名 (需要编译器支持 __PRETTY_FUNCTION__)
     */
    static std::string GetPrettyFunction(const char* pretty_func);

private:
    /// 文件名
    const char* m_file = nullptr;
    /// 行号
    int32_t m_line = 0;
    /// 程序启动到现在的毫秒数
    uint32_t m_elapse = 0;
    /// 线程ID
    uint32_t m_threadId = 0;
    /// 协程ID
    uint32_t m_fiberId = 0;
    /// 时间戳(秒)
    uint64_t m_time = 0;
    /// 时间戳(微秒部分)
    uint64_t m_timeUs = 0;
    /// 线程名称
    std::string m_threadName;
    /// 日志内容流
    std::stringstream m_ss;
    /// 日志器 (weak_ptr 避免循环引用)
    std::weak_ptr<Logger> m_logger;
    /// 日志等级
    LogLevel::Level m_level = LogLevel::UNKNOW;

    // ======================== 扩展字段 ========================
    /// 进程ID
    uint32_t m_processId = 0;
    /// 主机名
    std::string m_hostname;
    /// 分布式追踪 Trace ID
    std::string m_traceId;
    /// 分布式追踪 Span ID
    std::string m_spanId;
    /// 函数名
    std::string m_functionName;
    /// 应用名
    std::string m_appName;
    /// 用户自定义标签
    TagMap m_tags;
    /// MDC上下文快照
    std::map<std::string, std::string> m_mdc_context;
};

} // namespace zero

#endif // __ZERO_LOG_EVENT_H__

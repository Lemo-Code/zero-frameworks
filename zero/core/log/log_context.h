/**
 * @file log_context.h
 * @brief 日志诊断上下文 (MDC / NDC)
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_CONTEXT_H__
#define __ZERO_LOG_CONTEXT_H__

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <random>
#include <sstream>
#include "zero/core/base/noncopyable.h"

namespace zero {

/**
 * @brief MDC (Mapped Diagnostic Context) - 线程局部映射
 * @details 使用thread_local实现，零锁开销
 */
class MDC {
public:
    /**
     * @brief 设置键值对
     */
    static void put(const std::string& key, const std::string& value);

    /**
     * @brief 获取值
     */
    static std::string get(const std::string& key);

    /**
     * @brief 移除键
     */
    static void remove(const std::string& key);

    /**
     * @brief 清空当前线程的所有MDC
     */
    static void clear();

    /**
     * @brief 获取当前线程MDC快照
     */
    static std::map<std::string, std::string> getCopy();

    /**
     * @brief 获取MDC格式化字符串 (key1=val1 key2=val2)
     */
    static std::string getFormatted();

    /**
     * @brief 检查是否包含键
     */
    static bool contains(const std::string& key);

private:
    static std::map<std::string, std::string>& getThreadLocalMap();
};

/**
 * @brief NDC (Nested Diagnostic Context) - 线程局部栈
 * @details 使用thread_local实现，零锁开销
 */
class NDC {
public:
    /**
     * @brief 压入上下文消息
     */
    static void push(const std::string& message);

    /**
     * @brief 弹出上下文消息
     */
    static std::string pop();

    /**
     * @brief 查看栈顶消息 (不弹出)
     */
    static std::string peek();

    /**
     * @brief 获取当前深度
     */
    static size_t depth();

    /**
     * @brief 清空栈
     */
    static void clear();

    /**
     * @brief 获取完整栈信息 (用分隔符连接)
     */
    static std::string getFormatted(const std::string& separator = " > ");

    /**
     * @brief 检查是否为空
     */
    static bool empty();

private:
    static std::vector<std::string>& getThreadLocalStack();
};

/**
 * @brief RAII NDC守卫 - 自动压入/弹出
 */
class NDCScope {
public:
    explicit NDCScope(const std::string& message);
    ~NDCScope();

    NDCScope(const NDCScope&) = delete;
    NDCScope& operator=(const NDCScope&) = delete;

private:
    bool m_active = true;
};

/**
 * @brief RAII MDC守卫 - 自动设置/移除
 */
class MDCScope {
public:
    MDCScope(const std::string& key, const std::string& value);
    ~MDCScope();

    MDCScope(const MDCScope&) = delete;
    MDCScope& operator=(const MDCScope&) = delete;

private:
    std::string m_key;
};

/**
 * @brief 分布式追踪上下文
 * @details 管理Trace ID和Span ID的生成与传播
 */
class TraceContext {
public:
    typedef std::shared_ptr<TraceContext> ptr;

    TraceContext();
    explicit TraceContext(const std::string& trace_id);

    /**
     * @brief 获取当前线程的Trace上下文
     */
    static TraceContext* getCurrent();

    /**
     * @brief 设置当前线程的Trace上下文
     */
    static void setCurrent(TraceContext* ctx);

    /**
     * @brief 创建新的Span ID
     */
    std::string newSpanId();

    /**
     * @brief 生成新的Trace ID
     */
    static std::string generateTraceId();

    /**
     * @brief 生成新的Span ID
     */
    static std::string generateSpanId();

    const std::string& getTraceId() const { return m_traceId; }
    const std::string& getParentSpanId() const { return m_parentSpanId; }
    const std::string& getCurrentSpanId() const { return m_currentSpanId; }

    void setTraceId(const std::string& id) { m_traceId = id; }
    void setParentSpanId(const std::string& id) { m_parentSpanId = id; }
    void setCurrentSpanId(const std::string& id) { m_currentSpanId = id; }

    /**
     * @brief 转为W3C traceparent格式
     * @details 格式: 00-{trace-id}-{span-id}-{trace-flags}
     */
    std::string toTraceParent() const;

private:
    std::string m_traceId;
    std::string m_parentSpanId;
    std::string m_currentSpanId;

    static thread_local TraceContext* t_current;

    static std::string generateHexId(int length);
};

/**
 * @brief RAII Span守卫 - 自动管理Span生命周期
 */
class SpanScope {
public:
    explicit SpanScope(const std::string& span_name);
    ~SpanScope();

    SpanScope(const SpanScope&) = delete;
    SpanScope& operator=(const SpanScope&) = delete;

    const std::string& getSpanId() const { return m_spanId; }

private:
    std::string m_spanId;
    std::string m_previousSpanId;
    TraceContext* m_context;
};

// ======================== MDC便捷宏 ========================

/**
 * @brief 设置MDC键值对
 */
#define ZERO_LOG_MDC(key, value) zero::MDC::put(key, value)

/**
 * @brief 移除MDC键
 */
#define ZERO_LOG_MDC_REMOVE(key) zero::MDC::remove(key)

/**
 * @brief 清空MDC
 */
#define ZERO_LOG_MDC_CLEAR() zero::MDC::clear()

/**
 * @brief NDC压入
 */
#define ZERO_LOG_NDC_PUSH(msg) zero::NDC::push(msg)

/**
 * @brief NDC弹出
 */
#define ZERO_LOG_NDC_POP() zero::NDC::pop()

/**
 * @brief NDC清空
 */
#define ZERO_LOG_NDC_CLEAR() zero::NDC::clear()

/**
 * @brief 创建NDC守卫 (RAII)
 */
#define ZERO_LOG_NDC_SCOPE(msg) zero::NDCScope __ndc_scope__(msg)

/**
 * @brief 创建MDC守卫 (RAII)
 */
#define ZERO_LOG_MDC_SCOPE(key, value) zero::MDCScope __mdc_scope__(key, value)

/**
 * @brief 创建Span守卫 (RAII)
 */
#define ZERO_LOG_SPAN_SCOPE(name) zero::SpanScope __span_scope__(name)

} // namespace zero

#endif // __ZERO_LOG_CONTEXT_H__

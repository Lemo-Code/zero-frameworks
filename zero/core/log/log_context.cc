/**
 * @file log_context.cc
 * @brief 日志诊断上下文实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_context.h"
#include <iomanip>
#include <chrono>

namespace zero {

// ======================== MDC ========================

std::map<std::string, std::string>& MDC::getThreadLocalMap() {
    thread_local std::map<std::string, std::string> t_map;
    return t_map;
}

void MDC::put(const std::string& key, const std::string& value) {
    getThreadLocalMap()[key] = value;
}

std::string MDC::get(const std::string& key) {
    auto& m = getThreadLocalMap();
    auto it = m.find(key);
    return (it != m.end()) ? it->second : "";
}

void MDC::remove(const std::string& key) {
    getThreadLocalMap().erase(key);
}

void MDC::clear() {
    getThreadLocalMap().clear();
}

std::map<std::string, std::string> MDC::getCopy() {
    return getThreadLocalMap();
}

std::string MDC::getFormatted() {
    auto& m = getThreadLocalMap();
    if (m.empty()) return "";
    std::stringstream ss;
    bool first = true;
    for (auto& kv : m) {
        if (!first) ss << " ";
        ss << kv.first << "=" << kv.second;
        first = false;
    }
    return ss.str();
}

bool MDC::contains(const std::string& key) {
    return getThreadLocalMap().find(key) != getThreadLocalMap().end();
}

// ======================== NDC ========================

std::vector<std::string>& NDC::getThreadLocalStack() {
    thread_local std::vector<std::string> t_stack;
    return t_stack;
}

void NDC::push(const std::string& message) {
    getThreadLocalStack().push_back(message);
}

std::string NDC::pop() {
    auto& stack = getThreadLocalStack();
    if (stack.empty()) return "";
    std::string top = stack.back();
    stack.pop_back();
    return top;
}

std::string NDC::peek() {
    auto& stack = getThreadLocalStack();
    return stack.empty() ? "" : stack.back();
}

size_t NDC::depth() {
    return getThreadLocalStack().size();
}

void NDC::clear() {
    getThreadLocalStack().clear();
}

std::string NDC::getFormatted(const std::string& separator) {
    auto& stack = getThreadLocalStack();
    std::stringstream ss;
    for (size_t i = 0; i < stack.size(); ++i) {
        if (i > 0) ss << separator;
        ss << stack[i];
    }
    return ss.str();
}

bool NDC::empty() {
    return getThreadLocalStack().empty();
}

// ======================== NDCScope ========================

NDCScope::NDCScope(const std::string& message) {
    NDC::push(message);
}

NDCScope::~NDCScope() {
    if (m_active) {
        NDC::pop();
    }
}

// ======================== MDCScope ========================

MDCScope::MDCScope(const std::string& key, const std::string& value)
    : m_key(key) {
    MDC::put(key, value);
}

MDCScope::~MDCScope() {
    MDC::remove(m_key);
}

// ======================== TraceContext ========================

thread_local TraceContext* TraceContext::t_current = nullptr;

TraceContext::TraceContext() {
    m_traceId = generateTraceId();
    m_currentSpanId = m_traceId;  // root span
}

TraceContext::TraceContext(const std::string& trace_id)
    : m_traceId(trace_id) {
    m_currentSpanId = generateSpanId();
}

TraceContext* TraceContext::getCurrent() {
    return t_current;
}

void TraceContext::setCurrent(TraceContext* ctx) {
    t_current = ctx;
}

std::string TraceContext::newSpanId() {
    m_parentSpanId = m_currentSpanId;
    m_currentSpanId = generateSpanId();
    return m_currentSpanId;
}

std::string TraceContext::generateTraceId() {
    return generateHexId(32);
}

std::string TraceContext::generateSpanId() {
    return generateHexId(16);
}

std::string TraceContext::generateHexId(int length) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dis;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    // 使用时间戳 + 随机数
    auto now = std::chrono::system_clock::now();
    auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    ss << std::setw(16) << nanos;
    if (length > 16) {
        ss << std::setw(length - 16) << dis(gen);
    }

    std::string result = ss.str();
    if (result.size() > static_cast<size_t>(length)) {
        result = result.substr(0, length);
    }
    return result;
}

std::string TraceContext::toTraceParent() const {
    // 00-{trace-id}-{span-id}-01
    std::stringstream ss;
    ss << "00-" << m_traceId << "-" << m_currentSpanId << "-01";
    return ss.str();
}

// ======================== SpanScope ========================

SpanScope::SpanScope(const std::string& span_name) {
    m_context = TraceContext::getCurrent();
    if (!m_context) {
        m_context = new TraceContext();
        TraceContext::setCurrent(m_context);
    }
    m_previousSpanId = m_context->getCurrentSpanId();
    m_spanId = m_context->newSpanId();

    MDC::put("trace_id", m_context->getTraceId());
    MDC::put("span_id", m_spanId);
}

SpanScope::~SpanScope() {
    if (m_context) {
        m_context->setCurrentSpanId(m_previousSpanId);
        MDC::put("span_id", m_previousSpanId);
    }
}

} // namespace zero

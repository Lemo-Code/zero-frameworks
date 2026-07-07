/**
 * @file log_event.cc
 * @brief 日志事件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_event.h"
#include "log.h"
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <sstream>
#include <cstdio>
#include <stdarg.h>

namespace zero {

// ======================== LogEventWrap ========================

LogEventWrap::LogEventWrap(EventPtr e)
    : m_event(e) {
}

LogEventWrap::~LogEventWrap() {
    auto logger = m_event->getLogger();
    if (logger) {
        logger->log(m_event->getLevel(), m_event);
    }
}

std::stringstream& LogEventWrap::getSS() {
    return m_event->getSS();
}

// ======================== LogEvent ========================

LogEvent::LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level,
                   const char* file, int32_t line, uint32_t elapse,
                   uint32_t thread_id, uint32_t fiber_id, uint64_t time,
                   const std::string& thread_name)
    : m_file(file)
    , m_line(line)
    , m_elapse(elapse)
    , m_threadId(thread_id)
    , m_fiberId(fiber_id)
    , m_time(time)
    , m_timeUs(0)
    , m_threadName(thread_name)
    , m_logger(logger)
    , m_level(level) {
    // 获取微秒时间戳
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) == 0) {
        m_timeUs = tv.tv_usec;
    }
    // 获取进程ID
    m_processId = getpid();
    // 获取主机名
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        m_hostname = hostname;
    }
}

LogEvent::LogEvent(std::shared_ptr<Logger> logger, LogLevel::Level level,
                   const char* file, int32_t line, uint32_t elapse,
                   uint32_t thread_id, uint32_t fiber_id, uint64_t time,
                   uint64_t time_us,
                   const std::string& thread_name)
    : m_file(file)
    , m_line(line)
    , m_elapse(elapse)
    , m_threadId(thread_id)
    , m_fiberId(fiber_id)
    , m_time(time)
    , m_timeUs(time_us)
    , m_threadName(thread_name)
    , m_logger(logger)
    , m_level(level) {
    m_processId = getpid();
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        m_hostname = hostname;
    }
}

void LogEvent::addTag(const std::string& key, const std::string& value) {
    m_tags[key] = value;
}

void LogEvent::addTag(const std::string& key, int64_t value) {
    m_tags[key] = std::to_string(value);
}

void LogEvent::addTag(const std::string& key, double value) {
    m_tags[key] = std::to_string(value);
}

void LogEvent::format(const char* fmt, ...) {
    va_list al;
    va_start(al, fmt);
    format(fmt, al);
    va_end(al);
}

void LogEvent::format(const char* fmt, va_list al) {
    char* buf = nullptr;
    int len = vasprintf(&buf, fmt, al);
    if (len != -1) {
        m_ss << std::string(buf, len);
        free(buf);
    }
}

std::string LogEvent::toJson() const {
    std::stringstream js;
    js << "{";
    js << "\"timestamp\":" << m_time;
    js << ",\"timestamp_us\":" << m_timeUs;
    js << ",\"level\":\"" << LogLevel::ToString(m_level) << "\"";
    js << ",\"level_num\":" << (int)m_level;
    js << ",\"logger\":\"" << (m_logger.lock() ? m_logger.lock()->getName() : "unknown") << "\"";
    js << ",\"thread_id\":" << m_threadId;
    js << ",\"thread_name\":\"" << m_threadName << "\"";
    js << ",\"fiber_id\":" << m_fiberId;
    js << ",\"process_id\":" << m_processId;
    js << ",\"hostname\":\"" << m_hostname << "\"";
    js << ",\"file\":\"" << (m_file ? m_file : "") << "\"";
    js << ",\"line\":" << m_line;
    if (!m_traceId.empty()) {
        js << ",\"trace_id\":\"" << m_traceId << "\"";
    }
    if (!m_spanId.empty()) {
        js << ",\"span_id\":\"" << m_spanId << "\"";
    }
    if (!m_functionName.empty()) {
        js << ",\"function\":\"" << m_functionName << "\"";
    }
    // 转义JSON字符串的辅助lambda
    auto jsonEscape = [](const std::string& s) -> std::string {
        std::string result;
        result.reserve(s.size() * 2);
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c;
            }
        }
        return result;
    };

    js << ",\"message\":\"" << jsonEscape(m_ss.str()) << "\"";

    // Tags
    if (!m_tags.empty()) {
        js << ",\"tags\":{";
        bool first = true;
        for (auto& t : m_tags) {
            if (!first) js << ",";
            js << "\"" << jsonEscape(t.first) << "\":\"" << jsonEscape(t.second) << "\"";
            first = false;
        }
        js << "}";
    }

    js << "}";
    return js.str();
}

std::string LogEvent::GetPrettyFunction(const char* pretty_func) {
    if (!pretty_func) return "";
    std::string s(pretty_func);
    // 截取函数签名部分
    size_t paren = s.find('(');
    if (paren != std::string::npos) {
        // 找到函数名起始位置
        size_t space = s.rfind(' ', paren);
        if (space != std::string::npos) {
            return s.substr(space + 1, paren - space - 1);
        }
        return s.substr(0, paren);
    }
    return s;
}

} // namespace zero

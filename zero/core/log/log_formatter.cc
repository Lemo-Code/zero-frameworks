/**
 * @file log_formatter.cc
 * @brief 日志格式化器实现 - 30+格式模式
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_formatter.h"
#include "log.h"
#include "log_color.h"
#include "log_context.h"
#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <tuple>
#include <functional>

namespace zero {

// ======================== 所有 FormatItem 子类实现 ========================

// ---- 消息体 ----
class MessageFormatItem : public LogFormatter::FormatItem {
public:
    MessageFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getContent();
    }
};

// ---- 日志级别(完整) ----
class LevelFormatItem : public LogFormatter::FormatItem {
public:
    LevelFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) override {
        os << LogLevel::ToString(level);
    }
};

// ---- 日志级别(缩写) ----
class ShortLevelFormatItem : public LogFormatter::FormatItem {
public:
    ShortLevelFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) override {
        os << LogLevel::ToShortString(level);
    }
};

// ---- 级别颜色开始 ----
class ColorStartFormatItem : public LogFormatter::FormatItem {
public:
    ColorStartFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) override {
        os << ColorizeBegin(level);
    }
};

// ---- 颜色结束 ----
class ColorEndFormatItem : public LogFormatter::FormatItem {
public:
    ColorEndFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << ColorizeEnd();
    }
};

// ---- 纯文本模式开始 ----
class PlainTextFormatItem : public LogFormatter::FormatItem {
public:
    PlainTextFormatItem(const std::string& = "") {}
    void format(std::ostream&, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        // 纯文本模式 - 不输出任何颜色信息
    }
};

// ---- 毫秒耗时 ----
class ElapseFormatItem : public LogFormatter::FormatItem {
public:
    ElapseFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getElapse();
    }
};

// ---- 微秒耗时 ----
class ElapseUsFormatItem : public LogFormatter::FormatItem {
public:
    ElapseUsFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        // 从系统启动开始计算微秒数
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t us = static_cast<uint64_t>(ts.tv_sec) * 1000000ULL
                    + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
        os << us;
    }
};

// ---- Logger名称 ----
class NameFormatItem : public LogFormatter::FormatItem {
public:
    NameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level, LogEvent::ptr) override {
        os << (logger ? logger->getName() : "unknown");
    }
};

// ---- 线程ID ----
class ThreadIdFormatItem : public LogFormatter::FormatItem {
public:
    ThreadIdFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getThreadId();
    }
};

// ---- 协程ID ----
class FiberIdFormatItem : public LogFormatter::FormatItem {
public:
    FiberIdFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getFiberId();
    }
};

// ---- 线程名称 ----
class ThreadNameFormatItem : public LogFormatter::FormatItem {
public:
    ThreadNameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getThreadName();
    }
};

// ---- 日期时间 ----
class DateTimeFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
        : m_format(format) {
        if (m_format.empty()) {
            m_format = "%Y-%m-%d %H:%M:%S";
        }
    }
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        struct tm tm;
        time_t time = event->getTime();
        localtime_r(&time, &tm);
        char buf[128];
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);
        os << buf;
    }
private:
    std::string m_format;
};

// ---- 微秒级日期时间 ----
class DateTimeUsFormatItem : public LogFormatter::FormatItem {
public:
    DateTimeUsFormatItem(const std::string& format = "%Y-%m-%d %H:%M:%S")
        : m_format(format) {
        if (m_format.empty()) m_format = "%Y-%m-%d %H:%M:%S";
    }
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        struct tm tm;
        time_t sec = event->getTime();
        localtime_r(&sec, &tm);
        char buf[128];
        strftime(buf, sizeof(buf), m_format.c_str(), &tm);
        os << buf;
        // 追加微秒
        char us_buf[16];
        snprintf(us_buf, sizeof(us_buf), ".%06lu", (unsigned long)event->getTimeUs());
        os << us_buf;
    }
private:
    std::string m_format;
};

// ---- 文件名 ----
class FilenameFormatItem : public LogFormatter::FormatItem {
public:
    FilenameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << (event->getFile() ? event->getFile() : "");
    }
};

// ---- 行号 ----
class LineFormatItem : public LogFormatter::FormatItem {
public:
    LineFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getLine();
    }
};

// ---- 换行 ----
class NewLineFormatItem : public LogFormatter::FormatItem {
public:
    NewLineFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << std::endl;
    }
};

// ---- 制表符 ----
class TabFormatItem : public LogFormatter::FormatItem {
public:
    TabFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << "\t";
    }
};

// ---- 字符串字面量 ----
class StringFormatItem : public LogFormatter::FormatItem {
public:
    StringFormatItem(const std::string& str) : m_string(str) {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << m_string;
    }
private:
    std::string m_string;
};

// ---- 百分号 ----
class PercentFormatItem : public LogFormatter::FormatItem {
public:
    PercentFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << "%";
    }
};

// ---- 进程ID ----
class ProcessIdFormatItem : public LogFormatter::FormatItem {
public:
    ProcessIdFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getProcessId();
    }
};

// ---- 主机名 ----
class HostnameFormatItem : public LogFormatter::FormatItem {
public:
    HostnameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getHostname();
    }
};

// ---- 函数名 ----
class FunctionNameFormatItem : public LogFormatter::FormatItem {
public:
    FunctionNameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getFunctionName();
    }
};

// ---- 应用名 ----
class AppNameFormatItem : public LogFormatter::FormatItem {
public:
    AppNameFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getAppName();
    }
};

// ---- MDC指定键 ----
class MDCFormatItem : public LogFormatter::FormatItem {
public:
    MDCFormatItem(const std::string& key) : m_key(key) {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << MDC::get(m_key);
    }
private:
    std::string m_key;
};

// ---- 完整MDC ----
class FullMDCFormatItem : public LogFormatter::FormatItem {
public:
    FullMDCFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << MDC::getFormatted();
    }
};

// ---- MDC JSON格式 ----
class MDCJsonFormatItem : public LogFormatter::FormatItem {
public:
    MDCJsonFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        auto mdc = MDC::getCopy();
        os << "{";
        bool first = true;
        for (auto& kv : mdc) {
            if (!first) os << ",";
            os << "\"" << kv.first << "\":\"" << kv.second << "\"";
            first = false;
        }
        os << "}";
    }
};

// ---- NDC ----
class NDCFormatItem : public LogFormatter::FormatItem {
public:
    NDCFormatItem(const std::string& sep = " > ") : m_separator(sep) {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << NDC::getFormatted(m_separator);
    }
private:
    std::string m_separator;
};

// ---- NDC深度 ----
class NDCDepthFormatItem : public LogFormatter::FormatItem {
public:
    NDCDepthFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {
        os << NDC::depth();
    }
};

// ---- Trace ID ----
class TraceIdFormatItem : public LogFormatter::FormatItem {
public:
    TraceIdFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getTraceId();
    }
};

// ---- Span ID ----
class SpanIdFormatItem : public LogFormatter::FormatItem {
public:
    SpanIdFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << event->getSpanId();
    }
};

// ---- 用户Tag指定键 ----
class TagFormatItem : public LogFormatter::FormatItem {
public:
    TagFormatItem(const std::string& key) : m_key(key) {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        auto& tags = event->getTags();
        auto it = tags.find(m_key);
        if (it != tags.end()) os << it->second;
    }
private:
    std::string m_key;
};

// ---- 所有用户Tag JSON ----
class AllTagsFormatItem : public LogFormatter::FormatItem {
public:
    AllTagsFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        os << "{";
        bool first = true;
        for (auto& kv : event->getTags()) {
            if (!first) os << ",";
            os << "\"" << kv.first << "\":\"" << kv.second << "\"";
            first = false;
        }
        os << "}";
    }
};

// ---- 日志级别数值 ----
class LevelNumFormatItem : public LogFormatter::FormatItem {
public:
    LevelNumFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) override {
        os << static_cast<int>(level);
    }
};

// ---- 日志级别syslog优先级 ----
class SyslogPriorityFormatItem : public LogFormatter::FormatItem {
public:
    SyslogPriorityFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level level, LogEvent::ptr) override {
        os << LogLevel::ToSyslogPriority(level);
    }
};

// ---- 左对齐/右对齐包装器 ----
class AlignFormatItem : public LogFormatter::FormatItem {
public:
    AlignFormatItem(LogFormatter::FormatItem::ptr inner, int width, bool left_align)
        : m_inner(inner), m_width(width), m_left_align(left_align) {}

    void format(std::ostream& os, std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override {
        std::stringstream ss;
        m_inner->format(ss, logger, level, event);
        std::string s = ss.str();
        if (m_left_align) {
            os << s;
            for (int i = s.size(); i < m_width; ++i) os << ' ';
        } else {
            for (int i = s.size(); i < m_width; ++i) os << ' ';
            os << s;
        }
    }
private:
    LogFormatter::FormatItem::ptr m_inner;
    int m_width;
    bool m_left_align;
};

// ---- MDC上下文(快照)输出 ----
class MDCContextFormatItem : public LogFormatter::FormatItem {
public:
    MDCContextFormatItem(const std::string& = "") {}
    void format(std::ostream& os, std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr event) override {
        auto& ctx = event->getMDCContext();
        os << "{";
        bool first = true;
        for (auto& kv : ctx) {
            if (!first) os << ",";
            os << kv.first << "=" << kv.second;
            first = false;
        }
        os << "}";
    }
};

// ======================== FormatItemFactory ========================

FormatItemFactory& FormatItemFactory::GetInstance() {
    static FormatItemFactory instance;
    return instance;
}

FormatItemFactory::FormatItemFactory() {
    // 注册所有内置格式项
#define REGISTER(key, cls) \
    registerCreator(key, [](const std::string& fmt) -> LogFormatter::FormatItem::ptr { \
        return std::make_shared<cls>(fmt); \
    })

    REGISTER("m", MessageFormatItem);
    REGISTER("p", LevelFormatItem);
    REGISTER("P", ShortLevelFormatItem);
    REGISTER("r", ElapseFormatItem);
    REGISTER("R", ElapseUsFormatItem);
    REGISTER("c", NameFormatItem);
    REGISTER("t", ThreadIdFormatItem);
    REGISTER("n", NewLineFormatItem);
    REGISTER("d", DateTimeFormatItem);
    REGISTER("D", DateTimeUsFormatItem);
    REGISTER("f", FilenameFormatItem);
    REGISTER("l", LineFormatItem);
    REGISTER("T", TabFormatItem);
    REGISTER("F", FiberIdFormatItem);
    REGISTER("N", ThreadNameFormatItem);
    REGISTER("C", FunctionNameFormatItem);
    REGISTER("i", ProcessIdFormatItem);
    REGISTER("h", HostnameFormatItem);
    REGISTER("a", AppNameFormatItem);
    REGISTER("S", TraceIdFormatItem);
    REGISTER("s", SpanIdFormatItem);
    REGISTER("X", FullMDCFormatItem);
    REGISTER("J", MDCJsonFormatItem);
    REGISTER("e", NDCFormatItem);
    REGISTER("E", NDCDepthFormatItem);
    REGISTER("K", AllTagsFormatItem);
    REGISTER("Y", ColorStartFormatItem);
    REGISTER("y", ColorEndFormatItem);
    REGISTER("b", PlainTextFormatItem);
    REGISTER("L", LevelNumFormatItem);
    REGISTER("G", SyslogPriorityFormatItem);
    REGISTER("W", MDCContextFormatItem);

    // %x{key} 和 %k{key} 和 %B{key} 需要特殊处理 (带参数)
    // 这些在 LogFormatter::init() 中直接处理

#undef REGISTER
}

void FormatItemFactory::registerCreator(const std::string& key, Creator creator) {
    m_creators[key] = creator;
}

LogFormatter::FormatItem::ptr FormatItemFactory::create(const std::string& key, const std::string& fmt) {
    auto it = m_creators.find(key);
    if (it != m_creators.end()) {
        return it->second(fmt);
    }
    return nullptr;
}

bool FormatItemFactory::hasCreator(const std::string& key) const {
    return m_creators.find(key) != m_creators.end();
}

std::vector<std::string> FormatItemFactory::getRegisteredKeys() const {
    std::vector<std::string> keys;
    for (auto& kv : m_creators) {
        keys.push_back(kv.first);
    }
    return keys;
}

// ======================== LogFormatter ========================

LogFormatter::LogFormatter(const std::string& pattern)
    : m_pattern(pattern) {
    // 处理预定义快捷格式名
    if (pattern == "json" || pattern == "JSON") {
        m_output_format = OutputFormat::JSON;
        m_pattern = "json";
    } else if (pattern == "xml" || pattern == "XML") {
        m_output_format = OutputFormat::XML;
        m_pattern = "xml";
    } else if (pattern == "ltsv" || pattern == "LTSV") {
        m_output_format = OutputFormat::LTSV;
        m_pattern = "ltsv";
    } else if (pattern == "cef" || pattern == "CEF") {
        m_output_format = OutputFormat::CEF;
        m_pattern = "cef";
    } else {
        m_output_format = OutputFormat::PATTERN;
        init();
    }
}

void LogFormatter::setPattern(const std::string& pattern) {
    m_pattern = pattern;
    m_items.clear();
    m_error = false;
    if (pattern == "json" || pattern == "JSON") {
        m_output_format = OutputFormat::JSON;
    } else if (pattern == "xml" || pattern == "XML") {
        m_output_format = OutputFormat::XML;
    } else if (pattern == "ltsv" || pattern == "LTSV") {
        m_output_format = OutputFormat::LTSV;
    } else if (pattern == "cef" || pattern == "CEF") {
        m_output_format = OutputFormat::CEF;
    } else {
        m_output_format = OutputFormat::PATTERN;
        init();
    }
}

std::string LogFormatter::format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!event) return "";

    switch (m_output_format) {
        case OutputFormat::JSON:
            return formatJson(logger, level, event);
        case OutputFormat::XML:
            return formatXml(logger, level, event);
        case OutputFormat::LTSV:
            return formatLtsv(logger, level, event);
        case OutputFormat::CEF:
            return formatCef(logger, level, event);
        default:
            break;
    }

    std::stringstream ss;
    for (auto& i : m_items) {
        i->format(ss, logger, level, event);
    }
    return ss.str();
}

std::ostream& LogFormatter::format(std::ostream& ofs, std::shared_ptr<Logger> logger,
                                    LogLevel::Level level, LogEvent::ptr event) {
    if (!event) return ofs;

    switch (m_output_format) {
        case OutputFormat::JSON:
            ofs << formatJson(logger, level, event);
            break;
        case OutputFormat::XML:
            ofs << formatXml(logger, level, event);
            break;
        case OutputFormat::LTSV:
            ofs << formatLtsv(logger, level, event);
            break;
        case OutputFormat::CEF:
            ofs << formatCef(logger, level, event);
            break;
        default:
            for (auto& i : m_items) {
                i->format(ofs, logger, level, event);
            }
            break;
    }
    return ofs;
}

std::string LogFormatter::formatJson(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    std::stringstream js;
    js << "{";

    js << "\"timestamp\":" << event->getTime();
    js << ",\"timestamp_us\":" << event->getTimeUs();

    struct tm tm;
    time_t t = event->getTime();
    localtime_r(&t, &tm);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm);
    js << ",\"datetime\":\"" << time_buf << "." << event->getTimeUs() << "\"";

    js << ",\"level\":\"" << LogLevel::ToString(level) << "\"";
    js << ",\"level_num\":" << static_cast<int>(level);
    js << ",\"logger\":\"" << (logger ? logger->getName() : "unknown") << "\"";
    js << ",\"thread_id\":" << event->getThreadId();
    js << ",\"thread_name\":\"" << event->getThreadName() << "\"";
    js << ",\"fiber_id\":" << event->getFiberId();
    js << ",\"process_id\":" << event->getProcessId();
    js << ",\"hostname\":\"" << event->getHostname() << "\"";
    js << ",\"file\":\"" << (event->getFile() ? event->getFile() : "") << "\"";
    js << ",\"line\":" << event->getLine();

    if (!event->getTraceId().empty()) {
        js << ",\"trace_id\":\"" << event->getTraceId() << "\"";
    }
    if (!event->getSpanId().empty()) {
        js << ",\"span_id\":\"" << event->getSpanId() << "\"";
    }
    if (!event->getFunctionName().empty()) {
        js << ",\"function\":\"" << event->getFunctionName() << "\"";
    }

    // 转义消息中的特殊字符
    std::string msg = event->getContent();
    js << ",\"message\":\"";
    for (char c : msg) {
        switch (c) {
            case '"':  js << "\\\""; break;
            case '\\': js << "\\\\"; break;
            case '\n': js << "\\n"; break;
            case '\r': js << "\\r"; break;
            case '\t': js << "\\t"; break;
            default:   js << c;
        }
    }
    js << "\"";

    // MDC
    auto mdc = event->getMDCContext();
    if (!mdc.empty()) {
        js << ",\"mdc\":{";
        bool first = true;
        for (auto& kv : mdc) {
            if (!first) js << ",";
            js << "\"" << kv.first << "\":\"" << kv.second << "\"";
            first = false;
        }
        js << "}";
    }

    // Tags
    auto& tags = event->getTags();
    if (!tags.empty()) {
        js << ",\"tags\":{";
        bool first = true;
        for (auto& kv : tags) {
            if (!first) js << ",";
            js << "\"" << kv.first << "\":\"" << kv.second << "\"";
            first = false;
        }
        js << "}";
    }

    js << "}\n";
    return js.str();
}

std::string LogFormatter::formatXml(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    std::stringstream xml;
    xml << "<log>";

    xml << "<timestamp>" << event->getTime() << "." << event->getTimeUs() << "</timestamp>";
    xml << "<level>" << LogLevel::ToString(level) << "</level>";
    xml << "<logger>" << (logger ? logger->getName() : "unknown") << "</logger>";
    xml << "<thread_id>" << event->getThreadId() << "</thread_id>";
    xml << "<thread_name>" << event->getThreadName() << "</thread_name>";
    xml << "<fiber_id>" << event->getFiberId() << "</fiber_id>";
    xml << "<process_id>" << event->getProcessId() << "</process_id>";
    xml << "<hostname>" << event->getHostname() << "</hostname>";
    xml << "<file>" << (event->getFile() ? event->getFile() : "") << "</file>";
    xml << "<line>" << event->getLine() << "</line>";

    if (!event->getTraceId().empty()) {
        xml << "<trace_id>" << event->getTraceId() << "</trace_id>";
    }
    if (!event->getSpanId().empty()) {
        xml << "<span_id>" << event->getSpanId() << "</span_id>";
    }

    xml << "<message>" << event->getContent() << "</message>";

    // MDC
    auto& mdc = event->getMDCContext();
    if (!mdc.empty()) {
        xml << "<mdc>";
        for (auto& kv : mdc) {
            xml << "<" << kv.first << ">" << kv.second << "</" << kv.first << ">";
        }
        xml << "</mdc>";
    }

    xml << "</log>\n";
    return xml.str();
}

std::string LogFormatter::formatLtsv(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    std::stringstream ss;
    ss << "time:" << event->getTime();
    ss << "\tlevel:" << LogLevel::ToString(level);
    ss << "\tlogger:" << (logger ? logger->getName() : "unknown");
    ss << "\tthread_id:" << event->getThreadId();
    ss << "\tthread_name:" << event->getThreadName();
    ss << "\tfiber_id:" << event->getFiberId();
    ss << "\tprocess_id:" << event->getProcessId();
    ss << "\thostname:" << event->getHostname();
    ss << "\tfile:" << (event->getFile() ? event->getFile() : "");
    ss << "\tline:" << event->getLine();
    ss << "\tmessage:" << event->getContent();

    if (!event->getTraceId().empty()) {
        ss << "\ttrace_id:" << event->getTraceId();
    }

    ss << "\n";
    return ss.str();
}

std::string LogFormatter::formatCef(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    // CEF: CEF:0|DeviceVendor|DeviceProduct|DeviceVersion|SignatureID|Name|Severity|Extension
    std::stringstream ss;
    ss << "CEF:0|" << (event->getAppName().empty() ? "zero" : event->getAppName());
    ss << "|zero-kv|1.0";
    ss << "|log_event|";
    ss << LogLevel::ToString(level);
    ss << "|";

    // Severity映射: DEBUG=0, INFO=1, WARN=4, ERROR=7, FATAL=10
    int sev = 0;
    switch (level) {
        case LogLevel::TRACE: sev = 0; break;
        case LogLevel::DEBUG: sev = 0; break;
        case LogLevel::INFO:  sev = 1; break;
        case LogLevel::NOTICE: sev = 2; break;
        case LogLevel::WARN: sev = 4; break;
        case LogLevel::ERROR: sev = 7; break;
        case LogLevel::CRITICAL: sev = 9; break;
        case LogLevel::ALERT: sev = 9; break;
        case LogLevel::FATAL: sev = 10; break;
        case LogLevel::EMERGENCY: sev = 10; break;
        default: sev = 0;
    }
    ss << sev;

    // Extensions
    ss << "|msg=" << event->getContent();
    ss << " src=" << (event->getFile() ? event->getFile() : "");
    ss << " sln=" << event->getLine();
    ss << " dhost=" << event->getHostname();
    ss << " duser=" << (logger ? logger->getName() : "unknown");
    ss << " cn1=" << event->getThreadId();
    ss << " cn1Label=ThreadId";
    ss << " cn2=" << event->getFiberId();
    ss << " cn2Label=FiberId";

    ss << "\n";
    return ss.str();
}

// 解析模式字符串 %xxx 或 %xxx{fmt}
void LogFormatter::init() {
    // <string, format_string, type>
    // type: 0=普通字符串, 1=格式项, 2=带参数的格式项(%x{key})
    std::vector<std::tuple<std::string, std::string, int>> vec;
    std::string nstr;

    auto& factory = FormatItemFactory::GetInstance();

    for (size_t i = 0; i < m_pattern.size(); ++i) {
        if (m_pattern[i] != '%') {
            nstr.append(1, m_pattern[i]);
            continue;
        }

        // 处理 %%
        if ((i + 1) < m_pattern.size() && m_pattern[i + 1] == '%') {
            nstr.append(1, '%');
            ++i; // skip next %
            continue;
        }

        // 开始解析格式说明符
        size_t n = i + 1;
        int fmt_status = 0;  // 0=解析key, 1=解析{fmt}
        size_t fmt_begin = 0;
        std::string str;  // 格式键
        std::string fmt;  // 格式参数

        while (n < m_pattern.size()) {
            if (fmt_status == 0) {
                if (m_pattern[n] == '{') {
                    str = m_pattern.substr(i + 1, n - i - 1);
                    fmt_status = 1;
                    fmt_begin = n;
                    ++n;
                    continue;
                }
                if (!isalpha(m_pattern[n])) {
                    str = m_pattern.substr(i + 1, n - i - 1);
                    break;
                }
            } else if (fmt_status == 1) {
                if (m_pattern[n] == '}') {
                    fmt = m_pattern.substr(fmt_begin + 1, n - fmt_begin - 1);
                    fmt_status = 0;
                    ++n;
                    break;
                }
            }
            ++n;
            if (n == m_pattern.size()) {
                if (str.empty()) {
                    str = m_pattern.substr(i + 1);
                }
            }
        }

        if (fmt_status == 0) {
            // 正常解析完成
            if (!nstr.empty()) {
                vec.push_back(std::make_tuple(nstr, std::string(), 0));
                nstr.clear();
            }

            // 处理带参数的格式项 (%x, %k, %B)
            if (str == "x" && !fmt.empty()) {
                vec.push_back(std::make_tuple("x", fmt, 2));
            } else if (str == "k" && !fmt.empty()) {
                vec.push_back(std::make_tuple("k", fmt, 2));
            } else if (str == "B" && !fmt.empty()) {
                vec.push_back(std::make_tuple("B", fmt, 2));
            } else {
                vec.push_back(std::make_tuple(str, fmt, 1));
            }
            i = n - 1;
        } else if (fmt_status == 1) {
            std::cout << "pattern parse error: " << m_pattern << " - " << m_pattern.substr(i) << std::endl;
            m_error = true;
            vec.push_back(std::make_tuple("<<pattern_error>>", "", 0));
        }
    }

    if (!nstr.empty()) {
        vec.push_back(std::make_tuple(nstr, "", 0));
    }

    // 构建 FormatItem 列表
    for (auto& i : vec) {
        int type = std::get<2>(i);
        if (type == 0) {
            // 普通字符串
            m_items.push_back(std::make_shared<StringFormatItem>(std::get<0>(i)));
        } else if (type == 2) {
            // 带参数的格式项
            std::string key = std::get<0>(i);
            std::string arg = std::get<1>(i);

            if (key == "x") {
                m_items.push_back(std::make_shared<MDCFormatItem>(arg));
            } else if (key == "k") {
                m_items.push_back(std::make_shared<TagFormatItem>(arg));
            } else if (key == "B") {
                // 自定义扩展 - 暂作为字符串输出
                m_items.push_back(std::make_shared<StringFormatItem>(arg));
            }
        } else {
            // 标准格式项
            std::string key = std::get<0>(i);
            std::string arg = std::get<1>(i);

            if (key == "x") {
                // %x 无参数 - 输出完整MDC
                m_items.push_back(std::make_shared<FullMDCFormatItem>(""));
                continue;
            }
            if (key == "k") {
                // %k 无参数 - 输出所有tags
                m_items.push_back(std::make_shared<AllTagsFormatItem>(""));
                continue;
            }
            if (key == "B") {
                m_items.push_back(std::make_shared<StringFormatItem>(""));
                continue;
            }

            auto item = factory.create(key, arg);
            if (item) {
                m_items.push_back(item);
            } else {
                m_items.push_back(std::make_shared<StringFormatItem>(
                    "<<error_format %" + key + ">>"));
                m_error = true;
            }
        }
    }
}

void LogFormatter::registerFormatItem(const std::string& key,
                                       std::function<FormatItem::ptr(const std::string&)> factory) {
    FormatItemFactory::GetInstance().registerCreator(key, factory);
}

// ======================== 便捷格式化器 ========================

namespace LogFormatters {

LogFormatter::ptr Default() {
    return std::make_shared<LogFormatter>(
        "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n");
}

LogFormatter::ptr Simple() {
    return std::make_shared<LogFormatter>(
        "%d{%H:%M:%S} %P [%c] %m%n");
}

LogFormatter::ptr Verbose() {
    return std::make_shared<LogFormatter>(
        "%d{%Y-%m-%d %H:%M:%S}.%D{} %i %h %t/%N/%F [%p] [%c] %C %f:%l %S/%s %m %X %e%n");
}

LogFormatter::ptr Json() {
    return std::make_shared<LogFormatter>("json");
}

LogFormatter::ptr Production() {
    return std::make_shared<LogFormatter>(
        "%d{%Y-%m-%d %H:%M:%S} %P [%c] %f:%l %m %X%n");
}

LogFormatter::ptr Development() {
    return std::make_shared<LogFormatter>(
        "%Y%d{%H:%M:%S} %P [%c] %f:%l%y %m%n");
}

LogFormatter::ptr Syslog() {
    return std::make_shared<LogFormatter>(
        "%d{%b %d %H:%M:%S} %h %a[%i]: %c[%t]: %f:%l %m%n");
}

LogFormatter::ptr Cef() {
    return std::make_shared<LogFormatter>("cef");
}

LogFormatter::ptr Ltsv() {
    return std::make_shared<LogFormatter>("ltsv");
}

} // namespace LogFormatters

} // namespace zero

/**
 * @file log.cc
 * @brief 日志系统核心实现 + 配置集成
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log.h"
#include <map>
#include <iostream>
#include <functional>
#include <time.h>
#include <string.h>
#include <algorithm>
#include "zero/core/config/config.h"
#include "zero/core/util/util.h"
#include "zero/core/base/macro.h"
#include "zero/core/util/env.h"

namespace zero {

// ======================== Logger ========================

Logger::Logger(const std::string& name)
    : m_name(name)
    , m_level(LogLevel::DEBUG)
    , m_log_count(0) {
    m_formatter.reset(new LogFormatter(
        "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
}

Logger::~Logger() {
    // 清理所有Appender
    for (auto& appender : m_appenders) {
        appender->close();
    }
}

void Logger::log(LogLevel::Level level, LogEvent::ptr event) {
    if (level >= m_level) {
        auto self = shared_from_this();
        MutexType::Lock lock(m_mutex);

        // 应用过滤器
        if (!applyFilters(level, event)) {
            return;
        }

        m_log_count.fetch_add(1, std::memory_order_relaxed);

        if (!m_appenders.empty()) {
            for (auto& i : m_appenders) {
                if (i->isEnabled()) {
                    i->log(self, level, event);
                }
            }
        } else if (m_root && m_root.get() != this) {
            m_root->log(level, event);
        }

        // 更新统计
        LogStatsManager::GetInstance().recordLog(
            level, event->getContent().size(),
            false, false, false, 0);
    }
}

void Logger::trace(LogEvent::ptr event)     { log(LogLevel::TRACE, event); }
void Logger::debug(LogEvent::ptr event)     { log(LogLevel::DEBUG, event); }
void Logger::info(LogEvent::ptr event)      { log(LogLevel::INFO, event); }
void Logger::notice(LogEvent::ptr event)    { log(LogLevel::NOTICE, event); }
void Logger::warn(LogEvent::ptr event)      { log(LogLevel::WARN, event); }
void Logger::error(LogEvent::ptr event)     { log(LogLevel::ERROR, event); }
void Logger::critical(LogEvent::ptr event)  { log(LogLevel::CRITICAL, event); }
void Logger::alert(LogEvent::ptr event)     { log(LogLevel::ALERT, event); }
void Logger::fatal(LogEvent::ptr event)     { log(LogLevel::FATAL, event); }
void Logger::emergency(LogEvent::ptr event) { log(LogLevel::EMERGENCY, event); }

void Logger::addAppender(LogAppender::ptr appender) {
    MutexType::Lock lock(m_mutex);
    if (!appender) return;

    if (!appender->getFormatter()) {
        MutexType::Lock ll(appender->m_mutex);
        appender->m_formatter = m_formatter;
    }
    m_appenders.push_back(appender);
}

void Logger::delAppender(LogAppender::ptr appender) {
    MutexType::Lock lock(m_mutex);
    for (auto it = m_appenders.begin(); it != m_appenders.end(); ++it) {
        if (*it == appender) {
            (*it)->close();
            m_appenders.erase(it);
            break;
        }
    }
}

void Logger::clearAppenders() {
    MutexType::Lock lock(m_mutex);
    for (auto& a : m_appenders) {
        a->close();
    }
    m_appenders.clear();
}

void Logger::addFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    if (filter) {
        m_filters.push_back(filter);
    }
}

void Logger::removeFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    auto it = std::find(m_filters.begin(), m_filters.end(), filter);
    if (it != m_filters.end()) {
        m_filters.erase(it);
    }
}

void Logger::clearFilters() {
    MutexType::Lock lock(m_mutex);
    m_filters.clear();
}

bool Logger::applyFilters(LogLevel::Level level, LogEvent::ptr event) {
    for (auto& f : m_filters) {
        FilterResult r = f->decide(shared_from_this(), level, event);
        if (r == FilterResult::DENY) return false;
    }
    return true;
}

void Logger::setFormatter(LogFormatter::ptr val) {
    MutexType::Lock lock(m_mutex);
    m_formatter = val;

    for (auto& i : m_appenders) {
        MutexType::Lock ll(i->m_mutex);
        if (!i->m_hasFormatter) {
            i->m_formatter = m_formatter;
        }
    }
}

void Logger::setFormatter(const std::string& val) {
    LogFormatter::ptr new_val(new LogFormatter(val));
    if (new_val->isError()) {
        std::cout << "Logger setFormatter name=" << m_name
                  << " value=" << val << " invalid formatter"
                  << std::endl;
        return;
    }
    setFormatter(new_val);
}

LogFormatter::ptr Logger::getFormatter() {
    MutexType::Lock lock(m_mutex);
    return m_formatter;
}

std::string Logger::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["name"] = m_name;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }

    for (auto& i : m_appenders) {
        node["appenders"].push_back(YAML::Load(i->toYamlString()));
    }

    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== LoggerManager ========================

LoggerManager::LoggerManager() {
    m_root.reset(new Logger);
    m_root->addAppender(LogAppender::ptr(new StdoutLogAppender));
    m_loggers[m_root->m_name] = m_root;
    init();
}

Logger::ptr LoggerManager::getLogger(const std::string& name) {
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    if (it != m_loggers.end()) {
        return it->second;
    }

    Logger::ptr logger(new Logger(name));
    logger->m_root = m_root;
    m_loggers[name] = logger;
    return logger;
}

Logger::ptr LoggerManager::lookupLogger(const std::string& name) const {
    MutexType::Lock lock(m_mutex);
    auto it = m_loggers.find(name);
    return (it != m_loggers.end()) ? it->second : nullptr;
}

void LoggerManager::removeLogger(const std::string& name) {
    MutexType::Lock lock(m_mutex);
    if (name == "root") return;  // 不能删除root
    auto it = m_loggers.find(name);
    if (it != m_loggers.end()) {
        it->second->clearAppenders();
        m_loggers.erase(it);
    }
}

std::map<std::string, Logger::ptr> LoggerManager::getAllLoggers() const {
    MutexType::Lock lock(m_mutex);
    return m_loggers;
}

std::string LoggerManager::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    for (auto& i : m_loggers) {
        node.push_back(YAML::Load(i.second->toYamlString()));
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void LoggerManager::visit(std::function<void(Logger::ptr)> cb) {
    MutexType::Lock lock(m_mutex);
    for (auto& kv : m_loggers) {
        cb(kv.second);
    }
}

void LoggerManager::init() {
    // 初始化完成后的回调，可用于加载配置文件等
}

// ======================== 配置集成 (向后兼容) ========================

// AppenderDefine used by existing config system
struct LogAppenderDefine {
    int type = 0; // 1=File, 2=Stdout, 3=ColorStdout, 4=Stderr,
                  // 5=RotatingFile, 6=TimeRotatingFile, 7=Syslog,
                  // 8=UDP, 9=TCP, 10=Memory, 11=Null
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::string file;
    std::string host;
    uint16_t port = 514;
    uint64_t max_file_size = 100 * 1024 * 1024;
    int max_backup_index = 10;
    bool compress = false;
    int rotation_policy = 3; // daily
    std::string mode = "sync"; // sync/async
    size_t async_queue_size = 64 * 1024 * 1024;
    size_t async_batch_size = 256;

    bool operator==(const LogAppenderDefine& oth) const {
        return type == oth.type
            && level == oth.level
            && formatter == oth.formatter
            && file == oth.file
            && host == oth.host
            && port == oth.port
            && mode == oth.mode;
    }
};

struct LogDefine {
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::vector<LogAppenderDefine> appenders;

    bool operator==(const LogDefine& oth) const {
        return name == oth.name
            && level == oth.level
            && formatter == oth.formatter
            && appenders == oth.appenders;
    }

    bool operator<(const LogDefine& oth) const {
        return name < oth.name;
    }

    bool isValid() const {
        return !name.empty();
    }
};

// 创建Appender的辅助函数
static LogAppender::ptr CreateAppenderFromDefine(const LogAppenderDefine& lad) {
    LogAppender::ptr ap;

    switch (lad.type) {
        case 1: // FileLogAppender
            ap.reset(new FileLogAppender(lad.file));
            break;
        case 2: // StdoutLogAppender
            ap.reset(new StdoutLogAppender());
            break;
        case 3: // ColorStdoutLogAppender
            ap.reset(new ColorStdoutLogAppender());
            break;
        case 4: // StderrLogAppender
            ap.reset(new StderrLogAppender());
            break;
        case 5: // RotatingFileLogAppender
            ap.reset(new RotatingFileLogAppender(
                lad.file, lad.max_file_size, lad.max_backup_index, lad.compress));
            break;
        case 6: // TimeRotatingFileLogAppender
            ap.reset(new TimeRotatingFileLogAppender(
                lad.file, static_cast<RotationPolicy>(lad.rotation_policy)));
            break;
        case 7: // SyslogLogAppender
            ap.reset(new SyslogLogAppender("zero"));
            break;
        case 8: // UDPLogAppender
            ap.reset(new UDPLogAppender(lad.host, lad.port));
            break;
        case 9: // TCPLogAppender
            ap.reset(new TCPLogAppender(lad.host, lad.port));
            break;
        case 10: // MemoryLogAppender
            ap.reset(new MemoryLogAppender(10000));
            break;
        case 11: // NullLogAppender
            ap.reset(new NullLogAppender());
            break;
        default:
            return nullptr;
    }

    // 如果配置为异步模式
    if (lad.mode == "async" && ap) {
        AsyncLogConfig async_cfg;
        async_cfg.queue_size = lad.async_queue_size;
        async_cfg.batch_size = lad.async_batch_size;
        async_cfg.overflow_policy = AsyncOverflowPolicy::BLOCK;
        ap = std::make_shared<AsyncAppenderWrapper>(ap, async_cfg);
    }

    return ap;
}

// LexicalCast特化 - YAML字符串到LogDefine
template<>
class LexicalCast<std::string, LogDefine> {
public:
    LogDefine operator()(const std::string& v) {
        YAML::Node n = YAML::Load(v);
        LogDefine ld;
        if (!n["name"].IsDefined()) {
            std::cout << "log config error: name is null, " << n << std::endl;
            throw std::logic_error("log config name is null");
        }
        ld.name = n["name"].as<std::string>();
        ld.level = LogLevel::FromString(
            n["level"].IsDefined() ? n["level"].as<std::string>() : "");

        if (n["formatter"].IsDefined()) {
            ld.formatter = n["formatter"].as<std::string>();
        }

        if (n["appenders"].IsDefined()) {
            for (size_t x = 0; x < n["appenders"].size(); ++x) {
                auto a = n["appenders"][x];
                if (!a["type"].IsDefined()) {
                    std::cout << "log config error: appender type is null, " << a << std::endl;
                    continue;
                }

                std::string type = a["type"].as<std::string>();
                LogAppenderDefine lad;

                // 解析mode
                if (a["mode"].IsDefined()) {
                    lad.mode = a["mode"].as<std::string>();
                }

                // 解析通用属性
                if (a["level"].IsDefined()) {
                    lad.level = LogLevel::FromString(a["level"].as<std::string>());
                }
                if (a["formatter"].IsDefined()) {
                    lad.formatter = a["formatter"].as<std::string>();
                }
                if (a["file"].IsDefined()) {
                    lad.file = a["file"].as<std::string>();
                }
                if (a["host"].IsDefined()) {
                    lad.host = a["host"].as<std::string>();
                }
                if (a["port"].IsDefined()) {
                    lad.port = a["port"].as<uint16_t>();
                }
                if (a["max_file_size"].IsDefined()) {
                    lad.max_file_size = a["max_file_size"].as<uint64_t>();
                }
                if (a["max_backup_index"].IsDefined()) {
                    lad.max_backup_index = a["max_backup_index"].as<int>();
                }
                if (a["compress"].IsDefined()) {
                    lad.compress = a["compress"].as<bool>();
                }

                // 异步参数
                if (a["async"].IsDefined()) {
                    auto async_n = a["async"];
                    if (async_n["queue_size"].IsDefined()) {
                        lad.async_queue_size = async_n["queue_size"].as<size_t>();
                    }
                    if (async_n["batch_size"].IsDefined()) {
                        lad.async_batch_size = async_n["batch_size"].as<size_t>();
                    }
                }

                // 类型映射
                if (type == "FileLogAppender" || type == "FileAppender") {
                    lad.type = 1;
                    if (lad.file.empty() && a["file"].IsDefined()) {
                        lad.file = a["file"].as<std::string>();
                    }
                } else if (type == "StdoutLogAppender" || type == "StdoutAppender") {
                    lad.type = 2;
                } else if (type == "ColorStdoutLogAppender" || type == "ColorStdoutAppender") {
                    lad.type = 3;
                } else if (type == "StderrLogAppender" || type == "StderrAppender") {
                    lad.type = 4;
                } else if (type == "RotatingFileLogAppender" || type == "RotatingFileAppender") {
                    lad.type = 5;
                    if (a["file"].IsDefined()) lad.file = a["file"].as<std::string>();
                } else if (type == "TimeRotatingFileLogAppender" || type == "TimeRotatingFileAppender") {
                    lad.type = 6;
                    if (a["file"].IsDefined()) lad.file = a["file"].as<std::string>();
                } else if (type == "SyslogLogAppender" || type == "SyslogAppender") {
                    lad.type = 7;
                } else if (type == "UDPLogAppender" || type == "UDPAppender") {
                    lad.type = 8;
                } else if (type == "TCPLogAppender" || type == "TCPAppender") {
                    lad.type = 9;
                } else if (type == "MemoryLogAppender" || type == "MemoryAppender") {
                    lad.type = 10;
                } else if (type == "NullLogAppender" || type == "NullAppender") {
                    lad.type = 11;
                } else {
                    std::cout << "log config error: unknown appender type: " << type << std::endl;
                    continue;
                }

                ld.appenders.push_back(lad);
            }
        }

        return ld;
    }
};

// LexicalCast特化 - LogDefine到YAML字符串
template<>
class LexicalCast<LogDefine, std::string> {
public:
    std::string operator()(const LogDefine& i) {
        YAML::Node n;
        n["name"] = i.name;
        if (i.level != LogLevel::UNKNOW) {
            n["level"] = LogLevel::ToString(i.level);
        }
        if (!i.formatter.empty()) {
            n["formatter"] = i.formatter;
        }

        for (auto& a : i.appenders) {
            YAML::Node na;
            static const char* type_names[] = {
                "Unknown", "FileLogAppender", "StdoutLogAppender",
                "ColorStdoutLogAppender", "StderrLogAppender",
                "RotatingFileLogAppender", "TimeRotatingFileLogAppender",
                "SyslogLogAppender", "UDPLogAppender", "TCPLogAppender",
                "MemoryLogAppender", "NullLogAppender"
            };
            if (a.type >= 1 && a.type <= 11) {
                na["type"] = type_names[a.type];
            }
            if (a.level != LogLevel::UNKNOW) {
                na["level"] = LogLevel::ToString(a.level);
            }
            if (!a.formatter.empty()) {
                na["formatter"] = a.formatter;
            }
            if (!a.file.empty()) {
                na["file"] = a.file;
            }
            if (!a.mode.empty()) {
                na["mode"] = a.mode;
            }
            n["appenders"].push_back(na);
        }

        std::stringstream ss;
        ss << n;
        return ss.str();
    }
};

// 全局日志配置 (保持向后兼容)
zero::ConfigVar<std::set<LogDefine>>::ptr g_log_defines =
    zero::Config::Lookup("logs", std::set<LogDefine>(), "logs config");

// 日志配置变更回调
struct LogIniter {
    LogIniter() {
        g_log_defines->addListener([](const std::set<LogDefine>& old_value,
                    const std::set<LogDefine>& new_value){
            ZERO_LOG_INFO(ZERO_LOG_ROOT()) << "on_logger_conf_changed";

            // 处理新增/修改的logger
            for (auto& i : new_value) {
                auto it = old_value.find(i);
                zero::Logger::ptr logger;
                if (it == old_value.end()) {
                    // 新增logger
                    logger = ZERO_LOG_NAME(i.name);
                } else {
                    if (!(i == *it)) {
                        // 修改的logger
                        logger = ZERO_LOG_NAME(i.name);
                    } else {
                        continue;
                    }
                }
                logger->setLevel(i.level);

                if (!i.formatter.empty()) {
                    logger->setFormatter(i.formatter);
                }

                logger->clearAppenders();
                for (auto& a : i.appenders) {
                    zero::LogAppender::ptr ap = CreateAppenderFromDefine(a);
                    if (!ap) continue;

                    ap->setLevel(a.level);
                    if (!a.formatter.empty()) {
                        LogFormatter::ptr fmt(new LogFormatter(a.formatter));
                        if (!fmt->isError()) {
                            ap->setFormatter(fmt);
                        } else {
                            std::cout << "log.name=" << i.name
                                      << " appender type=" << a.type
                                      << " formatter=" << a.formatter
                                      << " is invalid" << std::endl;
                        }
                    }

                    logger->addAppender(ap);
                }
            }

            // 处理删除的logger
            for (auto& i : old_value) {
                auto it = new_value.find(i);
                if (it == new_value.end()) {
                    auto logger = ZERO_LOG_NAME(i.name);
                    logger->setLevel((LogLevel::Level)0);
                    logger->clearAppenders();
                }
            }
        });
    }
};

static LogIniter __log_init;

} // namespace zero

/**
 * @file log_config.cc
 * @brief 日志配置管理实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_config.h"
#include "log.h"
#include "log_color.h"
#include "zero/core/config/config.h"
#include <iostream>
#include <sstream>

namespace zero {

// ======================== LoggerDefine ========================

bool LoggerDefine::operator==(const LoggerDefine& oth) const {
    return name == oth.name
        && level == oth.level
        && formatter == oth.formatter
        && appenders == oth.appenders;
}

bool LoggerDefine::operator<(const LoggerDefine& oth) const {
    return name < oth.name;
}

// ======================== LogConfigManager ========================

LogConfigManager& LogConfigManager::GetInstance() {
    static LogConfigManager instance;
    return instance;
}

LogConfigManager::LogConfigManager() {
}

bool LogConfigManager::loadFromYaml(const YAML::Node& root) {
    try {
        if (!root.IsDefined()) {
            return false;
        }

        // 解析全局配置
        if (root["global"] && root["global"].IsDefined()) {
            m_global_config = parseGlobalConfig(root["global"]);
        }

        // 解析Logger定义
        if (root["loggers"] && root["loggers"].IsDefined() && root["loggers"].IsSequence()) {
            m_logger_defines.clear();
            for (size_t i = 0; i < root["loggers"].size(); ++i) {
                auto ld = parseLogger(root["loggers"][i]);
                if (ld.isValid()) {
                    m_logger_defines.insert(ld);
                }
            }
        }

        // 解析颜色方案
        if (root["color_scheme"] && root["color_scheme"].IsDefined()) {
            std::string scheme = root["color_scheme"].as<std::string>();
            ColorManager::GetInstance().setConfigByName(scheme);
        }

        return true;
    } catch (std::exception& e) {
        std::cerr << "LogConfigManager::loadFromYaml error: " << e.what() << std::endl;
        return false;
    }
}

void LogConfigManager::applyConfig() {
    auto& loggerMgr = *LoggerMgr::GetInstance();

    for (auto& ld : m_logger_defines) {
        Logger::ptr logger = loggerMgr.getLogger(ld.name);

        if (ld.level != LogLevel::UNKNOW) {
            logger->setLevel(ld.level);
        }

        if (!ld.formatter.empty()) {
            logger->setFormatter(ld.formatter);
        }

        logger->clearAppenders();

        for (auto& ad : ld.appenders) {
            LogAppender::ptr appender;

            // 创建基础Appender
            appender = createAppender(ad);

            if (!appender) {
                std::cerr << "Failed to create appender: " << ad.type
                          << " for logger: " << ld.name << std::endl;
                continue;
            }

            // 如果需要异步模式
            if (ad.mode == "async") {
                appender = createAsyncAppender(appender, ad);
            }

            appender->setLevel(ad.level != LogLevel::UNKNOW ? ad.level : m_global_config.default_level);

            if (!ad.formatter.empty()) {
                LogFormatter::ptr fmt(new LogFormatter(ad.formatter));
                if (!fmt->isError()) {
                    appender->setFormatter(fmt);
                }
            }

            logger->addAppender(appender);
        }
    }
}

LogAppender::ptr LogConfigManager::createAppender(const AppenderDefine& def) {
    LogAppender::ptr appender;

    if (def.type == "StdoutLogAppender" || def.type == "StdoutAppender") {
        appender.reset(new StdoutLogAppender());
    } else if (def.type == "StderrLogAppender") {
        appender.reset(new StderrLogAppender());
    } else if (def.type == "ColorStdoutLogAppender" || def.type == "ColorStdoutAppender") {
        appender.reset(new ColorStdoutLogAppender());
    } else if (def.type == "FileLogAppender" || def.type == "FileAppender") {
        if (def.file.empty()) {
            std::cerr << "FileAppender requires file path" << std::endl;
            return nullptr;
        }
        appender.reset(new FileLogAppender(def.file, def.append));
    } else if (def.type == "RotatingFileLogAppender" || def.type == "RotatingFileAppender") {
        if (def.file.empty()) {
            std::cerr << "RotatingFileAppender requires file path" << std::endl;
            return nullptr;
        }
        appender.reset(new RotatingFileLogAppender(
            def.file, def.max_file_size, def.max_backup_index, def.compress));
    } else if (def.type == "TimeRotatingFileLogAppender" || def.type == "TimeRotatingFileAppender") {
        if (def.file.empty()) {
            std::cerr << "TimeRotatingFileAppender requires file path" << std::endl;
            return nullptr;
        }
        appender.reset(new TimeRotatingFileLogAppender(
            def.file, static_cast<RotationPolicy>(def.rotation_policy), def.max_history));
    } else if (def.type == "SyslogLogAppender" || def.type == "SyslogAppender") {
        appender.reset(new SyslogLogAppender(
            def.syslog_ident, def.syslog_option ? def.syslog_option : LOG_PID | LOG_CONS,
            def.syslog_facility ? def.syslog_facility : LOG_USER));
    } else if (def.type == "UDPLogAppender" || def.type == "UDPAppender") {
        if (def.host.empty()) {
            std::cerr << "UDPAppender requires host" << std::endl;
            return nullptr;
        }
        appender.reset(new UDPLogAppender(def.host, def.port));
    } else if (def.type == "TCPLogAppender" || def.type == "TCPAppender") {
        if (def.host.empty()) {
            std::cerr << "TCPAppender requires host" << std::endl;
            return nullptr;
        }
        appender.reset(new TCPLogAppender(def.host, def.port));
    } else if (def.type == "MemoryLogAppender" || def.type == "MemoryAppender") {
        appender.reset(new MemoryLogAppender(def.memory_capacity));
    } else if (def.type == "NullLogAppender" || def.type == "NullAppender") {
        appender.reset(new NullLogAppender());
    } else if (def.type == "MultiFileLogAppender" || def.type == "MultiFileAppender") {
        appender.reset(new MultiFileLogAppender(def.file, "%c.log"));
    } else {
        std::cerr << "Unknown appender type: " << def.type << std::endl;
        return nullptr;
    }

    return appender;
}

LogAppender::ptr LogConfigManager::createAsyncAppender(LogAppender::ptr appender,
                                                         const AppenderDefine& def) {
    AsyncLogConfig async_cfg;
    async_cfg.queue_size = def.async_queue_size;
    async_cfg.batch_size = def.async_batch_size;
    async_cfg.flush_interval_ms = def.async_flush_interval_ms;
    async_cfg.max_flush_interval_ms = def.async_max_flush_interval_ms;

    // 解析溢出策略
    if (def.async_overflow_policy == "block") {
        async_cfg.overflow_policy = AsyncOverflowPolicy::BLOCK;
    } else if (def.async_overflow_policy == "drop_oldest") {
        async_cfg.overflow_policy = AsyncOverflowPolicy::DROP_OLDEST;
    } else if (def.async_overflow_policy == "drop_newest") {
        async_cfg.overflow_policy = AsyncOverflowPolicy::DROP_NEWEST;
    } else if (def.async_overflow_policy == "discard") {
        async_cfg.overflow_policy = AsyncOverflowPolicy::DISCARD;
    }

    return std::make_shared<AsyncAppenderWrapper>(appender, async_cfg);
}

AppenderDefine LogConfigManager::parseAppender(const YAML::Node& node) {
    AppenderDefine def;

    if (node["type"].IsDefined()) {
        def.type = node["type"].as<std::string>();
    }
    if (node["level"].IsDefined()) {
        def.level = LogLevel::FromString(node["level"].as<std::string>());
    }
    if (node["mode"].IsDefined()) {
        def.mode = node["mode"].as<std::string>();
    }
    if (node["formatter"].IsDefined()) {
        def.formatter = node["formatter"].as<std::string>();
    }
    if (node["enabled"].IsDefined()) {
        def.enabled = node["enabled"].as<bool>();
    }

    // 文件参数
    if (node["file"].IsDefined()) {
        def.file = node["file"].as<std::string>();
    }
    if (node["append"].IsDefined()) {
        def.append = node["append"].as<bool>();
    }

    // 滚动参数
    if (node["max_file_size"].IsDefined()) {
        def.max_file_size = node["max_file_size"].as<uint64_t>();
    }
    if (node["max_backup_index"].IsDefined()) {
        def.max_backup_index = node["max_backup_index"].as<int>();
    }
    if (node["compress"].IsDefined()) {
        def.compress = node["compress"].as<bool>();
    }
    if (node["rotation_policy"].IsDefined()) {
        def.rotation_policy = node["rotation_policy"].as<int>();
    }
    if (node["max_history"].IsDefined()) {
        def.max_history = node["max_history"].as<int>();
    }

    // Syslog参数
    if (node["ident"].IsDefined()) {
        def.syslog_ident = node["ident"].as<std::string>();
    }
    if (node["facility"].IsDefined()) {
        def.syslog_facility = node["facility"].as<int>();
    }

    // 网络参数
    if (node["host"].IsDefined()) {
        def.host = node["host"].as<std::string>();
    }
    if (node["port"].IsDefined()) {
        def.port = node["port"].as<uint16_t>();
    }

    // 内存参数
    if (node["capacity"].IsDefined()) {
        def.memory_capacity = node["capacity"].as<size_t>();
    }

    // 异步参数
    if (node["async"].IsDefined()) {
        auto& async_node = node["async"];
        if (async_node["queue_size"].IsDefined()) {
            def.async_queue_size = async_node["queue_size"].as<size_t>();
        }
        if (async_node["batch_size"].IsDefined()) {
            def.async_batch_size = async_node["batch_size"].as<size_t>();
        }
        if (async_node["flush_interval_ms"].IsDefined()) {
            def.async_flush_interval_ms = async_node["flush_interval_ms"].as<uint32_t>();
        }
        if (async_node["max_flush_interval_ms"].IsDefined()) {
            def.async_max_flush_interval_ms = async_node["max_flush_interval_ms"].as<uint32_t>();
        }
        if (async_node["overflow_policy"].IsDefined()) {
            def.async_overflow_policy = async_node["overflow_policy"].as<std::string>();
        }
    }

    return def;
}

LoggerDefine LogConfigManager::parseLogger(const YAML::Node& node) {
    LoggerDefine ld;

    if (!node["name"].IsDefined()) {
        std::cerr << "Logger config error: name is required" << std::endl;
        return ld;
    }

    ld.name = node["name"].as<std::string>();
    if (node["level"].IsDefined()) {
        ld.level = LogLevel::FromString(node["level"].as<std::string>());
    }
    if (node["formatter"].IsDefined()) {
        ld.formatter = node["formatter"].as<std::string>();
    }

    if (node["appenders"].IsDefined() && node["appenders"].IsSequence()) {
        for (size_t i = 0; i < node["appenders"].size(); ++i) {
            ld.appenders.push_back(parseAppender(node["appenders"][i]));
        }
    }

    return ld;
}

LogGlobalConfig LogConfigManager::parseGlobalConfig(const YAML::Node& node) {
    LogGlobalConfig cfg;

    if (node["default_level"].IsDefined()) {
        cfg.default_level = LogLevel::FromString(node["default_level"].as<std::string>());
    }
    if (node["default_mode"].IsDefined()) {
        cfg.default_mode = node["default_mode"].as<std::string>();
    }
    if (node["default_formatter"].IsDefined()) {
        cfg.default_formatter = node["default_formatter"].as<std::string>();
    }
    if (node["color_scheme"].IsDefined()) {
        cfg.color_scheme = node["color_scheme"].as<std::string>();
    }
    if (node["stats_enabled"].IsDefined()) {
        cfg.stats_enabled = node["stats_enabled"].as<bool>();
    }
    if (node["stats_report_interval_sec"].IsDefined()) {
        cfg.stats_report_interval_sec = node["stats_report_interval_sec"].as<uint32_t>();
    }

    // 异步默认参数
    if (node["async"].IsDefined()) {
        auto& async = node["async"];
        if (async["queue_size"].IsDefined()) {
            cfg.default_queue_size = async["queue_size"].as<size_t>();
        }
        if (async["batch_size"].IsDefined()) {
            cfg.default_batch_size = async["batch_size"].as<size_t>();
        }
        if (async["flush_interval_ms"].IsDefined()) {
            cfg.default_flush_interval_ms = async["flush_interval_ms"].as<uint32_t>();
        }
        if (async["overflow_policy"].IsDefined()) {
            cfg.default_overflow_policy = async["overflow_policy"].as<std::string>();
        }
    }

    return cfg;
}

void LogConfigManager::setGlobalConfig(const LogGlobalConfig& cfg) {
    m_global_config = cfg;
}

std::string LogConfigManager::exportToYaml() const {
    YAML::Node root;
    root["version"] = 1;

    // 全局配置
    YAML::Node global;
    global["default_level"] = LogLevel::ToString(m_global_config.default_level);
    global["default_mode"] = m_global_config.default_mode;
    global["color_scheme"] = m_global_config.color_scheme;
    root["global"] = global;

    // Logger配置
    for (auto& ld : m_logger_defines) {
        YAML::Node logger_node;
        logger_node["name"] = ld.name;
        if (ld.level != LogLevel::UNKNOW) {
            logger_node["level"] = LogLevel::ToString(ld.level);
        }
        if (!ld.formatter.empty()) {
            logger_node["formatter"] = ld.formatter;
        }

        for (auto& ad : ld.appenders) {
            YAML::Node appender_node;
            appender_node["type"] = ad.type;
            appender_node["mode"] = ad.mode;
            if (ad.level != LogLevel::UNKNOW) {
                appender_node["level"] = LogLevel::ToString(ad.level);
            }
            if (!ad.formatter.empty()) {
                appender_node["formatter"] = ad.formatter;
            }
            if (!ad.file.empty()) {
                appender_node["file"] = ad.file;
            }
            if (!ad.host.empty()) {
                appender_node["host"] = ad.host;
                appender_node["port"] = ad.port;
            }
            logger_node["appenders"].push_back(appender_node);
        }

        root["loggers"].push_back(logger_node);
    }

    std::stringstream ss;
    ss << root;
    return ss.str();
}

} // namespace zero

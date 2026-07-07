/**
 * @file log_config.h
 * @brief 日志系统的YAML配置集成
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_CONFIG_H__
#define __ZERO_LOG_CONFIG_H__

#include <string>
#include <memory>
#include <set>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "log_level.h"
#include "log_appender.h"
#include "log_formatter.h"
#include "log_filter.h"
#include "async_log.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {

class Logger;

/**
 * @brief Appender配置描述符
 */
struct AppenderDefine {
    /// Appender类型
    std::string type;

    /// 日志级别
    LogLevel::Level level = LogLevel::UNKNOW;

    /// 运行模式: sync / async
    std::string mode = "sync";

    /// 格式化器模板
    std::string formatter;

    /// 是否启用
    bool enabled = true;

    // === 文件类型Appender通用参数 ===
    std::string file;
    bool append = true;

    // === RotatingFileAppender参数 ===
    uint64_t max_file_size = 100 * 1024 * 1024;
    int max_backup_index = 10;
    bool compress = false;

    // === TimeRotatingFileAppender参数 ===
    int rotation_policy = 3;  // daily
    int max_history = 30;

    // === SyslogAppender参数 ===
    std::string syslog_ident = "zero";
    int syslog_option = 0;
    int syslog_facility = 0;

    // === 网络Appender参数 ===
    std::string host;
    uint16_t port = 514;

    // === MemoryAppender参数 ===
    size_t memory_capacity = 10000;

    // === 异步参数 ===
    size_t async_queue_size = 64 * 1024 * 1024;
    size_t async_batch_size = 256;
    uint32_t async_flush_interval_ms = 5;
    uint32_t async_max_flush_interval_ms = 100;
    std::string async_overflow_policy = "block";

    // === 过滤器 ===
    std::vector<std::string> filter_names;

    bool operator==(const AppenderDefine& oth) const {
        return type == oth.type && level == oth.level && mode == oth.mode
            && formatter == oth.formatter && enabled == oth.enabled
            && file == oth.file && host == oth.host && port == oth.port;
    }
};

/**
 * @brief Logger配置描述符
 */
struct LoggerDefine {
    std::string name;
    LogLevel::Level level = LogLevel::UNKNOW;
    std::string formatter;
    std::vector<AppenderDefine> appenders;

    bool operator==(const LoggerDefine& oth) const;
    bool operator<(const LoggerDefine& oth) const;
    bool isValid() const { return !name.empty(); }
};

/**
 * @brief 日志全局配置
 */
struct LogGlobalConfig {
    LogLevel::Level default_level = LogLevel::DEBUG;
    std::string default_mode = "sync";           // sync / async
    std::string default_formatter;                // 空=使用格式化器内置默认值
    std::string color_scheme = "default";         // default / monokai / solarized
    bool stats_enabled = true;
    uint32_t stats_report_interval_sec = 60;

    // 异步默认参数
    size_t default_queue_size = 64 * 1024 * 1024;
    size_t default_batch_size = 256;
    uint32_t default_flush_interval_ms = 5;
    std::string default_overflow_policy = "block";
};

/**
 * @brief 日志配置管理器
 * @details 负责从YAML解析日志配置并应用到LoggerManager
 */
class LogConfigManager {
public:
    typedef std::shared_ptr<LogConfigManager> ptr;

    /**
     * @brief 获取单例
     */
    static LogConfigManager& GetInstance();

    /**
     * @brief 从YAML节点加载配置
     */
    bool loadFromYaml(const YAML::Node& root);

    /**
     * @brief 应用配置到LoggerManager
     */
    void applyConfig();

    /**
     * @brief 从LoggerManager导出当前配置为YAML
     */
    std::string exportToYaml() const;

    /**
     * @brief 获取全局配置
     */
    const LogGlobalConfig& getGlobalConfig() const { return m_global_config; }
    void setGlobalConfig(const LogGlobalConfig& cfg);

    /**
     * @brief 获取Logger定义列表
     */
    const std::set<LoggerDefine>& getLoggerDefines() const { return m_logger_defines; }

    /**
     * @brief 根据AppenderDefine创建Appender实例
     */
    static LogAppender::ptr createAppender(const AppenderDefine& def);

    /**
     * @brief 根据AppenderDefine创建异步包装器
     */
    static LogAppender::ptr createAsyncAppender(LogAppender::ptr appender, const AppenderDefine& def);

private:
    LogConfigManager();

    /**
     * @brief 解析单个Appender定义
     */
    static AppenderDefine parseAppender(const YAML::Node& node);

    /**
     * @brief 解析Logger定义
     */
    static LoggerDefine parseLogger(const YAML::Node& node);

    /**
     * @brief 解析全局配置
     */
    static LogGlobalConfig parseGlobalConfig(const YAML::Node& node);

    LogGlobalConfig m_global_config;
    std::set<LoggerDefine> m_logger_defines;
};

} // namespace zero

#endif // __ZERO_LOG_CONFIG_H__

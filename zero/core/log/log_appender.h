/**
 * @file log_appender.h
 * @brief 日志Appender系统 - 所有输出目标
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_APPENDER_H__
#define __ZERO_LOG_APPENDER_H__

#include <string>
#include <memory>
#include <vector>
#include <list>
#include <fstream>
#include <functional>
#include <syslog.h>
#include "log_level.h"
#include "log_event.h"
#include "log_formatter.h"
#include "log_filter.h"
#include "zero/core/concurrency/mutex.h"
#include "zero/core/base/noncopyable.h"

namespace zero {

class Logger;

/**
 * @brief Appender溢出策略 (异步模式使用)
 */
enum class AsyncOverflowPolicy {
    DROP_OLDEST = 0,  /// 丢弃最旧的消息
    DROP_NEWEST = 1,  /// 丢弃最新的消息
    BLOCK       = 2,  /// 阻塞等待
    DISCARD     = 3   /// 直接丢弃，不等待
};

/**
 * @brief 文件滚动策略
 */
enum class RotationPolicy {
    NONE      = 0,  /// 不滚动
    SIZE      = 1,  /// 按大小滚动
    HOURLY    = 2,  /// 每小时滚动
    DAILY     = 3,  /// 每天滚动
    WEEKLY    = 4,  /// 每周滚动
    MONTHLY   = 5,  /// 每月滚动
    MINUTELY  = 6   /// 每分钟滚动
};

/**
 * @brief 日志Appender抽象基类
 */
class LogAppender {
friend class Logger;
public:
    typedef std::shared_ptr<LogAppender> ptr;
    typedef Spinlock MutexType;

    virtual ~LogAppender() {}

    /**
     * @brief 写入日志 (纯虚函数)
     */
    virtual void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) = 0;

    /**
     * @brief 将配置转成YAML字符串
     */
    virtual std::string toYamlString() = 0;

    /**
     * @brief 获取Appender类型名称
     */
    virtual std::string getTypeName() const = 0;

    /**
     * @brief 刷新缓冲区
     */
    virtual void flush() {}

    /**
     * @brief 关闭Appender
     */
    virtual void close() {}

    // ===== 通用属性 =====
    void setFormatter(LogFormatter::ptr val);
    LogFormatter::ptr getFormatter();
    LogLevel::Level getLevel() const { return m_level; }
    void setLevel(LogLevel::Level val) { m_level = val; }

    /**
     * @brief 添加过滤器
     */
    void addFilter(LogFilter::ptr filter);
    void removeFilter(LogFilter::ptr filter);
    void clearFilters();

    /**
     * @brief 应用过滤器链
     * @return true=通过, false=被过滤
     */
    bool applyFilters(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 设置Appender是否启用
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

protected:
    /// 日志级别阈值
    LogLevel::Level m_level = LogLevel::DEBUG;
    /// 是否有自己的格式器
    bool m_hasFormatter = false;
    /// 是否启用
    bool m_enabled = true;
    /// 自旋锁
    mutable MutexType m_mutex;
    /// 格式器
    LogFormatter::ptr m_formatter;
    /// 过滤器链
    std::vector<LogFilter::ptr> m_filters;
};

// ======================== 控制台 Appender ========================

/**
 * @brief 标准输出Appender
 */
class StdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StdoutLogAppender> ptr;

    StdoutLogAppender();
    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "StdoutLogAppender"; }
};

/**
 * @brief 标准错误输出Appender
 */
class StderrLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<StderrLogAppender> ptr;

    StderrLogAppender();
    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "StderrLogAppender"; }
};

/**
 * @brief 带颜色的标准输出Appender
 */
class ColorStdoutLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<ColorStdoutLogAppender> ptr;

    ColorStdoutLogAppender();
    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "ColorStdoutLogAppender"; }
};

// ======================== 文件 Appender ========================

/**
 * @brief 基础文件Appender
 */
class FileLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<FileLogAppender> ptr;

    /**
     * @brief 构造函数
     * @param[in] filename 文件路径 (支持strftime格式如 app_%Y%m%d.log)
     * @param[in] append true=追加模式, false=覆盖模式
     */
    FileLogAppender(const std::string& filename, bool append = true);

    ~FileLogAppender();

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "FileLogAppender"; }

    void flush() override;
    void close() override;

    /**
     * @brief 重新打开日志文件 (文件轮转后使用)
     */
    bool reopen();

    const std::string& getFilename() const { return m_filename; }
    void setFilename(const std::string& filename);

    /**
     * @brief 设置自动重开间隔 (秒)
     */
    void setReopenInterval(uint32_t sec) { m_reopen_interval = sec; }
    uint32_t getReopenInterval() const { return m_reopen_interval; }

    /**
     * @brief 设置写入缓冲大小
     */
    void setBufferSize(size_t size) { m_buffer_size = size; }
    size_t getBufferSize() const { return m_buffer_size; }

protected:
    /// 文件路径
    std::string m_filename;
    /// 文件流
    std::ofstream m_filestream;
    /// 上次重新打开时间
    uint64_t m_lastTime = 0;
    /// 重新打开间隔 (秒, 默认3秒)
    uint32_t m_reopen_interval = 3;
    /// 是否追加模式
    bool m_append = true;
    /// 缓冲区大小
    size_t m_buffer_size = 8192;
    /// 写入缓冲区
    std::vector<char> m_write_buffer;
};

/**
 * @brief 按大小滚动的文件Appender
 */
class RotatingFileLogAppender : public FileLogAppender {
public:
    typedef std::shared_ptr<RotatingFileLogAppender> ptr;

    /**
     * @brief 构造函数
     * @param[in] filename 基础文件名
     * @param[in] max_file_size 单个文件最大大小 (字节, 默认100MB)
     * @param[in] max_backup_index 最大备份文件数 (0=不限制)
     * @param[in] compress 是否压缩旧文件
     */
    RotatingFileLogAppender(const std::string& filename,
                             uint64_t max_file_size = 100 * 1024 * 1024,
                             int max_backup_index = 10,
                             bool compress = false);

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "RotatingFileLogAppender"; }

    void setMaxFileSize(uint64_t size) { m_max_file_size = size; }
    uint64_t getMaxFileSize() const { return m_max_file_size; }
    void setMaxBackupIndex(int n) { m_max_backup_index = n; }
    int getMaxBackupIndex() const { return m_max_backup_index; }
    void setCompress(bool v) { m_compress = v; }
    bool getCompress() const { return m_compress; }

private:
    /**
     * @brief 滚动文件
     */
    void rotate();

    /// 当前文件大小
    uint64_t m_current_size = 0;
    /// 最大文件大小
    uint64_t m_max_file_size;
    /// 最大备份数
    int m_max_backup_index;
    /// 是否压缩
    bool m_compress;
};

/**
 * @brief 按时间滚动的文件Appender
 */
class TimeRotatingFileLogAppender : public FileLogAppender {
public:
    typedef std::shared_ptr<TimeRotatingFileLogAppender> ptr;

    /**
     * @param[in] filename 文件名 (支持strftime格式)
     * @param[in] policy 滚动策略
     * @param[in] max_history 保留文件天数 (0=永久保留)
     */
    TimeRotatingFileLogAppender(const std::string& filename,
                                 RotationPolicy policy = RotationPolicy::DAILY,
                                 int max_history = 30);

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "TimeRotatingFileLogAppender"; }

    void setRotationPolicy(RotationPolicy policy) { m_policy = policy; }
    RotationPolicy getRotationPolicy() const { return m_policy; }
    void setMaxHistory(int days) { m_max_history = days; }
    int getMaxHistory() const { return m_max_history; }

private:
    /**
     * @brief 检查是否需要滚动
     */
    bool needRotate(uint64_t now);

    /**
     * @brief 执行滚动
     */
    void doRotate();

    /**
     * @brief 清理过期文件
     */
    void cleanupOldFiles();

    RotationPolicy m_policy;
    int m_max_history;
    uint64_t m_last_rotate_time = 0;
    std::string m_current_date;  // 记录当前日期字符串用于判断
};

// ======================== 系统日志 Appender ========================

/**
 * @brief Syslog Appender
 */
class SyslogLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<SyslogLogAppender> ptr;

    /**
     * @param[in] ident 标识字符串
     * @param[in] option syslog选项 (LOG_PID, LOG_CONS等)
     * @param[in] facility syslog设施 (LOG_USER, LOG_LOCAL0等)
     */
    SyslogLogAppender(const std::string& ident = "zero",
                       int option = LOG_PID | LOG_CONS,
                       int facility = LOG_USER);

    ~SyslogLogAppender();

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "SyslogLogAppender"; }

private:
    std::string m_ident;
    int m_option;
    int m_facility;
    bool m_opened = false;
};

// ======================== 网络 Appender ========================

/**
 * @brief UDP日志Appender
 */
class UDPLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<UDPLogAppender> ptr;

    UDPLogAppender(const std::string& host, uint16_t port);
    ~UDPLogAppender();

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "UDPLogAppender"; }

    void setHost(const std::string& host) { m_host = host; }
    void setPort(uint16_t port) { m_port = port; }
    const std::string& getHost() const { return m_host; }
    uint16_t getPort() const { return m_port; }

private:
    bool connect();
    std::string m_host;
    uint16_t m_port;
    int m_sockfd = -1;
};

/**
 * @brief TCP日志Appender
 */
class TCPLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<TCPLogAppender> ptr;

    TCPLogAppender(const std::string& host, uint16_t port);
    ~TCPLogAppender();

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "TCPLogAppender"; }

private:
    bool connect();
    bool reconnect();

    std::string m_host;
    uint16_t m_port;
    int m_sockfd = -1;
    uint64_t m_last_reconnect = 0;
    static const uint64_t RECONNECT_INTERVAL = 30;  // 30秒重连间隔
};

// ======================== 内存 Appender ========================

/**
 * @brief 内存环形缓冲区Appender
 * @details 在内存中保留最近N条日志，用于调试和诊断
 */
class MemoryLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<MemoryLogAppender> ptr;

    struct LogEntry {
        uint64_t timestamp;
        LogLevel::Level level;
        std::string logger_name;
        std::string message;
        std::string file;
        int32_t line;
    };

    explicit MemoryLogAppender(size_t capacity = 10000);

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "MemoryLogAppender"; }

    /**
     * @brief 获取所有缓存的日志 (线程安全拷贝)
     */
    std::vector<LogEntry> getLogs() const;

    /**
     * @brief 获取最新N条日志
     */
    std::vector<LogEntry> getRecent(size_t n = 100) const;

    /**
     * @brief 按级别过滤获取日志
     */
    std::vector<LogEntry> getByLevel(LogLevel::Level min_level) const;

    /**
     * @brief 清空缓存
     */
    void clear();

    /**
     * @brief 获取当前日志条数
     */
    size_t size() const;

    void setCapacity(size_t cap);

private:
    mutable MutexType m_data_mutex;
    std::vector<LogEntry> m_entries;
    size_t m_capacity;
    size_t m_write_index = 0;
};

// ======================== 其他 Appender ========================

/**
 * @brief 空Appender - 丢弃所有日志
 */
class NullLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<NullLogAppender> ptr;

    void log(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr) override {}
    std::string toYamlString() override;
    std::string getTypeName() const override { return "NullLogAppender"; }
};

/**
 * @brief 回调Appender - 自定义处理函数
 */
class CallbackLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<CallbackLogAppender> ptr;
    typedef std::function<void(std::shared_ptr<Logger>, LogLevel::Level, LogEvent::ptr)> LogCallback;

    explicit CallbackLogAppender(LogCallback cb);

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "CallbackLogAppender"; }

    void setCallback(LogCallback cb);

private:
    LogCallback m_callback;
};

/**
 * @brief 多文件Appender - 按logger名称路由到不同文件
 */
class MultiFileLogAppender : public LogAppender {
public:
    typedef std::shared_ptr<MultiFileLogAppender> ptr;

    /**
     * @param[in] base_dir 基础目录
     * @param[in] pattern 文件名模式 (%c = logger名称)
     */
    MultiFileLogAppender(const std::string& base_dir,
                          const std::string& pattern = "%c.log");

    void log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) override;
    std::string toYamlString() override;
    std::string getTypeName() const override { return "MultiFileLogAppender"; }

    void flush() override;
    void close() override;

private:
    FileLogAppender::ptr getOrCreateAppender(const std::string& logger_name);

    std::string m_base_dir;
    std::string m_pattern;
    mutable Spinlock m_file_mutex;
    std::map<std::string, FileLogAppender::ptr> m_appenders;
};

} // namespace zero

#endif // __ZERO_LOG_APPENDER_H__

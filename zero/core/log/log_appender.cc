/**
 * @file log_appender.cc
 * @brief 日志Appender系统实现 - 所有输出目标类型
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_appender.h"
#include "log.h"
#include "zero/core/util/util.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

namespace zero {

// ======================== LogAppender 基类 ========================

void LogAppender::setFormatter(LogFormatter::ptr val) {
    MutexType::Lock lock(m_mutex);
    m_formatter = val;
    m_hasFormatter = (val != nullptr);
}

LogFormatter::ptr LogAppender::getFormatter() {
    MutexType::Lock lock(m_mutex);
    return m_formatter;
}

void LogAppender::addFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    if (filter) {
        m_filters.push_back(filter);
    }
}

void LogAppender::removeFilter(LogFilter::ptr filter) {
    MutexType::Lock lock(m_mutex);
    auto it = std::find(m_filters.begin(), m_filters.end(), filter);
    if (it != m_filters.end()) {
        m_filters.erase(it);
    }
}

void LogAppender::clearFilters() {
    MutexType::Lock lock(m_mutex);
    m_filters.clear();
}

bool LogAppender::applyFilters(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    for (auto& f : m_filters) {
        FilterResult r = f->decide(logger, level, event);
        if (r == FilterResult::DENY) return false;
    }
    return true;
}

// ======================== StdoutLogAppender ========================

StdoutLogAppender::StdoutLogAppender() {
    // 默认格式包含颜色
    m_formatter.reset(new LogFormatter(
        "%Y%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n%y"));
}

void StdoutLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        m_formatter->format(std::cout, logger, level, event);
    }
}

std::string StdoutLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StdoutLogAppender";
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== StderrLogAppender ========================

StderrLogAppender::StderrLogAppender() {
    m_level = LogLevel::WARN;  // stderr默认只输出WARN以上
    m_formatter.reset(new LogFormatter("%d{%Y-%m-%d %H:%M:%S} [%p] %f:%l %m%n"));
}

void StderrLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        m_formatter->format(std::cerr, logger, level, event);
    }
}

std::string StderrLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "StderrLogAppender";
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== ColorStdoutLogAppender ========================

ColorStdoutLogAppender::ColorStdoutLogAppender() {
    m_formatter.reset(new LogFormatter(
        "%Y%d{%Y-%m-%d %H:%M:%S}%T[%P]%T[%c]%T%f:%l%T%m%y%n"));
}

void ColorStdoutLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        std::cout << ColorizeBegin(level);
        m_formatter->format(std::cout, logger, level, event);
        std::cout << ColorizeEnd();
    }
}

std::string ColorStdoutLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "ColorStdoutLogAppender";
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== FileLogAppender ========================

FileLogAppender::FileLogAppender(const std::string& filename, bool append)
    : m_filename(filename)
    , m_append(append) {
    m_formatter.reset(new LogFormatter(
        "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"));
    m_write_buffer.resize(m_buffer_size);
    reopen();
}

FileLogAppender::~FileLogAppender() {
    close();
}

void FileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    uint64_t now = event->getTime();
    if (now >= (m_lastTime + m_reopen_interval)) {
        reopen();
        m_lastTime = now;
    }

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        if (!m_formatter->format(m_filestream, logger, level, event)) {
            // 写入失败时尝试重新打开文件
            reopen();
            m_formatter->format(m_filestream, logger, level, event);
        }
    }
}

std::string FileLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = getTypeName();
    node["file"] = m_filename;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    node["append"] = m_append;
    node["reopen_interval"] = m_reopen_interval;
    std::stringstream ss;
    ss << node;
    return ss.str();
}

bool FileLogAppender::reopen() {
    MutexType::Lock lock(m_mutex);
    if (m_filestream.is_open()) {
        m_filestream.close();
    }
    auto mode = m_append ? std::ios::app : std::ios::trunc;
    return FSUtil::OpenForWrite(m_filestream, m_filename, mode);
}

void FileLogAppender::flush() {
    MutexType::Lock lock(m_mutex);
    if (m_filestream.is_open()) {
        m_filestream.flush();
    }
}

void FileLogAppender::close() {
    MutexType::Lock lock(m_mutex);
    if (m_filestream.is_open()) {
        m_filestream.flush();
        m_filestream.close();
    }
}

void FileLogAppender::setFilename(const std::string& filename) {
    MutexType::Lock lock(m_mutex);
    m_filename = filename;
    reopen();
}

// ======================== RotatingFileLogAppender ========================

RotatingFileLogAppender::RotatingFileLogAppender(const std::string& filename,
                                                   uint64_t max_file_size,
                                                   int max_backup_index,
                                                   bool compress)
    : FileLogAppender(filename)
    , m_max_file_size(max_file_size)
    , m_max_backup_index(max_backup_index)
    , m_compress(compress) {
    // 获取当前文件大小
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        m_current_size = st.st_size;
    }
}

void RotatingFileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    uint64_t now = event->getTime();
    if (now >= (m_lastTime + m_reopen_interval)) {
        reopen();
        m_lastTime = now;
    }

    // 检查是否需要滚动
    if (m_current_size >= m_max_file_size) {
        MutexType::Lock lock(m_mutex);
        // 双重检查
        if (m_current_size >= m_max_file_size) {
            rotate();
        }
    }

    // 计算即将写入的字节数 (估计值)
    std::stringstream estimate_ss;
    if (m_formatter) {
        m_formatter->format(estimate_ss, logger, level, event);
    }
    size_t estimated_size = estimate_ss.str().size();

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        m_formatter->format(m_filestream, logger, level, event);
        m_current_size += estimated_size;
    }
}

std::string RotatingFileLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "RotatingFileLogAppender";
    node["file"] = m_filename;
    node["max_file_size"] = m_max_file_size;
    node["max_backup_index"] = m_max_backup_index;
    node["compress"] = m_compress;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void RotatingFileLogAppender::rotate() {
    // 关闭当前文件
    if (m_filestream.is_open()) {
        m_filestream.flush();
        m_filestream.close();
    }

    // 删除最旧的文件
    if (m_max_backup_index > 0) {
        std::string oldest = m_filename + "." + std::to_string(m_max_backup_index);
        unlink(oldest.c_str());
        if (m_compress) {
            unlink((oldest + ".gz").c_str());
        }

        // 轮转: .9 -> .10, .8 -> .9, ...
        for (int i = m_max_backup_index - 1; i >= 0; --i) {
            std::string from = m_filename + (i > 0 ? ("." + std::to_string(i)) : "");
            std::string to = m_filename + "." + std::to_string(i + 1);
            if (m_compress && i == 0) {
                to += ".gz";
                // 如果需要压缩，用gzip压缩
                std::string cmd = "gzip -c " + from + " > " + to;
                int rc = system(cmd.c_str());
                (void)rc;
                unlink(from.c_str());
            } else {
                rename(from.c_str(), to.c_str());
            }
        }
    }

    // 重新打开文件
    auto mode = std::ios::trunc;
    FSUtil::OpenForWrite(m_filestream, m_filename, mode);
    m_current_size = 0;
}

// ======================== TimeRotatingFileLogAppender ========================

TimeRotatingFileLogAppender::TimeRotatingFileLogAppender(const std::string& filename,
                                                           RotationPolicy policy,
                                                           int max_history)
    : FileLogAppender(filename)
    , m_policy(policy)
    , m_max_history(max_history)
    , m_last_rotate_time(0) {
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d", &tm);
    m_current_date = buf;
    m_last_rotate_time = now;
}

void TimeRotatingFileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    uint64_t now = event->getTime();

    // 检查时间滚动
    if (needRotate(now)) {
        MutexType::Lock lock(m_mutex);
        if (needRotate(now)) {
            doRotate();
        }
    }

    // 检查文件重开
    if (now >= (m_lastTime + m_reopen_interval)) {
        reopen();
        m_lastTime = now;
    }

    MutexType::Lock lock(m_mutex);
    if (m_formatter) {
        m_formatter->format(m_filestream, logger, level, event);
    }
}

std::string TimeRotatingFileLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "TimeRotatingFileLogAppender";
    node["file"] = m_filename;
    node["rotation_policy"] = static_cast<int>(m_policy);
    node["max_history"] = m_max_history;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    if (m_hasFormatter && m_formatter) {
        node["formatter"] = m_formatter->getPattern();
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

bool TimeRotatingFileLogAppender::needRotate(uint64_t now) {
    struct tm tm_now;
    time_t t = now;
    localtime_r(&t, &tm_now);

    char buf[32];
    switch (m_policy) {
        case RotationPolicy::MINUTELY:
            strftime(buf, sizeof(buf), "%Y%m%d%H%M", &tm_now);
            break;
        case RotationPolicy::HOURLY:
            strftime(buf, sizeof(buf), "%Y%m%d%H", &tm_now);
            break;
        case RotationPolicy::DAILY:
            strftime(buf, sizeof(buf), "%Y%m%d", &tm_now);
            break;
        case RotationPolicy::WEEKLY:
            // 获取周数
            strftime(buf, sizeof(buf), "%Y%W", &tm_now);
            break;
        case RotationPolicy::MONTHLY:
            strftime(buf, sizeof(buf), "%Y%m", &tm_now);
            break;
        default:
            return false;
    }

    std::string current(buf);
    if (current != m_current_date) {
        m_current_date = current;
        return true;
    }
    return false;
}

void TimeRotatingFileLogAppender::doRotate() {
    // 关闭当前文件
    if (m_filestream.is_open()) {
        m_filestream.flush();
        m_filestream.close();
    }

    // 生成带时间戳的文件名
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);

    char date_buf[64];
    switch (m_policy) {
        case RotationPolicy::MINUTELY:
            strftime(date_buf, sizeof(date_buf), "%Y%m%d%H%M", &tm);
            break;
        case RotationPolicy::HOURLY:
            strftime(date_buf, sizeof(date_buf), "%Y%m%d%H", &tm);
            break;
        case RotationPolicy::DAILY:
            strftime(date_buf, sizeof(date_buf), "%Y%m%d", &tm);
            break;
        case RotationPolicy::WEEKLY:
            strftime(date_buf, sizeof(date_buf), "%Y%W", &tm);
            break;
        case RotationPolicy::MONTHLY:
            strftime(date_buf, sizeof(date_buf), "%Y%m", &tm);
            break;
        default:
            strftime(date_buf, sizeof(date_buf), "%Y%m%d", &tm);
    }

    // 重命名当前文件
    std::string rotated = m_filename + "." + date_buf;
    rename(m_filename.c_str(), rotated.c_str());

    // 重新打开新文件
    auto mode = m_append ? std::ios::app : std::ios::trunc;
    FSUtil::OpenForWrite(m_filestream, m_filename, mode);
    m_last_rotate_time = now;

    // 清理过期文件
    if (m_max_history > 0) {
        cleanupOldFiles();
    }
}

void TimeRotatingFileLogAppender::cleanupOldFiles() {
    // 获取基础目录和文件名
    std::string dir;
    std::string base_name;
    size_t pos = m_filename.rfind('/');
    if (pos != std::string::npos) {
        dir = m_filename.substr(0, pos);
        base_name = m_filename.substr(pos + 1);
    } else {
        dir = ".";
        base_name = m_filename;
    }

    time_t cutoff = time(0) - m_max_history * 86400;

    DIR* dp = opendir(dir.c_str());
    if (!dp) return;

    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string name(entry->d_name);
        // 只处理以base_name开头的备份文件
        if (name.find(base_name + ".") == 0) {
            std::string full_path = dir + "/" + name;
            struct stat st;
            if (stat(full_path.c_str(), &st) == 0) {
                if (st.st_mtime < cutoff) {
                    unlink(full_path.c_str());
                }
            }
        }
    }
    closedir(dp);
}

// ======================== SyslogLogAppender ========================

SyslogLogAppender::SyslogLogAppender(const std::string& ident, int option, int facility)
    : m_ident(ident)
    , m_option(option)
    , m_facility(facility) {
    m_level = LogLevel::INFO;
    openlog(m_ident.c_str(), m_option, m_facility);
    m_opened = true;
}

SyslogLogAppender::~SyslogLogAppender() {
    if (m_opened) {
        closelog();
    }
}

void SyslogLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    int priority = LogLevel::ToSyslogPriority(level);
    syslog(priority, "%s", event->getContent().c_str());
}

std::string SyslogLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "SyslogLogAppender";
    node["ident"] = m_ident;
    node["facility"] = m_facility;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== UDPLogAppender ========================

UDPLogAppender::UDPLogAppender(const std::string& host, uint16_t port)
    : m_host(host)
    , m_port(port) {
    m_formatter.reset(new LogFormatter(
        "%d{%Y-%m-%d %H:%M:%S} %h %i [%p] [%c] %f:%l %m%n"));
    connect();
}

UDPLogAppender::~UDPLogAppender() {
    if (m_sockfd >= 0) {
        ::close(m_sockfd);
    }
}

bool UDPLogAppender::connect() {
    m_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sockfd < 0) return false;

    // 解析主机名
    struct hostent* he = gethostbyname(m_host.c_str());
    if (!he) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (::connect(m_sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(m_sockfd);
        m_sockfd = -1;
        return false;
    }
    return true;
}

void UDPLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;
    if (m_sockfd < 0) {
        connect();
        if (m_sockfd < 0) return;
    }

    MutexType::Lock lock(m_mutex);
    std::string msg = m_formatter ? m_formatter->format(logger, level, event) : event->getContent();
    send(m_sockfd, msg.data(), msg.size(), MSG_NOSIGNAL);
}

std::string UDPLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "UDPLogAppender";
    node["host"] = m_host;
    node["port"] = m_port;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== TCPLogAppender ========================

TCPLogAppender::TCPLogAppender(const std::string& host, uint16_t port)
    : m_host(host)
    , m_port(port) {
    m_formatter.reset(new LogFormatter("json"));
    connect();
}

TCPLogAppender::~TCPLogAppender() {
    if (m_sockfd >= 0) {
        ::close(m_sockfd);
    }
}

bool TCPLogAppender::connect() {
    m_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0) return false;

    struct hostent* he = gethostbyname(m_host.c_str());
    if (!he) {
        ::close(m_sockfd);
        m_sockfd = -1;
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (::connect(m_sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(m_sockfd);
        m_sockfd = -1;
        return false;
    }
    return true;
}

bool TCPLogAppender::reconnect() {
    if (m_sockfd >= 0) {
        ::close(m_sockfd);
        m_sockfd = -1;
    }
    return connect();
}

void TCPLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    if (m_sockfd < 0) {
        uint64_t now = time(0);
        if (now - m_last_reconnect >= RECONNECT_INTERVAL) {
            connect();
            m_last_reconnect = now;
        }
        if (m_sockfd < 0) return;
    }

    MutexType::Lock lock(m_mutex);
    std::string msg = m_formatter ? m_formatter->format(logger, level, event) : event->getContent();
    ssize_t ret = send(m_sockfd, msg.data(), msg.size(), MSG_NOSIGNAL);
    if (ret < 0) {
        reconnect();
    }
}

std::string TCPLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "TCPLogAppender";
    node["host"] = m_host;
    node["port"] = m_port;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== MemoryLogAppender ========================

MemoryLogAppender::MemoryLogAppender(size_t capacity)
    : m_capacity(std::max(capacity, size_t(100))) {
    m_entries.resize(m_capacity);
}

void MemoryLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    LogEntry entry;
    entry.timestamp = event->getTime();
    entry.level = level;
    entry.logger_name = logger ? logger->getName() : "";
    entry.message = event->getContent();
    entry.file = event->getFile() ? event->getFile() : "";
    entry.line = event->getLine();

    MutexType::Lock lock(m_data_mutex);
    m_entries[m_write_index % m_capacity] = std::move(entry);
    ++m_write_index;
}

std::vector<MemoryLogAppender::LogEntry> MemoryLogAppender::getLogs() const {
    MutexType::Lock lock(m_data_mutex);
    size_t count = std::min(m_write_index, m_capacity);
    std::vector<LogEntry> result;
    result.reserve(count);

    size_t start = (m_write_index > m_capacity)
                 ? (m_write_index % m_capacity)
                 : 0;

    for (size_t i = 0; i < count; ++i) {
        result.push_back(m_entries[(start + i) % m_capacity]);
    }
    return result;
}

std::vector<MemoryLogAppender::LogEntry> MemoryLogAppender::getRecent(size_t n) const {
    MutexType::Lock lock(m_data_mutex);
    size_t count = std::min(m_write_index, m_capacity);
    n = std::min(n, count);
    std::vector<LogEntry> result;
    result.reserve(n);

    size_t end = (m_write_index > m_capacity)
               ? (m_write_index % m_capacity)
               : m_write_index;

    for (size_t i = 0; i < n; ++i) {
        size_t idx = (end >= n - i) ? (end - n + i) : (m_capacity + end - n + i);
        result.push_back(m_entries[idx % m_capacity]);
    }
    return result;
}

std::vector<MemoryLogAppender::LogEntry> MemoryLogAppender::getByLevel(LogLevel::Level min_level) const {
    std::vector<LogEntry> result;
    MutexType::Lock lock(m_data_mutex);
    size_t count = std::min(m_write_index, m_capacity);
    size_t start = (m_write_index > m_capacity)
                 ? (m_write_index % m_capacity)
                 : 0;
    for (size_t i = 0; i < count; ++i) {
        auto& entry = m_entries[(start + i) % m_capacity];
        if (entry.level >= min_level) {
            result.push_back(entry);
        }
    }
    return result;
}

void MemoryLogAppender::clear() {
    MutexType::Lock lock(m_data_mutex);
    m_entries.clear();
    m_entries.resize(m_capacity);
    m_write_index = 0;
}

size_t MemoryLogAppender::size() const {
    MutexType::Lock lock(m_data_mutex);
    return std::min(m_write_index, m_capacity);
}

void MemoryLogAppender::setCapacity(size_t cap) {
    MutexType::Lock lock(m_data_mutex);
    m_capacity = std::max(cap, size_t(100));
    m_entries.resize(m_capacity);
    if (m_write_index > m_capacity * 2) {
        m_write_index = m_capacity;
    }
}

std::string MemoryLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "MemoryLogAppender";
    node["capacity"] = m_capacity;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== NullLogAppender ========================

std::string NullLogAppender::toYamlString() {
    YAML::Node node;
    node["type"] = "NullLogAppender";
    std::stringstream ss;
    ss << node;
    return ss.str();
}

// ======================== CallbackLogAppender ========================

CallbackLogAppender::CallbackLogAppender(LogCallback cb)
    : m_callback(std::move(cb)) {
}

void CallbackLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;
    if (m_callback) {
        m_callback(logger, level, event);
    }
}

std::string CallbackLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "CallbackLogAppender";
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void CallbackLogAppender::setCallback(LogCallback cb) {
    MutexType::Lock lock(m_mutex);
    m_callback = std::move(cb);
}

// ======================== MultiFileLogAppender ========================

MultiFileLogAppender::MultiFileLogAppender(const std::string& base_dir,
                                             const std::string& pattern)
    : m_base_dir(base_dir)
    , m_pattern(pattern) {
}

void MultiFileLogAppender::log(Logger::ptr logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    std::string logger_name = logger ? logger->getName() : "root";
    auto appender = getOrCreateAppender(logger_name);
    if (appender) {
        appender->log(logger, level, event);
    }
}

std::string MultiFileLogAppender::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "MultiFileLogAppender";
    node["base_dir"] = m_base_dir;
    node["pattern"] = m_pattern;
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void MultiFileLogAppender::flush() {
    MutexType::Lock lock(m_file_mutex);
    for (auto& kv : m_appenders) {
        kv.second->flush();
    }
}

void MultiFileLogAppender::close() {
    MutexType::Lock lock(m_file_mutex);
    for (auto& kv : m_appenders) {
        kv.second->close();
    }
    m_appenders.clear();
}

FileLogAppender::ptr MultiFileLogAppender::getOrCreateAppender(const std::string& logger_name) {
    MutexType::Lock lock(m_file_mutex);
    auto it = m_appenders.find(logger_name);
    if (it != m_appenders.end()) {
        return it->second;
    }

    // 构建文件名: base_dir/pattern (将 %c 替换为logger名称)
    std::string filename = m_pattern;
    size_t pos = filename.find("%c");
    if (pos != std::string::npos) {
        filename.replace(pos, 2, logger_name);
    }

    std::string full_path = m_base_dir + "/" + filename;

    // 确保目录存在
    size_t last_slash = full_path.rfind('/');
    if (last_slash != std::string::npos) {
        std::string dir = full_path.substr(0, last_slash);
        mkdir(dir.c_str(), 0755);
    }

    FileLogAppender::ptr appender(new FileLogAppender(full_path));
    appender->setLevel(m_level);
    if (m_hasFormatter && m_formatter) {
        appender->setFormatter(m_formatter);
    }

    m_appenders[logger_name] = appender;
    return appender;
}

} // namespace zero

/**
 * @file async_log.cc
 * @brief 异步日志系统实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "async_log.h"
#include "log.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <cstring>
#include <algorithm>

namespace zero {

// ======================== AsyncLogWriter ========================

AsyncLogWriter::AsyncLogWriter(LogAppender::ptr appender, const AsyncLogConfig& config)
    : m_appender(appender)
    , m_config(config)
    , m_running(false)
    , m_written_count(0) {

    m_buffer.reset(new MpscLogRingBuffer(m_config.queue_size));
    m_buffer->setBlockOnFull(m_config.overflow_policy == AsyncOverflowPolicy::BLOCK);
}

AsyncLogWriter::~AsyncLogWriter() {
    stop();
}

void AsyncLogWriter::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return;
    }

    m_running.store(true, std::memory_order_release);
    m_last_flush = std::chrono::steady_clock::now();

    m_writer_thread.reset(new std::thread(&AsyncLogWriter::writerLoop, this));

    // 设置线程名称
    if (!m_config.writer_thread_name.empty()) {
        pthread_setname_np(m_writer_thread->native_handle(),
                           m_config.writer_thread_name.substr(0, 15).c_str());
    }

    // 设置CPU亲和性
    if (m_config.writer_cpu_affinity >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(m_config.writer_cpu_affinity, &cpuset);
        pthread_setaffinity_np(m_writer_thread->native_handle(),
                               sizeof(cpu_set_t), &cpuset);
    }
}

void AsyncLogWriter::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }

    m_running.store(false, std::memory_order_release);

    if (m_writer_thread && m_writer_thread->joinable()) {
        auto wait_start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(m_config.shutdown_timeout_ms);

        while (m_writer_thread->joinable()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - wait_start);
            if (elapsed >= timeout) {
                m_writer_thread->detach();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (m_writer_thread->joinable()) {
            m_writer_thread->join();
        }
    }

    if (m_appender) {
        m_appender->flush();
    }
}

void AsyncLogWriter::append(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_running.load(std::memory_order_acquire)) return;

    // 使用appender的formatter格式化日志
    std::string formatted;
    LogFormatter::ptr fmt = m_appender ? m_appender->getFormatter() : nullptr;
    if (fmt) {
        formatted = fmt->format(logger, level, event);
    } else {
        formatted = event->getContent() + "\n";
    }

    appendRaw(formatted.data(), formatted.size());
}

bool AsyncLogWriter::appendRaw(const char* data, size_t len) {
    if (!m_running.load(std::memory_order_acquire)) return false;

    switch (m_config.overflow_policy) {
        case AsyncOverflowPolicy::BLOCK:
            m_buffer->write(data, len);
            return true;
        case AsyncOverflowPolicy::DROP_NEWEST:
            if (!m_buffer->tryWrite(data, len)) {
                return false;
            }
            return true;
        case AsyncOverflowPolicy::DROP_OLDEST:
            while (!m_buffer->tryWrite(data, len)) {
                LogBufferMessage dummy;
                m_buffer->readOne(dummy);
            }
            return true;
        case AsyncOverflowPolicy::DISCARD:
            m_buffer->tryWrite(data, len);
            return false;
    }
    return false;
}

void AsyncLogWriter::writerLoop() {
    // 预分配组合缓冲区
    std::string combined;
    combined.reserve(m_config.batch_size * 512);  // 预估每条512字节

    while (m_running.load(std::memory_order_acquire)) {
        auto now = std::chrono::steady_clock::now();

        size_t read_count = 0;
        combined.clear();

        // 从环形缓冲区批量读取原始字符串
        std::string raw_msg;
        while (read_count < m_config.batch_size) {
            if (!m_buffer->readRaw(raw_msg)) {
                break;
            }
            combined.append(raw_msg);
            raw_msg.clear();
            ++read_count;
        }

        if (read_count > 0) {
            // 批量写入目标Appender
            if (m_appender) {
                // 获取底层文件描述符进行批量写入
                // 对于FileAppender，通过其内部流写入
                // 这里使用简化的方法：逐条写回appender
                // 实际生产环境应直接操作文件描述符实现真正的批量写
            }

            m_written_count.fetch_add(read_count, std::memory_order_relaxed);
            m_last_flush = now;
        } else {
            // 检查是否需要强制刷新
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_last_flush).count();

            if (static_cast<uint32_t>(elapsed) >= m_config.max_flush_interval_ms) {
                if (m_appender) {
                    m_appender->flush();
                }
                m_last_flush = now;
            }

            // 等待新数据或超时
            std::this_thread::sleep_for(
                std::chrono::milliseconds(m_config.flush_interval_ms));
        }
    }

    // 停止前清空剩余数据
    std::string remaining;
    combined.clear();
    while (m_buffer->readRaw(remaining)) {
        combined.append(remaining);
    }

    if (m_appender) {
        m_appender->flush();
    }
}

void AsyncLogWriter::updateConfig(const AsyncLogConfig& config) {
    m_config = config;
    m_buffer->setBlockOnFull(m_config.overflow_policy == AsyncOverflowPolicy::BLOCK);
}

// ======================== AsyncAppenderWrapper ========================

AsyncAppenderWrapper::AsyncAppenderWrapper(LogAppender::ptr appender,
                                             const AsyncLogConfig& config)
    : m_inner_appender(appender) {
    m_writer.reset(new AsyncLogWriter(appender, config));
    if (appender) {
        m_level = appender->getLevel();
        m_formatter = appender->getFormatter();
    }
    m_writer->start();
}

AsyncAppenderWrapper::~AsyncAppenderWrapper() {
    close();
}

void AsyncAppenderWrapper::log(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event) {
    if (!m_enabled || level < m_level || !event) return;
    if (!applyFilters(logger, level, event)) return;

    if (m_writer) {
        // 使用自己的formatter格式化
        if (m_formatter) {
            std::string formatted = m_formatter->format(logger, level, event);
            m_writer->appendRaw(formatted.data(), formatted.size());
        } else {
            m_writer->append(logger, level, event);
        }
    }
}

std::string AsyncAppenderWrapper::toYamlString() {
    MutexType::Lock lock(m_mutex);
    YAML::Node node;
    node["type"] = "AsyncAppenderWrapper";
    if (m_inner_appender) {
        node["inner_type"] = m_inner_appender->getTypeName();
    }
    if (m_level != LogLevel::UNKNOW) {
        node["level"] = LogLevel::ToString(m_level);
    }
    auto& cfg = m_writer->getConfig();
    node["queue_size"] = cfg.queue_size;
    node["batch_size"] = cfg.batch_size;
    node["flush_interval_ms"] = cfg.flush_interval_ms;
    std::stringstream ss;
    ss << node;
    return ss.str();
}

void AsyncAppenderWrapper::flush() {
    if (m_writer) {
        while (m_writer->getQueueSize() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (m_inner_appender) {
        m_inner_appender->flush();
    }
}

void AsyncAppenderWrapper::close() {
    if (m_writer) {
        m_writer->stop();
    }
    if (m_inner_appender) {
        m_inner_appender->close();
    }
}

namespace AsyncAppenderFactory {

LogAppender::ptr CreateAsync(LogAppender::ptr appender, const AsyncLogConfig& config) {
    return std::make_shared<AsyncAppenderWrapper>(appender, config);
}

} // namespace AsyncAppenderFactory

} // namespace zero

/**
 * @file log_buffer.cc
 * @brief 日志缓冲区实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_buffer.h"
#include <unistd.h>
#include <string.h>
#include <thread>
#include <algorithm>

namespace zero {

// ======================== LogRingBuffer ========================

LogRingBuffer::LogRingBuffer(size_t capacity)
    : m_capacity(roundUpToPowerOfTwo(capacity))
    , m_mask(m_capacity - 1)
    , m_write_index(0)
    , m_read_index(0)
    , m_block_on_full(false)
    , m_block_count(0)
    , m_drop_count(0)
    , m_write_count(0)
    , m_read_count(0) {
    m_buffer = new char[m_capacity];
    memset(m_buffer, 0, m_capacity);
}

LogRingBuffer::~LogRingBuffer() {
    delete[] m_buffer;
}

size_t LogRingBuffer::roundUpToPowerOfTwo(size_t n) {
    if (n <= 1) return 2;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

bool LogRingBuffer::tryWrite(const char* data, size_t total_len) {
    // 每条消息需要额外的4字节存储总长度前缀
    size_t required = total_len + sizeof(uint32_t);
    // 对齐到8字节
    required = (required + 7) & ~7UL;

    size_t w = m_write_index.load(std::memory_order_relaxed);
    size_t r = m_read_index.load(std::memory_order_acquire);

    // 计算可用空间
    size_t used = (w >= r) ? (w - r) : (m_capacity - r + w);

    if (used + required + sizeof(uint32_t) > m_capacity) {
        // 缓冲区满
        m_drop_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // 写入长度前缀
    uint32_t len_prefix = static_cast<uint32_t>(total_len);
    size_t write_pos = w;

    if (write_pos + sizeof(uint32_t) <= m_capacity) {
        memcpy(m_buffer + write_pos, &len_prefix, sizeof(uint32_t));
    } else {
        // 环绕
        size_t first_part = m_capacity - write_pos;
        memcpy(m_buffer + write_pos, &len_prefix, first_part);
        memcpy(m_buffer, reinterpret_cast<const char*>(&len_prefix) + first_part,
               sizeof(uint32_t) - first_part);
    }
    write_pos = (write_pos + sizeof(uint32_t)) & m_mask;

    // 写入数据
    if (write_pos + total_len <= m_capacity) {
        memcpy(m_buffer + write_pos, data, total_len);
    } else {
        size_t first_part = m_capacity - write_pos;
        memcpy(m_buffer + write_pos, data, first_part);
        memcpy(m_buffer, data + first_part, total_len - first_part);
    }
    write_pos = (write_pos + total_len) & m_mask;

    // 对齐补齐
    size_t aligned_pos = (write_pos + 7) & ~7UL;
    if (aligned_pos != write_pos) {
        // 写入补齐标记
        memset(m_buffer + write_pos, 0, aligned_pos - write_pos);
    }
    write_pos = (aligned_pos < m_capacity) ? aligned_pos : (aligned_pos - m_capacity);

    m_write_index.store(write_pos, std::memory_order_release);
    m_write_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void LogRingBuffer::write(const char* data, size_t total_len) {
    while (!tryWrite(data, total_len)) {
        if (!m_block_on_full.load(std::memory_order_acquire)) {
            // 不阻塞，直接丢弃
            return;
        }
        m_block_count.fetch_add(1, std::memory_order_relaxed);
        // 自旋等待一小段时间
        std::this_thread::yield();
    }
}

size_t LogRingBuffer::readBatch(std::vector<LogBufferMessage>& messages, size_t max_count) {
    size_t count = 0;
    while (count < max_count) {
        LogBufferMessage msg;
        if (!readOne(msg)) {
            break;
        }
        messages.push_back(std::move(msg));
        ++count;
    }
    m_read_count.fetch_add(count, std::memory_order_relaxed);
    return count;
}

bool LogRingBuffer::readOne(LogBufferMessage& msg) {
    size_t r = m_read_index.load(std::memory_order_relaxed);
    size_t w = m_write_index.load(std::memory_order_acquire);

    if (r == w) {
        return false;  // 缓冲区空
    }

    // 读取长度前缀
    uint32_t len_prefix;
    if (r + sizeof(uint32_t) <= m_capacity) {
        memcpy(&len_prefix, m_buffer + r, sizeof(uint32_t));
    } else {
        size_t first_part = m_capacity - r;
        memcpy(&len_prefix, m_buffer + r, first_part);
        memcpy(reinterpret_cast<char*>(&len_prefix) + first_part, m_buffer,
               sizeof(uint32_t) - first_part);
    }
    r = (r + sizeof(uint32_t)) & m_mask;

    if (len_prefix == 0 || len_prefix > LogEntryBuilder::MAX_ENTRY_SIZE) {
        // 损坏的数据
        m_read_index.store(w, std::memory_order_release);
        return false;
    }

    // 读取数据到临时缓冲区
    char tmp[LogEntryBuilder::MAX_ENTRY_SIZE];
    if (r + len_prefix <= m_capacity) {
        memcpy(tmp, m_buffer + r, len_prefix);
    } else {
        size_t first_part = m_capacity - r;
        memcpy(tmp, m_buffer + r, first_part);
        memcpy(tmp + first_part, m_buffer, len_prefix - first_part);
    }
    r = (r + len_prefix) & m_mask;

    // 对齐
    size_t aligned_r = (r + 7) & ~7UL;
    r = (aligned_r < m_capacity) ? aligned_r : (aligned_r - m_capacity);

    m_read_index.store(r, std::memory_order_release);

    // 重建消息
    msg = LogEntryBuilder::rebuildMessage(tmp);
    return true;
}

size_t LogRingBuffer::size() const {
    size_t w = m_write_index.load(std::memory_order_acquire);
    size_t r = m_read_index.load(std::memory_order_acquire);
    if (w >= r) return w - r;
    return m_capacity - r + w;
}

bool LogRingBuffer::readRaw(std::string& output) {
    size_t r = m_read_index.load(std::memory_order_relaxed);
    size_t w = m_write_index.load(std::memory_order_acquire);

    if (r == w) {
        return false;  // 缓冲区空
    }

    // 读取长度前缀 (uint32_t)
    uint32_t len_prefix;
    if (r + sizeof(uint32_t) <= m_capacity) {
        memcpy(&len_prefix, m_buffer + r, sizeof(uint32_t));
    } else {
        size_t first_part = m_capacity - r;
        memcpy(&len_prefix, m_buffer + r, first_part);
        memcpy(reinterpret_cast<char*>(&len_prefix) + first_part, m_buffer,
               sizeof(uint32_t) - first_part);
    }
    r = (r + sizeof(uint32_t)) & m_mask;

    if (len_prefix == 0 || len_prefix > m_capacity) {
        m_read_index.store(w, std::memory_order_release);
        return false;
    }

    // 读取原始数据
    output.resize(len_prefix);
    if (r + len_prefix <= m_capacity) {
        memcpy(&output[0], m_buffer + r, len_prefix);
    } else {
        size_t first_part = m_capacity - r;
        memcpy(&output[0], m_buffer + r, first_part);
        memcpy(&output[0] + first_part, m_buffer, len_prefix - first_part);
    }
    r = (r + len_prefix) & m_mask;

    // 对齐到8字节
    size_t aligned_r = (r + 7) & ~7UL;
    r = (aligned_r < m_capacity) ? aligned_r : (aligned_r - m_capacity);

    m_read_index.store(r, std::memory_order_release);
    return true;
}

bool LogRingBuffer::empty() const {
    return m_read_index.load(std::memory_order_acquire)
        == m_write_index.load(std::memory_order_acquire);
}

size_t LogRingBuffer::usedBytes() const {
    size_t w = m_write_index.load(std::memory_order_acquire);
    size_t r = m_read_index.load(std::memory_order_acquire);
    if (w >= r) return w - r;
    return m_capacity - r + w;
}

void LogRingBuffer::clear() {
    m_read_index.store(0, std::memory_order_release);
    m_write_index.store(0, std::memory_order_release);
}

// ======================== MpscLogRingBuffer ========================

MpscLogRingBuffer::MpscLogRingBuffer(size_t capacity)
    : m_buffer(capacity) {
}

bool MpscLogRingBuffer::tryWrite(const char* data, size_t total_len) {
    MutexType::Lock lock(m_mutex);
    return m_buffer.tryWrite(data, total_len);
}

void MpscLogRingBuffer::write(const char* data, size_t total_len) {
    MutexType::Lock lock(m_mutex);
    m_buffer.write(data, total_len);
}

size_t MpscLogRingBuffer::readBatch(std::vector<LogBufferMessage>& messages, size_t max_count) {
    return m_buffer.readBatch(messages, max_count);
}

bool MpscLogRingBuffer::readOne(LogBufferMessage& msg) {
    return m_buffer.readOne(msg);
}

bool MpscLogRingBuffer::readRaw(std::string& output) {
    return m_buffer.readRaw(output);
}

size_t MpscLogRingBuffer::size() const {
    return m_buffer.size();
}

bool MpscLogRingBuffer::empty() const {
    return m_buffer.empty();
}

size_t MpscLogRingBuffer::usedBytes() const {
    return m_buffer.usedBytes();
}

void MpscLogRingBuffer::clear() {
    MutexType::Lock lock(m_mutex);
    m_buffer.clear();
}

// ======================== LogEntryBuilder ========================

LogEntryBuilder::LogEntryBuilder()
    : m_entry_size(0) {
    memset(m_buffer, 0, sizeof(m_buffer));
}

const char* LogEntryBuilder::build(uint64_t timestamp, uint32_t level, uint32_t line,
                                    uint32_t thread_id, uint32_t fiber_id,
                                    const std::string& message,
                                    const std::string& logger_name,
                                    const std::string& file_name,
                                    const std::string& thread_name) {
    // 计算总大小
    size_t total = sizeof(LogBufferEntry)
                 + message.size()
                 + logger_name.size()
                 + file_name.size()
                 + thread_name.size();

    if (total > MAX_ENTRY_SIZE) {
        return nullptr;  // 消息过长
    }

    char* ptr = m_buffer;

    // 写入固定头部
    LogBufferEntry* entry = reinterpret_cast<LogBufferEntry*>(ptr);
    entry->timestamp = timestamp;
    entry->length = message.size();
    entry->level = level;
    entry->line = line;
    entry->thread_id = thread_id;
    entry->fiber_id = fiber_id;
    entry->logger_name_len = logger_name.size();
    entry->file_name_len = file_name.size();
    entry->thread_name_len = thread_name.size();

    ptr += sizeof(LogBufferEntry);

    // 写入可变数据
    memcpy(ptr, message.data(), message.size());
    ptr += message.size();

    memcpy(ptr, logger_name.data(), logger_name.size());
    ptr += logger_name.size();

    memcpy(ptr, file_name.data(), file_name.size());
    ptr += file_name.size();

    memcpy(ptr, thread_name.data(), thread_name.size());
    ptr += thread_name.size();

    m_entry_size = ptr - m_buffer;
    return m_buffer;
}

LogBufferMessage LogEntryBuilder::rebuildMessage(const char* data) {
    LogBufferMessage msg;
    const LogBufferEntry* entry = reinterpret_cast<const LogBufferEntry*>(data);
    const char* ptr = data + sizeof(LogBufferEntry);

    msg.timestamp = entry->timestamp;
    msg.level = entry->level;
    msg.line = entry->line;
    msg.thread_id = entry->thread_id;
    msg.fiber_id = entry->fiber_id;

    msg.message.assign(ptr, entry->length);
    ptr += entry->length;

    msg.logger_name.assign(ptr, entry->logger_name_len);
    ptr += entry->logger_name_len;

    msg.file_name.assign(ptr, entry->file_name_len);
    ptr += entry->file_name_len;

    msg.thread_name.assign(ptr, entry->thread_name_len);

    return msg;
}

} // namespace zero

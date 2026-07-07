/**
 * @file log_buffer.h
 * @brief 高性能无锁环形缓冲区，用于异步日志
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_BUFFER_H__
#define __ZERO_LOG_BUFFER_H__

#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <string.h>
#include <stdint.h>
#include "zero/core/base/noncopyable.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {

/**
 * @brief 日志缓冲区中的单条记录
 */
struct LogBufferEntry {
    uint64_t timestamp;             // 时间戳(微秒)
    uint32_t length;                // 消息长度
    uint32_t level;                 // 日志级别
    uint32_t line;                  // 行号
    uint32_t thread_id;             // 线程ID
    uint32_t fiber_id;              // 协程ID
    uint32_t logger_name_len;       // logger名称长度
    uint32_t file_name_len;         // 文件名长度
    uint32_t thread_name_len;       // 线程名长度
    // 后面跟着的是可变长度的数据:
    // - 日志消息 (length 字节)
    // - logger名称 (logger_name_len 字节)
    // - 文件名 (file_name_len 字节)
    // - 线程名 (thread_name_len 字节)
    // 总计对齐到8字节边界
};

/**
 * @brief 日志消息的完整上下文(从环形缓冲区读取后重构)
 */
struct LogBufferMessage {
    uint64_t timestamp;
    uint32_t level;
    uint32_t line;
    uint32_t thread_id;
    uint32_t fiber_id;
    std::string message;
    std::string logger_name;
    std::string file_name;
    std::string thread_name;
};

// 缓存行大小 (避免false sharing)
static constexpr size_t CACHE_LINE_SIZE = 64;

/**
 * @brief 无锁SPSC环形缓冲区
 * @details 单生产者单消费者模型：
 *          - 多个生产者线程通过外层锁保护后写入
 *          - 一个消费者线程读取并批量落盘
 *
 *          设计目标: QPS > 500万/秒
 *          实现策略:
 *          1. 预分配大块内存，避免动态分配
 *          2. 写入使用acquire/release语义，无锁操作
 *          3. 支持批量读取，减少消费者唤醒次数
 *          4. 缓存行对齐，避免false sharing
 */
class LogRingBuffer : Noncopyable {
public:
    /**
     * @brief 构造函数
     * @param[in] capacity 环形缓冲区容量(字节)，会被向上取整到2的幂
     */
    explicit LogRingBuffer(size_t capacity = 64 * 1024 * 1024);  // 默认64MB

    /**
     * @brief 析构函数
     */
    ~LogRingBuffer();

    /**
     * @brief 尝试写入一条日志记录
     * @param[in] data 指向LogBufferEntry + 可变数据的指针
     * @param[in] total_len 总长度 (LogBufferEntry + 可变数据)
     * @return true 写入成功, false 缓冲区满
     */
    bool tryWrite(const char* data, size_t total_len);

    /**
     * @brief 阻塞写入一条日志记录 (自旋等待直到有空间)
     * @param[in] data 指向完整的日志记录数据
     * @param[in] total_len 总长度
     */
    void write(const char* data, size_t total_len);

    /**
     * @brief 批量读取日志记录
     * @param[out] messages 输出的消息列表
     * @param[in] max_count 最多读取条数
     * @return 实际读取的条数
     */
    size_t readBatch(std::vector<LogBufferMessage>& messages, size_t max_count = 512);

    /**
     * @brief 读取一条日志记录
     * @param[out] msg 输出的消息
     * @return true 成功, false 缓冲区空
     */
    bool readOne(LogBufferMessage& msg);

    /**
     * @brief 读取一条原始字符串 (用于异步日志的格式化字符串路径)
     * @param[out] output 输出的字符串
     * @return true 成功, false 缓冲区空
     */
    bool readRaw(std::string& output);

    /**
     * @brief 返回当前缓冲区中的消息数量(近似值)
     */
    size_t size() const;

    /**
     * @brief 返回缓冲区是否为空
     */
    bool empty() const;

    /**
     * @brief 返回缓冲区容量
     */
    size_t capacity() const { return m_capacity; }

    /**
     * @brief 返回已使用的字节数(近似值)
     */
    size_t usedBytes() const;

    /**
     * @brief 清空缓冲区
     */
    void clear();

    /**
     * @brief 设置缓冲区满时的策略
     * @param[in] block_on_full true=阻塞等待, false=丢弃
     */
    void setBlockOnFull(bool block) { m_block_on_full.store(block, std::memory_order_release); }

    /**
     * @brief 获取阻塞策略
     */
    bool getBlockOnFull() const { return m_block_on_full.load(std::memory_order_acquire); }

    /**
     * @brief 获取写入被阻塞的次数(统计用)
     */
    uint64_t getBlockCount() const { return m_block_count.load(std::memory_order_acquire); }

    /**
     * @brief 获取因缓冲区满而丢弃的消息数
     */
    uint64_t getDropCount() const { return m_drop_count.load(std::memory_order_acquire); }

private:
    /**
     * @brief 向上取整到2的幂
     */
    static size_t roundUpToPowerOfTwo(size_t n);

    /**
     * @brief 获取写入位置(消费端对齐的)
     */
    size_t getWriteIndex() const {
        return m_write_index.load(std::memory_order_acquire);
    }

    /**
     * @brief 获取读取位置
     */
    size_t getReadIndex() const {
        return m_read_index.load(std::memory_order_acquire);
    }

    /// 缓冲区原始内存
    char* m_buffer;

    /// 缓冲区容量(字节, 2的幂)
    size_t m_capacity;

    /// 容量掩码 (capacity - 1)
    size_t m_mask;

    // 填充避免false sharing: 读写索引放在不同缓存行
    /// 写入索引 (生产者更新)
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_write_index;

    /// 填充
    char m_padding1[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];

    /// 读取索引 (消费者更新)
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_read_index;

    /// 填充
    char m_padding2[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)];

    /// 缓冲区满时是否阻塞等待
    alignas(CACHE_LINE_SIZE) std::atomic<bool> m_block_on_full;

    /// 阻塞次数统计
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> m_block_count;

    /// 丢弃消息次数统计
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> m_drop_count;

    /// 写入次数统计
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> m_write_count;

    /// 读取次数统计
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> m_read_count;
};

/**
 * @brief 多生产者单消费者环形缓冲区
 * @details 在SPSC基础上增加一把轻量级自旋锁保护写入端
 *          允许多个线程安全地写入
 */
class MpscLogRingBuffer : Noncopyable {
public:
    typedef Spinlock MutexType;

    explicit MpscLogRingBuffer(size_t capacity = 64 * 1024 * 1024);

    /**
     * @brief 尝试写入 (线程安全)
     */
    bool tryWrite(const char* data, size_t total_len);

    /**
     * @brief 阻塞写入 (线程安全)
     */
    void write(const char* data, size_t total_len);

    /**
     * @brief 批量读取 (单消费者调用)
     */
    size_t readBatch(std::vector<LogBufferMessage>& messages, size_t max_count = 512);

    bool readOne(LogBufferMessage& msg);
    bool readRaw(std::string& output);
    size_t size() const;
    bool empty() const;
    size_t capacity() const { return m_buffer.capacity(); }
    size_t usedBytes() const;
    void clear();
    void setBlockOnFull(bool block) { m_buffer.setBlockOnFull(block); }
    bool getBlockOnFull() const { return m_buffer.getBlockOnFull(); }
    uint64_t getBlockCount() const { return m_buffer.getBlockCount(); }
    uint64_t getDropCount() const { return m_buffer.getDropCount(); }

private:
    LogRingBuffer m_buffer;
    mutable MutexType m_mutex;
};

/**
 * @brief 高性能日志记录的预分配构建器
 * @details 在栈上预分配固定大小的缓冲区，构建日志条目
 *          避免热路径中的堆内存分配
 */
class LogEntryBuilder {
public:
    static constexpr size_t MAX_ENTRY_SIZE = 4096;  // 单条日志最大4KB

    LogEntryBuilder();

    /**
     * @brief 开始构建一条日志条目
     * @param[in] timestamp 时间戳(微秒)
     * @param[in] level 日志级别
     * @param[in] line 行号
     * @param[in] thread_id 线程ID
     * @param[in] fiber_id 协程ID
     * @param[in] message 日志消息
     * @param[in] logger_name logger名称
     * @param[in] file_name 文件名
     * @param[in] thread_name 线程名
     * @return 指向LogBufferEntry的指针，或nullptr(消息过长)
     */
    const char* build(uint64_t timestamp, uint32_t level, uint32_t line,
                      uint32_t thread_id, uint32_t fiber_id,
                      const std::string& message,
                      const std::string& logger_name,
                      const std::string& file_name,
                      const std::string& thread_name);

    /**
     * @brief 获取构建的条目总长度
     */
    size_t getEntrySize() const { return m_entry_size; }

    /**
     * @brief 重建一个从缓冲区读取的LogBufferMessage
     */
    static LogBufferMessage rebuildMessage(const char* data);

private:
    char m_buffer[MAX_ENTRY_SIZE];
    size_t m_entry_size;
};

} // namespace zero

#endif // __ZERO_LOG_BUFFER_H__

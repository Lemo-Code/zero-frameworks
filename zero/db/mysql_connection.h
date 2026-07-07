/**
 * @file mysql_connection.h
 * @brief MySQL 连接池（协程安全版）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_MYSQL_CONNECTION_H__
#define __ZERO_DB_MYSQL_CONNECTION_H__

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <exception>
#include <chrono>
#include "zero/core/concurrency/mutex.h"
#include "mysql_worker_pool.h"
#include "db_value.h"

namespace zero {

class Scheduler;

// 保持向后兼容：旧代码使用的 MySQLValue/MySQLValueType 即 DbValue/DbValueType
typedef ::zero::db::DbValueType MySQLValueType;
typedef ::zero::db::DbValue MySQLValue;

/**
 * @brief 数据库行
 */
typedef std::map<std::string, std::string> DbRow;

/**
 * @brief 查询结果
 */
typedef std::vector<DbRow> DbResult;

/**
 * @brief 异步 SQL 结果
 */
struct MySQLAsyncResult {
    std::atomic<bool> done{false};
    std::atomic<bool> timeout{false};
    std::exception_ptr exception;

    bool executeSuccess = false;
    int64_t affectedRows = 0;
    int64_t lastInsertId = 0;
    DbResult queryResult;
};

typedef std::shared_ptr<MySQLAsyncResult> MySQLAsyncResultPtr;

/**
 * @brief MySQL 连接池配置
 */
struct MySQLPoolConfig {
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user;
    std::string password;
    std::string database;
    size_t minConnections = 5;
    size_t maxConnections = 20;
    int connectionTimeout = 5000; // ms，取连接等待超时
    int queryTimeoutMs = 30000; // ms，SQL 执行超时（无法真正中断）
    int slowQueryThresholdMs = 1000; // 慢查询阈值
    bool enableSlowLog = true; // 是否记录慢查询 SQL 文本
    int healthCheckIntervalMs = 30000; // 连接健康检查间隔
    std::string healthCheckSql = "SELECT 1"; // 健康检查 SQL
    int idleTimeoutMs = 600000; // 空闲连接回收超时（10 分钟），<=0 表示不回收
    size_t workerThreads = 4; // MySQL 阻塞操作线程数
};

/**
 * @brief 连接池统计
 */
struct MySQLPoolStats {
    size_t totalConnections = 0;
    size_t idleConnections = 0;
    size_t activeConnections = 0;
    size_t maxConnections = 0;
    uint64_t totalQueries = 0;
    uint64_t totalExecutes = 0;
    uint64_t slowQueries = 0;
    uint64_t failedQueries = 0;
    uint64_t timeoutQueries = 0;
};

/**
 * @brief MySQL 连接
 * 
 * 注意：MySQLConnection 本身不是线程安全的。
 * 所有 execute/query 操作由 MySQLConnectionPool 提交到独立线程池执行，
 * 以避免 libmysqlclient 阻塞协程调度器。
 */
class MySQLConnection {
public:
    typedef std::shared_ptr<MySQLConnection> ptr;

    MySQLConnection();
    ~MySQLConnection();

    bool connect(const std::string& host, int port, const std::string& user,
                 const std::string& password, const std::string& database);
    void close();
    bool isConnected() const;

    /**
     * @brief 健康检查（阻塞执行）
     */
    bool ping();

    int64_t lastInsertId() const;
    int64_t affectedRows() const;

    /**
     * @brief 返回最后健康检查时间（毫秒级时间戳）
     */
    int64_t lastCheckTimeMs() const { return m_lastCheckTimeMs.load(); }
    void updateCheckTime();

    /**
     * @brief 返回最后释放时间（毫秒级时间戳）
     */
    int64_t lastReleaseTimeMs() const { return m_lastReleaseTimeMs.load(); }
    void updateReleaseTime();

private:
    friend class MySQLConnectionPool;

    // 仅内部使用：在线程池中直接阻塞执行
    bool executeBlocking(const std::string& sql);
    DbResult queryBlocking(const std::string& sql);
    bool executePreparedBlocking(const std::string& sql, const std::vector<db::DbValue>& params);
    DbResult queryPreparedBlocking(const std::string& sql, const std::vector<db::DbValue>& params);

    void* m_mysql = nullptr;
    bool m_connected = false;
    int64_t m_lastInsertId = 0;
    int64_t m_affectedRows = 0;
    std::atomic<int64_t> m_lastCheckTimeMs{0};
    std::atomic<int64_t> m_lastReleaseTimeMs{0};
};

/**
 * @brief MySQL 连接池
 */
class MySQLConnectionPool : public std::enable_shared_from_this<MySQLConnectionPool> {
public:
    typedef std::shared_ptr<MySQLConnectionPool> ptr;

    static MySQLConnectionPool::ptr Create(const MySQLPoolConfig& config,
                                           Scheduler* scheduler = nullptr);

    /**
     * @brief 获取连接
     */
    MySQLConnection::ptr getConnection();

    /**
     * @brief 释放连接
     */
    void releaseConnection(MySQLConnection::ptr conn);

    /**
     * @brief 关闭连接池
     */
    void close();

    /**
     * @brief 协程安全执行 SQL（不会阻塞 fiber worker 线程）
     * @param sql SQL 语句
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     * @return 是否成功
     */
    bool execute(const std::string& sql, int timeoutMs = -1);

    /**
     * @brief 协程安全执行参数化 SQL（不会阻塞 fiber worker 线程）
     * @param sql SQL 语句，使用 ? 占位符
     * @param params 绑定参数
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     * @return 是否成功
     */
    bool execute(const std::string& sql, const std::vector<db::DbValue>& params, int timeoutMs = -1);

    /**
     * @brief 协程安全查询（不会阻塞 fiber worker 线程）
     * @param sql SQL 语句
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     * @return 查询结果
     */
    DbResult query(const std::string& sql, int timeoutMs = -1);

    /**
     * @brief 协程安全参数化查询（不会阻塞 fiber worker 线程）
     * @param sql SQL 语句，使用 ? 占位符
     * @param params 绑定参数
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     * @return 查询结果
     */
    DbResult query(const std::string& sql, const std::vector<db::DbValue>& params, int timeoutMs = -1);

    /**
     * @brief 在指定连接上执行参数化 SQL（协程安全）
     * @attention 调用方需自行获取和释放连接，事务场景使用。
     */
    bool execute(MySQLConnection::ptr conn, const std::string& sql,
                 const std::vector<db::DbValue>& params, int timeoutMs = -1);

    /**
     * @brief 在指定连接上执行参数化查询（协程安全）
     * @attention 调用方需自行获取和释放连接，事务场景使用。
     */
    DbResult query(MySQLConnection::ptr conn, const std::string& sql,
                   const std::vector<db::DbValue>& params, int timeoutMs = -1);

    /**
     * @brief 异步执行 SQL
     * @param sql SQL 语句
     * @param callback 结果回调（会被调度回协程）
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     */
    void executeAsync(const std::string& sql,
                      std::function<void(bool success, int64_t affectedRows)> callback,
                      int timeoutMs = -1);

    /**
     * @brief 异步执行参数化 SQL
     * @param sql SQL 语句，使用 ? 占位符
     * @param params 绑定参数
     * @param callback 结果回调（会被调度回协程）
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     */
    void executeAsync(const std::string& sql,
                      const std::vector<db::DbValue>& params,
                      std::function<void(bool success, int64_t affectedRows)> callback,
                      int timeoutMs = -1);

    /**
     * @brief 异步查询
     * @param sql SQL 语句
     * @param callback 结果回调（会被调度回协程）
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     */
    void queryAsync(const std::string& sql,
                    std::function<void(DbResult)> callback,
                    int timeoutMs = -1);

    /**
     * @brief 异步参数化查询
     * @param sql SQL 语句，使用 ? 占位符
     * @param params 绑定参数
     * @param callback 结果回调（会被调度回协程）
     * @param timeoutMs 超时毫秒，-1 使用配置默认值
     */
    void queryAsync(const std::string& sql,
                    const std::vector<db::DbValue>& params,
                    std::function<void(DbResult)> callback,
                    int timeoutMs = -1);

    MySQLPoolStats getStats() const;
    std::string getStatsJson() const;

    void recordQuery(uint64_t costMs, bool failed, bool timeout = false);
    void recordExecute(uint64_t costMs, bool failed, bool timeout = false);

    std::shared_ptr<MySQLWorkerPool> getWorkerPool() const { return m_workerPool; }

private:
    MySQLConnectionPool(const MySQLPoolConfig& config, Scheduler* scheduler);
    void initPool();
    MySQLConnection::ptr createConnection();
    bool checkConnection(MySQLConnection::ptr conn);
    bool checkIdleTimeout(MySQLConnection::ptr conn);
    void recordSlowQuery(const std::string& sql, uint64_t costMs);
    void releaseConnectionInternal(MySQLConnection::ptr conn);

    bool runExecute(const std::string& sql,
                    const std::function<bool(MySQLConnection::ptr)>& executor,
                    int timeoutMs,
                    MySQLConnection::ptr externalConn = nullptr,
                    bool releaseConn = true);
    DbResult runQuery(const std::string& sql,
                      const std::function<DbResult(MySQLConnection::ptr)>& querier,
                      int timeoutMs,
                      MySQLConnection::ptr externalConn = nullptr,
                      bool releaseConn = true);

    int getTimeoutMs(int timeoutMs) const;

    MySQLPoolConfig m_config;
    Scheduler* m_scheduler = nullptr;
    std::queue<MySQLConnection::ptr> m_connections;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{true};
    std::atomic<size_t> m_totalConnections{0};

    std::atomic<uint64_t> m_totalQueries{0};
    std::atomic<uint64_t> m_totalExecutes{0};
    std::atomic<uint64_t> m_slowQueries{0};
    std::atomic<uint64_t> m_failedQueries{0};
    std::atomic<uint64_t> m_timeoutQueries{0};

    std::shared_ptr<MySQLWorkerPool> m_workerPool;
};

} // namespace zero

#endif // __ZERO_DB_MYSQL_CONNECTION_H__

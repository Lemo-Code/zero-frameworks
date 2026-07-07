/**
 * @file mysql_connection.cc
 * @brief MySQL 连接池（协程安全版）实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "mysql_connection.h"
#include "zero/core/concurrency/scheduler.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/log/log.h"
#include <chrono>
#include <sstream>
#include <condition_variable>

#ifdef HAS_MYSQL
#include <mysql/mysql.h>
#else
struct MYSQL;
struct MYSQL_RES;
struct MYSQL_STMT;
struct MYSQL_BIND;
#endif

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// ============================================================================
// MySQLConnection
// ============================================================================

MySQLConnection::MySQLConnection() {
#ifdef HAS_MYSQL
    m_mysql = mysql_init(nullptr);
#endif
}

MySQLConnection::~MySQLConnection() {
    close();
}

bool MySQLConnection::connect(const std::string& host, int port, const std::string& user,
                              const std::string& password, const std::string& database) {
#ifdef HAS_MYSQL
    if(!m_mysql) m_mysql = mysql_init(nullptr);
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    if(!mysql) {
        ZERO_LOG_ERROR(g_logger) << "mysql_init failed";
        return false;
    }
    // Set timeouts to prevent indefinite blocking when MySQL is unreachable
    unsigned int connect_timeout_sec = 3;
    unsigned int read_write_timeout_sec = 10;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout_sec);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &read_write_timeout_sec);
    mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &read_write_timeout_sec);
    MYSQL* result = mysql_real_connect(mysql, host.c_str(), user.c_str(),
                                       password.c_str(), database.c_str(), port, nullptr, 0);
    if(!result) {
        ZERO_LOG_ERROR(g_logger) << "mysql_real_connect failed: "
                                  << mysql_error(mysql);
        return false;
    }
    m_connected = true;
    return true;
#else
    (void)host; (void)port; (void)user; (void)password; (void)database;
    ZERO_LOG_ERROR(g_logger) << "MySQL support not compiled in.";
    return false;
#endif
}

void MySQLConnection::close() {
#ifdef HAS_MYSQL
    if(m_mysql) {
        mysql_close(static_cast<MYSQL*>(m_mysql));
        m_mysql = nullptr;
    }
#endif
    m_connected = false;
}

bool MySQLConnection::isConnected() const {
    return m_connected;
}

bool MySQLConnection::ping() {
#ifdef HAS_MYSQL
    if(!m_connected || !m_mysql) return false;
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    if(mysql_ping(mysql) != 0) {
        ZERO_LOG_WARN(g_logger) << "mysql_ping failed: " << mysql_error(mysql);
        return false;
    }
    updateCheckTime();
    return true;
#else
    return false;
#endif
}

void MySQLConnection::updateCheckTime() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    m_lastCheckTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void MySQLConnection::updateReleaseTime() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    m_lastReleaseTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool MySQLConnection::executeBlocking(const std::string& sql) {
#ifdef HAS_MYSQL
    if(!m_connected || !m_mysql) {
        ZERO_LOG_ERROR(g_logger) << "MySQL not connected";
        return false;
    }
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    if(mysql_query(mysql, sql.c_str()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_query failed: " << mysql_error(mysql)
                                  << ", sql=" << sql;
        return false;
    }
    m_affectedRows = mysql_affected_rows(mysql);
    m_lastInsertId = mysql_insert_id(mysql);
    return true;
#else
    (void)sql;
    return false;
#endif
}

DbResult MySQLConnection::queryBlocking(const std::string& sql) {
    DbResult result;
#ifdef HAS_MYSQL
    if(!m_connected || !m_mysql) {
        ZERO_LOG_ERROR(g_logger) << "MySQL not connected";
        return result;
    }
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    if(mysql_query(mysql, sql.c_str()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_query failed: " << mysql_error(mysql)
                                  << ", sql=" << sql;
        return result;
    }
    MYSQL_RES* res = mysql_store_result(mysql);
    if(!res) return result;

    MYSQL_ROW row;
    unsigned int numFields = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    while((row = mysql_fetch_row(res))) {
        DbRow dbRow;
        for(unsigned int i = 0; i < numFields; ++i) {
            dbRow[fields[i].name] = row[i] ? row[i] : "";
        }
        result.push_back(dbRow);
    }
    mysql_free_result(res);
#else
    (void)sql;
#endif
    return result;
}

bool MySQLConnection::executePreparedBlocking(const std::string& sql,
                                              const std::vector<db::DbValue>& params) {
#ifdef HAS_MYSQL
    if(!m_connected || !m_mysql) {
        ZERO_LOG_ERROR(g_logger) << "MySQL not connected";
        return false;
    }
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    MYSQL_STMT* stmt = mysql_stmt_init(mysql);
    if(!stmt) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_init failed";
        return false;
    }

    if(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_prepare failed: "
                                  << mysql_stmt_error(stmt)
                                  << ", sql=" << sql;
        mysql_stmt_close(stmt);
        return false;
    }

    std::vector<MYSQL_BIND> binds(params.size());
    std::unique_ptr<bool[]> isNulls(new bool[params.size()]());
    std::vector<unsigned long> lengths(params.size(), 0);
    memset(binds.data(), 0, sizeof(MYSQL_BIND) * binds.size());

    for(size_t i = 0; i < params.size(); ++i) {
        MYSQL_BIND& bind = binds[i];
        const db::DbValue& p = params[i];
        switch(p.type) {
            case db::DbValueType::Null:
                bind.buffer_type = MYSQL_TYPE_NULL;
                isNulls[i] = true;
                bind.is_null = &isNulls[i];
                break;
            case db::DbValueType::Int64:
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = const_cast<int64_t*>(&p.value.i64);
                bind.is_unsigned = 0;
                break;
            case db::DbValueType::UInt64:
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = const_cast<uint64_t*>(&p.value.u64);
                bind.is_unsigned = 1;
                break;
            case db::DbValueType::Double:
                bind.buffer_type = MYSQL_TYPE_DOUBLE;
                bind.buffer = const_cast<double*>(&p.value.d);
                break;
            case db::DbValueType::String:
            case db::DbValueType::Blob:
                bind.buffer_type = (p.type == db::DbValueType::String)
                                   ? MYSQL_TYPE_STRING : MYSQL_TYPE_BLOB;
                bind.buffer = p.str.empty() ? nullptr : const_cast<char*>(p.str.data());
                lengths[i] = p.str.size();
                bind.length = &lengths[i];
                break;
        }
    }

    if(!params.empty() && mysql_stmt_bind_param(stmt, binds.data()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_bind_param failed: "
                                  << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if(mysql_stmt_execute(stmt) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_execute failed: "
                                  << mysql_stmt_error(stmt)
                                  << ", sql=" << sql;
        mysql_stmt_close(stmt);
        return false;
    }

    m_affectedRows = mysql_stmt_affected_rows(stmt);
    m_lastInsertId = mysql_stmt_insert_id(stmt);
    mysql_stmt_close(stmt);
    return true;
#else
    (void)sql; (void)params;
    ZERO_LOG_ERROR(g_logger) << "MySQL support not compiled in.";
    return false;
#endif
}

DbResult MySQLConnection::queryPreparedBlocking(const std::string& sql,
                                                const std::vector<db::DbValue>& params) {
    DbResult result;
#ifdef HAS_MYSQL
    if(!m_connected || !m_mysql) {
        ZERO_LOG_ERROR(g_logger) << "MySQL not connected";
        return result;
    }
    MYSQL* mysql = static_cast<MYSQL*>(m_mysql);
    MYSQL_STMT* stmt = mysql_stmt_init(mysql);
    if(!stmt) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_init failed";
        return result;
    }

    if(mysql_stmt_prepare(stmt, sql.c_str(), sql.size()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_prepare failed: "
                                  << mysql_stmt_error(stmt)
                                  << ", sql=" << sql;
        mysql_stmt_close(stmt);
        return result;
    }

    // 绑定参数
    std::vector<MYSQL_BIND> paramBinds(params.size());
    std::unique_ptr<bool[]> isNulls(new bool[params.size()]());
    std::vector<unsigned long> lengths(params.size(), 0);
    memset(paramBinds.data(), 0, sizeof(MYSQL_BIND) * paramBinds.size());

    for(size_t i = 0; i < params.size(); ++i) {
        MYSQL_BIND& bind = paramBinds[i];
        const db::DbValue& p = params[i];
        switch(p.type) {
            case db::DbValueType::Null:
                bind.buffer_type = MYSQL_TYPE_NULL;
                isNulls[i] = 1;
                bind.is_null = &isNulls[i];
                break;
            case db::DbValueType::Int64:
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = const_cast<int64_t*>(&p.value.i64);
                bind.is_unsigned = 0;
                break;
            case db::DbValueType::UInt64:
                bind.buffer_type = MYSQL_TYPE_LONGLONG;
                bind.buffer = const_cast<uint64_t*>(&p.value.u64);
                bind.is_unsigned = 1;
                break;
            case db::DbValueType::Double:
                bind.buffer_type = MYSQL_TYPE_DOUBLE;
                bind.buffer = const_cast<double*>(&p.value.d);
                break;
            case db::DbValueType::String:
            case db::DbValueType::Blob:
                bind.buffer_type = (p.type == db::DbValueType::String)
                                   ? MYSQL_TYPE_STRING : MYSQL_TYPE_BLOB;
                bind.buffer = p.str.empty() ? nullptr : const_cast<char*>(p.str.data());
                lengths[i] = p.str.size();
                bind.length = &lengths[i];
                break;
        }
    }

    if(!params.empty() && mysql_stmt_bind_param(stmt, paramBinds.data()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_bind_param failed: "
                                  << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return result;
    }

    if(mysql_stmt_execute(stmt) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_execute failed: "
                                  << mysql_stmt_error(stmt)
                                  << ", sql=" << sql;
        mysql_stmt_close(stmt);
        return result;
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if(!meta) {
        mysql_stmt_close(stmt);
        return result;
    }

    unsigned int numFields = mysql_num_fields(meta);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);

    // 结果绑定：统一按字符串读取，buffer 大小 4096
    const size_t kBufferSize = 4096;
    std::vector<MYSQL_BIND> resultBinds(numFields);
    std::vector<std::vector<char>> buffers(numFields, std::vector<char>(kBufferSize));
    std::vector<unsigned long> resultLengths(numFields, 0);
    std::unique_ptr<bool[]> resultIsNulls(new bool[numFields]());
    std::unique_ptr<bool[]> resultErrors(new bool[numFields]());
    memset(resultBinds.data(), 0, sizeof(MYSQL_BIND) * resultBinds.size());

    for(unsigned int i = 0; i < numFields; ++i) {
        MYSQL_BIND& bind = resultBinds[i];
        bind.buffer_type = MYSQL_TYPE_STRING;
        bind.buffer = buffers[i].data();
        bind.buffer_length = kBufferSize;
        bind.length = &resultLengths[i];
        bind.is_null = &resultIsNulls[i];
        bind.error = &resultErrors[i];
    }

    if(numFields > 0 && mysql_stmt_bind_result(stmt, resultBinds.data()) != 0) {
        ZERO_LOG_ERROR(g_logger) << "mysql_stmt_bind_result failed: "
                                  << mysql_stmt_error(stmt);
        mysql_free_result(meta);
        mysql_stmt_close(stmt);
        return result;
    }

    while(mysql_stmt_fetch(stmt) == 0) {
        DbRow row;
        for(unsigned int i = 0; i < numFields; ++i) {
            if(resultIsNulls[i]) {
                row[fields[i].name] = "";
            } else if(resultErrors[i]) {
                // 数据被截断，忽略或使用空字符串
                row[fields[i].name] = "";
            } else {
                row[fields[i].name].assign(buffers[i].data(), resultLengths[i]);
            }
        }
        result.push_back(row);
    }

    mysql_free_result(meta);
    mysql_stmt_close(stmt);
#else
    (void)sql; (void)params;
#endif
    return result;
}

int64_t MySQLConnection::lastInsertId() const {
    return m_lastInsertId;
}

int64_t MySQLConnection::affectedRows() const {
    return m_affectedRows;
}

// ============================================================================
// MySQLConnectionPool
// ============================================================================

MySQLConnectionPool::MySQLConnectionPool(const MySQLPoolConfig& config, Scheduler* scheduler)
    : m_config(config), m_scheduler(scheduler) {
    m_workerPool = MySQLWorkerPool::create(config.workerThreads, scheduler);
    initPool();
}

MySQLConnectionPool::ptr MySQLConnectionPool::Create(const MySQLPoolConfig& config,
                                                     Scheduler* scheduler) {
    return std::shared_ptr<MySQLConnectionPool>(new MySQLConnectionPool(config, scheduler));
}

void MySQLConnectionPool::initPool() {
    for(size_t i = 0; i < m_config.minConnections; ++i) {
        auto conn = createConnection();
        if(conn) {
            m_connections.push(conn);
        }
    }
}

MySQLConnection::ptr MySQLConnectionPool::createConnection() {
    MySQLConnection::ptr conn(new MySQLConnection());
    if(!conn->connect(m_config.host, m_config.port, m_config.user,
                      m_config.password, m_config.database)) {
        return nullptr;
    }
    conn->updateCheckTime();
    ++m_totalConnections;
    return conn;
}

bool MySQLConnectionPool::checkIdleTimeout(MySQLConnection::ptr conn) {
    if(!conn) return false;
    int idleMs = m_config.idleTimeoutMs;
    if(idleMs <= 0) return false;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t releaseMs = conn->lastReleaseTimeMs();

    if(releaseMs > 0 && nowMs - releaseMs > idleMs) {
        ZERO_LOG_INFO(g_logger) << "MySQL idle connection timeout, close it";
        --m_totalConnections;
        conn->close();
        return true;
    }
    return false;
}

MySQLConnection::ptr MySQLConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(m_mutex);

    // 1. 优先从空闲队列取
    while(!m_connections.empty()) {
        auto conn = m_connections.front();
        m_connections.pop();
        lock.unlock();

        // idle 超时检查
        if(checkIdleTimeout(conn)) {
            lock.lock();
            continue;
        }

        if(!checkConnection(conn)) {
            auto replacement = createConnection();
            if(replacement) return replacement;
            lock.lock();
            continue;
        }
        return conn;
    }

    // 2. 动态扩容
    if(m_totalConnections.load() < m_config.maxConnections) {
        lock.unlock();
        auto conn = createConnection();
        if(conn) return conn;
        lock.lock();
    }

    // 3. 等待其他连接释放
    if(m_cv.wait_for(lock, std::chrono::milliseconds(m_config.connectionTimeout),
                     [this]() { return !m_connections.empty() || !m_running; })) {
        if(!m_running) return nullptr;
        while(!m_connections.empty()) {
            auto conn = m_connections.front();
            m_connections.pop();
            lock.unlock();

            if(checkIdleTimeout(conn)) {
                lock.lock();
                continue;
            }

            if(!checkConnection(conn)) {
                auto replacement = createConnection();
                if(replacement) return replacement;
                lock.lock();
                continue;
            }
            return conn;
        }
    }
    return nullptr;
}

bool MySQLConnectionPool::checkConnection(MySQLConnection::ptr conn) {
    if(!conn) return false;

    int64_t intervalMs = m_config.healthCheckIntervalMs;
    if(intervalMs <= 0) return true;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    int64_t lastMs = conn->lastCheckTimeMs();

    if(nowMs - lastMs < intervalMs) {
        return true;
    }

    if(!conn->ping()) {
        --m_totalConnections;
        return false;
    }
    return true;
}

void MySQLConnectionPool::releaseConnection(MySQLConnection::ptr conn) {
    if(!conn) return;
    releaseConnectionInternal(conn);
}

void MySQLConnectionPool::releaseConnectionInternal(MySQLConnection::ptr conn) {
    if(!conn) return;
    conn->updateReleaseTime();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!m_running) {
            --m_totalConnections;
            conn->close();
            return;
        }
        m_connections.push(conn);
    }
    m_cv.notify_one();
}

void MySQLConnectionPool::close() {
    m_running = false;
    m_cv.notify_all();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        while(!m_connections.empty()) {
            m_connections.front()->close();
            m_connections.pop();
            --m_totalConnections;
        }
    }
    if(m_workerPool) {
        m_workerPool->stop(true);
    }
}

int MySQLConnectionPool::getTimeoutMs(int timeoutMs) const {
    if(timeoutMs < 0) return m_config.queryTimeoutMs;
    return timeoutMs;
}

// ============================================================================
// 通用内部执行辅助
// ============================================================================

bool MySQLConnectionPool::runExecute(const std::string& sql,
                                     const std::function<bool(MySQLConnection::ptr)>& executor,
                                     int timeoutMs,
                                     MySQLConnection::ptr externalConn,
                                     bool releaseConn) {
    auto conn = externalConn ? externalConn : getConnection();
    if(!conn) {
        ZERO_LOG_ERROR(g_logger) << "MySQL getConnection failed, sql=" << sql;
        recordExecute(0, true, false);
        return false;
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    auto sem = std::make_shared<FiberSemaphore>(0);
    int realTimeout = getTimeoutMs(timeoutMs);

    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result, sem]() {
            result->timeout = true;
            sem->notify();
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, executor, result, releaseConn]() {
            auto start = std::chrono::steady_clock::now();
            bool ok = false;
            try {
                ok = executor(conn);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception || !ok) {
                result->executeSuccess = false;
                self->recordExecute(cost, true, result->timeout.load());
            } else {
                result->executeSuccess = true;
                result->affectedRows = conn->affectedRows();
                result->lastInsertId = conn->lastInsertId();
                self->recordExecute(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            if(releaseConn) {
                self->releaseConnectionInternal(conn);
            }
        },
        [sem]() {
            sem->notify();
        }
    );

    sem->wait();
    if(timer) {
        timer->cancel();
    }

    if(result->timeout) {
        ++m_timeoutQueries;
        ZERO_LOG_WARN(g_logger) << "MySQL execute timeout, sql=" << sql;
        return false;
    }
    if(result->exception) {
        std::rethrow_exception(result->exception);
    }
    return result->executeSuccess;
}

DbResult MySQLConnectionPool::runQuery(const std::string& sql,
                                       const std::function<DbResult(MySQLConnection::ptr)>& querier,
                                       int timeoutMs,
                                       MySQLConnection::ptr externalConn,
                                       bool releaseConn) {
    auto conn = externalConn ? externalConn : getConnection();
    if(!conn) {
        ZERO_LOG_ERROR(g_logger) << "MySQL getConnection failed, sql=" << sql;
        recordQuery(0, true, false);
        return DbResult();
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    auto sem = std::make_shared<FiberSemaphore>(0);
    int realTimeout = getTimeoutMs(timeoutMs);

    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result, sem]() {
            result->timeout = true;
            sem->notify();
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, querier, result, releaseConn]() {
            auto start = std::chrono::steady_clock::now();
            try {
                result->queryResult = querier(conn);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception) {
                self->recordQuery(cost, true, result->timeout.load());
            } else {
                self->recordQuery(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            if(releaseConn) {
                self->releaseConnectionInternal(conn);
            }
        },
        [sem]() {
            sem->notify();
        }
    );

    sem->wait();
    if(timer) {
        timer->cancel();
    }

    if(result->timeout) {
        ++m_timeoutQueries;
        ZERO_LOG_WARN(g_logger) << "MySQL query timeout, sql=" << sql;
        return DbResult();
    }
    if(result->exception) {
        std::rethrow_exception(result->exception);
    }
    return result->queryResult;
}

// ============================================================================
// MySQLConnectionPool 公共接口
// ============================================================================

bool MySQLConnectionPool::execute(const std::string& sql, int timeoutMs) {
    return runExecute(sql, [this, &sql](MySQLConnection::ptr conn) {
        return conn->executeBlocking(sql);
    }, timeoutMs);
}

bool MySQLConnectionPool::execute(const std::string& sql,
                                  const std::vector<db::DbValue>& params,
                                  int timeoutMs) {
    return runExecute(sql, [this, &sql, &params](MySQLConnection::ptr conn) {
        return conn->executePreparedBlocking(sql, params);
    }, timeoutMs);
}

DbResult MySQLConnectionPool::query(const std::string& sql, int timeoutMs) {
    return runQuery(sql, [this, &sql](MySQLConnection::ptr conn) {
        return conn->queryBlocking(sql);
    }, timeoutMs);
}

DbResult MySQLConnectionPool::query(const std::string& sql,
                                    const std::vector<db::DbValue>& params,
                                    int timeoutMs) {
    return runQuery(sql, [this, &sql, &params](MySQLConnection::ptr conn) {
        return conn->queryPreparedBlocking(sql, params);
    }, timeoutMs);
}

bool MySQLConnectionPool::execute(MySQLConnection::ptr conn, const std::string& sql,
                                  const std::vector<db::DbValue>& params, int timeoutMs) {
    if(!conn) {
        ZERO_LOG_ERROR(g_logger) << "MySQL execute conn is null, sql=" << sql;
        recordExecute(0, true, false);
        return false;
    }
    return runExecute(sql, [this, &sql, &params](MySQLConnection::ptr c) {
        return c->executePreparedBlocking(sql, params);
    }, timeoutMs, conn, false);
}

DbResult MySQLConnectionPool::query(MySQLConnection::ptr conn, const std::string& sql,
                                    const std::vector<db::DbValue>& params, int timeoutMs) {
    if(!conn) {
        ZERO_LOG_ERROR(g_logger) << "MySQL query conn is null, sql=" << sql;
        recordQuery(0, true, false);
        return DbResult();
    }
    return runQuery(sql, [this, &sql, &params](MySQLConnection::ptr c) {
        return c->queryPreparedBlocking(sql, params);
    }, timeoutMs, conn, false);
}

void MySQLConnectionPool::executeAsync(const std::string& sql,
                                       std::function<void(bool success, int64_t affectedRows)> callback,
                                       int timeoutMs) {
    auto conn = getConnection();
    if(!conn) {
        if(callback) callback(false, 0);
        return;
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    int realTimeout = getTimeoutMs(timeoutMs);
    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result]() {
            result->timeout = true;
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, result]() {
            auto start = std::chrono::steady_clock::now();
            bool ok = false;
            try {
                ok = conn->executeBlocking(sql);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception || !ok) {
                result->executeSuccess = false;
                self->recordExecute(cost, true, result->timeout.load());
            } else {
                result->executeSuccess = true;
                result->affectedRows = conn->affectedRows();
                result->lastInsertId = conn->lastInsertId();
                self->recordExecute(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            self->releaseConnectionInternal(conn);
        },
        [result, callback, timer]() {
            if(timer) timer->cancel();
            if(callback) callback(result->executeSuccess, result->affectedRows);
        }
    );
}

void MySQLConnectionPool::executeAsync(const std::string& sql,
                                       const std::vector<db::DbValue>& params,
                                       std::function<void(bool success, int64_t affectedRows)> callback,
                                       int timeoutMs) {
    auto conn = getConnection();
    if(!conn) {
        if(callback) callback(false, 0);
        return;
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    int realTimeout = getTimeoutMs(timeoutMs);
    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result]() {
            result->timeout = true;
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, params, result]() {
            auto start = std::chrono::steady_clock::now();
            bool ok = false;
            try {
                ok = conn->executePreparedBlocking(sql, params);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception || !ok) {
                result->executeSuccess = false;
                self->recordExecute(cost, true, result->timeout.load());
            } else {
                result->executeSuccess = true;
                result->affectedRows = conn->affectedRows();
                result->lastInsertId = conn->lastInsertId();
                self->recordExecute(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            self->releaseConnectionInternal(conn);
        },
        [result, callback, timer]() {
            if(timer) timer->cancel();
            if(callback) callback(result->executeSuccess, result->affectedRows);
        }
    );
}

void MySQLConnectionPool::queryAsync(const std::string& sql,
                                     std::function<void(DbResult)> callback,
                                     int timeoutMs) {
    auto conn = getConnection();
    if(!conn) {
        if(callback) callback(DbResult());
        return;
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    int realTimeout = getTimeoutMs(timeoutMs);
    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result]() {
            result->timeout = true;
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, result]() {
            auto start = std::chrono::steady_clock::now();
            try {
                result->queryResult = conn->queryBlocking(sql);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception) {
                self->recordQuery(cost, true, result->timeout.load());
            } else {
                self->recordQuery(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            self->releaseConnectionInternal(conn);
        },
        [result, callback, timer]() {
            if(timer) timer->cancel();
            if(callback) {
                if(result->timeout) {
                    callback(DbResult());
                } else {
                    callback(result->queryResult);
                }
            }
        }
    );
}

void MySQLConnectionPool::queryAsync(const std::string& sql,
                                     const std::vector<db::DbValue>& params,
                                     std::function<void(DbResult)> callback,
                                     int timeoutMs) {
    auto conn = getConnection();
    if(!conn) {
        if(callback) callback(DbResult());
        return;
    }

    auto result = std::make_shared<MySQLAsyncResult>();
    int realTimeout = getTimeoutMs(timeoutMs);
    Timer::ptr timer;
    IOManager* iom = dynamic_cast<IOManager*>(m_scheduler);
    if(realTimeout > 0 && iom) {
        timer = iom->addTimer(realTimeout, [result]() {
            result->timeout = true;
        }, false);
    }

    auto self = shared_from_this();
    m_workerPool->submit(
        [self, conn, sql, params, result]() {
            auto start = std::chrono::steady_clock::now();
            try {
                result->queryResult = conn->queryPreparedBlocking(sql, params);
            } catch(...) {
                result->exception = std::current_exception();
            }
            auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if(result->exception) {
                self->recordQuery(cost, true, result->timeout.load());
            } else {
                self->recordQuery(cost, false, false);
                if(static_cast<int>(cost) >= self->m_config.slowQueryThresholdMs) {
                    self->recordSlowQuery(sql, cost);
                }
            }
            result->done = true;
            self->releaseConnectionInternal(conn);
        },
        [result, callback, timer]() {
            if(timer) timer->cancel();
            if(callback) {
                if(result->timeout) {
                    callback(DbResult());
                } else {
                    callback(result->queryResult);
                }
            }
        }
    );
}

void MySQLConnectionPool::recordSlowQuery(const std::string& sql, uint64_t costMs) {
    ++m_slowQueries;
    ZERO_LOG_WARN(g_logger) << "MySQL slow query cost=" << costMs
                            << "ms sql=" << sql;
}

MySQLPoolStats MySQLConnectionPool::getStats() const {
    MySQLPoolStats stats;
    stats.totalConnections = m_totalConnections.load();
    stats.maxConnections = m_config.maxConnections;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        stats.idleConnections = m_connections.size();
    }
    stats.activeConnections = stats.totalConnections > stats.idleConnections
                              ? stats.totalConnections - stats.idleConnections : 0;
    stats.totalQueries = m_totalQueries.load();
    stats.totalExecutes = m_totalExecutes.load();
    stats.slowQueries = m_slowQueries.load();
    stats.failedQueries = m_failedQueries.load();
    stats.timeoutQueries = m_timeoutQueries.load();
    return stats;
}

std::string MySQLConnectionPool::getStatsJson() const {
    MySQLPoolStats s = getStats();
    std::ostringstream oss;
    oss << "{\"totalConnections\":" << s.totalConnections
        << ",\"idleConnections\":" << s.idleConnections
        << ",\"activeConnections\":" << s.activeConnections
        << ",\"maxConnections\":" << s.maxConnections
        << ",\"totalQueries\":" << s.totalQueries
        << ",\"totalExecutes\":" << s.totalExecutes
        << ",\"slowQueries\":" << s.slowQueries
        << ",\"failedQueries\":" << s.failedQueries
        << ",\"timeoutQueries\":" << s.timeoutQueries
        << "}";
    return oss.str();
}

void MySQLConnectionPool::recordQuery(uint64_t costMs, bool failed, bool timeout) {
    ++m_totalQueries;
    if(timeout) ++m_timeoutQueries;
    if(failed) ++m_failedQueries;
    else if(costMs >= static_cast<uint64_t>(m_config.slowQueryThresholdMs)) ++m_slowQueries;
}

void MySQLConnectionPool::recordExecute(uint64_t costMs, bool failed, bool timeout) {
    ++m_totalExecutes;
    if(timeout) ++m_timeoutQueries;
    if(failed) ++m_failedQueries;
    else if(costMs >= static_cast<uint64_t>(m_config.slowQueryThresholdMs)) ++m_slowQueries;
}

} // namespace zero

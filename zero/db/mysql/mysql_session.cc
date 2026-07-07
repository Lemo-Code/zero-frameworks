/**
 * @file mysql_session.cc
 * @brief MySQL 数据库会话实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "mysql_session.h"
#include "zero/core/log/log.h"
#include "zero/core/concurrency/fiber.h"
#include <chrono>

namespace zero {
namespace db {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

MySQLSession::MySQLSession(MySQLConnectionPool::ptr pool)
    : m_pool(pool) {
}

MySQLSession::~MySQLSession() {
    releaseConnection();
}

bool MySQLSession::ensureConnection() {
    if(m_conn) return true;
    if(!m_pool) return false;
    m_conn = m_pool->getConnection();
    m_ownedConnection = true;
    return m_conn != nullptr;
}

void MySQLSession::releaseConnection() {
    if(m_conn && m_ownedConnection && m_pool) {
        m_pool->releaseConnection(m_conn);
    }
    m_conn.reset();
    m_ownedConnection = false;
    m_inTransaction = false;
}

bool MySQLSession::execute(const std::string& sql, const DbValues& params) {
    if(!ensureConnection()) return false;
    for (auto& h : m_hooks) h->beforeExecute(sql, params);
    auto start = std::chrono::steady_clock::now();
    bool ok = m_pool->execute(m_conn, sql, params);
    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    for (auto& h : m_hooks) h->afterExecute(sql, params, ok, cost);
    return ok;
}

DbResult MySQLSession::query(const std::string& sql, const DbValues& params) {
    if(!ensureConnection()) return DbResult();
    for (auto& h : m_hooks) h->beforeQuery(sql, params);
    auto start = std::chrono::steady_clock::now();
    auto r = m_pool->query(m_conn, sql, params);
    auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    for (auto& h : m_hooks) h->afterQuery(sql, params, r, cost);
    return r;
}

int64_t MySQLSession::lastInsertId() const {
    if(!m_conn) return 0;
    return m_conn->lastInsertId();
}

int64_t MySQLSession::affectedRows() const {
    if(!m_conn) return 0;
    return m_conn->affectedRows();
}

bool MySQLSession::beginTransaction() {
    if(m_inTransaction) return true;
    if(!ensureConnection()) return false;
    if(!m_pool->execute(m_conn, "BEGIN", {})) {
        return false;
    }
    m_inTransaction = true;
    return true;
}

bool MySQLSession::commit() {
    if(!m_inTransaction) return false;
    bool ok = m_pool->execute(m_conn, "COMMIT", {});
    m_inTransaction = false;
    return ok;
}

bool MySQLSession::rollback() {
    if(!m_inTransaction) return false;
    bool ok = m_pool->execute(m_conn, "ROLLBACK", {});
    m_inTransaction = false;
    return ok;
}

std::shared_ptr<DbTransaction> MySQLSession::openTransaction() {
    return std::shared_ptr<DbTransaction>(new DbTransaction(shared_from_this()));
}

} // namespace db
} // namespace zero

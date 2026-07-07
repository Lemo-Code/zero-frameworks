/**
 * @file mysql_session.h
 * @brief MySQL 数据库会话
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_MYSQL_MYSQL_SESSION_H__
#define __ZERO_DB_MYSQL_MYSQL_SESSION_H__

#include "zero/db/db_session.h"
#include "zero/db/db_transaction.h"
#include "zero/db/mysql_connection.h"

namespace zero {
namespace db {

/**
 * @brief MySQL 会话
 * 
 * 持有一个连接池的连接，提供协程安全的执行接口和事务支持。
 */
class MySQLSession : public DbSession {
public:
    typedef std::shared_ptr<MySQLSession> ptr;

    explicit MySQLSession(MySQLConnectionPool::ptr pool);
    ~MySQLSession() override;

    bool execute(const std::string& sql, const DbValues& params = {}) override;
    DbResult query(const std::string& sql, const DbValues& params = {}) override;

    int64_t lastInsertId() const override;
    int64_t affectedRows() const override;

    bool inTransaction() const override { return m_inTransaction; }
    bool beginTransaction() override;
    bool commit() override;
    bool rollback() override;

    std::shared_ptr<DbTransaction> openTransaction() override;

private:
    bool ensureConnection();
    void releaseConnection();

    MySQLConnectionPool::ptr m_pool;
    MySQLConnection::ptr m_conn;
    bool m_inTransaction = false;
    bool m_ownedConnection = false;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_MYSQL_MYSQL_SESSION_H__

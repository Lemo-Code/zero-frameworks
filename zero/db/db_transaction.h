/**
 * @file db_transaction.h
 * @brief 编程式与声明式事务封装
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_TRANSACTION_H__
#define __ZERO_DB_DB_TRANSACTION_H__

#include <memory>
#include <functional>
#include "db_session.h"

namespace zero {
namespace db {

/**
 * @brief 事务隔离级别
 */
enum class DbIsolationLevel {
    Default,
    ReadUncommitted,
    ReadCommitted,
    RepeatableRead,
    Serializable
};

/**
 * @brief 编程式事务
 */
class DbTransaction {
public:
    typedef std::shared_ptr<DbTransaction> ptr;

    explicit DbTransaction(DbSession::ptr session,
                           DbIsolationLevel level = DbIsolationLevel::Default)
        : m_session(session), m_level(level), m_active(false), m_committed(false) {}

    ~DbTransaction() {
        if (m_active && !m_committed) {
            rollback();
        }
    }

    /**
     * @brief 开启事务
     */
    bool begin();

    /**
     * @brief 提交
     */
    bool commit();

    /**
     * @brief 回滚
     */
    bool rollback();

    bool active() const { return m_active; }
    bool committed() const { return m_committed; }

    DbSession::ptr session() { return m_session; }

private:
    DbSession::ptr m_session;
    DbIsolationLevel m_level;
    bool m_active;
    bool m_committed;
};

/**
 * @brief RAII 事务守卫
 * @code
 * {
 *     DbTransactionGuard guard(session);
 *     // do something
 *     if (ok) guard.commit();
 * }
 * @endcode
 */
class DbTransactionGuard {
public:
    explicit DbTransactionGuard(DbSession::ptr session,
                                DbIsolationLevel level = DbIsolationLevel::Default)
        : m_tx(std::make_shared<DbTransaction>(session, level)), m_committed(false) {
        m_tx->begin();
    }

    ~DbTransactionGuard() {
        if (m_tx && m_tx->active() && !m_committed) {
            m_tx->rollback();
        }
    }

    bool commit() {
        m_committed = true;
        return m_tx->commit();
    }

    bool rollback() {
        m_committed = true;
        return m_tx->rollback();
    }

    DbTransaction::ptr tx() { return m_tx; }

private:
    DbTransaction::ptr m_tx;
    bool m_committed;
};

/**
 * @brief 在事务中执行 lambda
 */
template<typename Func>
bool dbInTransaction(DbSession::ptr session, Func&& fn,
                     DbIsolationLevel level = DbIsolationLevel::Default) {
    DbTransaction tx(session, level);
    if (!tx.begin()) return false;
    try {
        if (fn(tx)) {
            return tx.commit();
        } else {
            tx.rollback();
            return false;
        }
    } catch (...) {
        tx.rollback();
        throw;
    }
}

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_TRANSACTION_H__

/**
 * @file db_transaction.cc
 * @brief 数据库事务封装实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "db_transaction.h"
#include "zero/core/log/log.h"

namespace zero {
namespace db {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// ============================================================================
// DbTransaction
// ============================================================================

bool DbTransaction::begin() {
    if(!m_session) return false;
    if(m_active) return true;

    std::string setIso;
    switch(m_level) {
        case DbIsolationLevel::ReadUncommitted:
            setIso = "SET SESSION TRANSACTION ISOLATION LEVEL READ UNCOMMITTED";
            break;
        case DbIsolationLevel::ReadCommitted:
            setIso = "SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED";
            break;
        case DbIsolationLevel::RepeatableRead:
            setIso = "SET SESSION TRANSACTION ISOLATION LEVEL REPEATABLE READ";
            break;
        case DbIsolationLevel::Serializable:
            setIso = "SET SESSION TRANSACTION ISOLATION LEVEL SERIALIZABLE";
            break;
        default:
            break;
    }
    if(!setIso.empty()) {
        if(!m_session->execute(setIso)) {
            return false;
        }
    }

    if(!m_session->beginTransaction()) {
        return false;
    }
    m_active = true;
    m_committed = false;
    return true;
}

bool DbTransaction::commit() {
    if(!m_active) return false;
    if(!m_session->commit()) {
        return false;
    }
    m_active = false;
    m_committed = true;
    return true;
}

bool DbTransaction::rollback() {
    if(!m_active) return false;
    bool ok = m_session->rollback();
    m_active = false;
    m_committed = false;
    return ok;
}

} // namespace db
} // namespace zero

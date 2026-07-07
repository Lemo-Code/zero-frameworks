/**
 * @file mysql_driver.cc
 * @brief MySQL 数据库驱动实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "mysql_driver.h"
#include "mysql_session.h"

namespace zero {
namespace db {

MySQLDriver::MySQLDriver(const MySQLPoolConfig& config, Scheduler* scheduler) {
    m_pool = MySQLConnectionPool::Create(config, scheduler);
}

MySQLDriver::ptr MySQLDriver::Create(const MySQLPoolConfig& config, Scheduler* scheduler) {
    return std::shared_ptr<MySQLDriver>(new MySQLDriver(config, scheduler));
}

std::shared_ptr<DbSession> MySQLDriver::openSession() {
    return std::shared_ptr<DbSession>(new MySQLSession(m_pool));
}

std::string MySQLDriver::getStatsJson() const {
    if(!m_pool) return "{}";
    return m_pool->getStatsJson();
}

void MySQLDriver::close() {
    if(m_pool) {
        m_pool->close();
    }
}

} // namespace db
} // namespace zero

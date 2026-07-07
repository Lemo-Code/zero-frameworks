/**
 * @file mysql_driver.h
 * @brief MySQL 数据库驱动
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_MYSQL_MYSQL_DRIVER_H__
#define __ZERO_DB_MYSQL_MYSQL_DRIVER_H__

#include "zero/db/db_driver.h"
#include "zero/db/mysql_connection.h"

namespace zero {
namespace db {

/**
 * @brief MySQL 驱动
 */
class MySQLDriver : public DbDriver {
public:
    typedef std::shared_ptr<MySQLDriver> ptr;

    static MySQLDriver::ptr Create(const MySQLPoolConfig& config, Scheduler* scheduler = nullptr);

    std::string name() const override { return "mysql"; }
    std::shared_ptr<DbSession> openSession() override;
    std::string getStatsJson() const override;
    void close() override;

    MySQLConnectionPool::ptr pool() const { return m_pool; }

private:
    MySQLDriver(const MySQLPoolConfig& config, Scheduler* scheduler);

    MySQLConnectionPool::ptr m_pool;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_MYSQL_MYSQL_DRIVER_H__

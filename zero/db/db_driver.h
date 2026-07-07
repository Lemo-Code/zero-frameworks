/**
 * @file db_driver.h
 * @brief 数据库驱动抽象接口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_DRIVER_H__
#define __ZERO_DB_DB_DRIVER_H__

#include <memory>
#include <string>
#include "db_value.h"

namespace zero {
namespace db {

class DbSession;

/**
 * @brief 数据库驱动抽象
 * 
 * 每种数据库实现一个 DbDriver，负责创建会话和管理连接池。
 */
class DbDriver {
public:
    typedef std::shared_ptr<DbDriver> ptr;

    virtual ~DbDriver() = default;

    /**
     * @brief 驱动名称，如 "mysql"
     */
    virtual std::string name() const = 0;

    /**
     * @brief 打开一个会话
     */
    virtual std::shared_ptr<DbSession> openSession() = 0;

    /**
     * @brief 返回连接池统计信息 JSON
     */
    virtual std::string getStatsJson() const = 0;

    /**
     * @brief 关闭驱动，释放所有连接
     */
    virtual void close() = 0;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_DRIVER_H__

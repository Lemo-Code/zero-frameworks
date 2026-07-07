/**
 * @file db_session.h
 * @brief 数据库会话抽象接口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_SESSION_H__
#define __ZERO_DB_DB_SESSION_H__

#include <memory>
#include <string>
#include <vector>
#include <map>
#include "db_value.h"
#include "db_session_hook.h"

namespace zero {
namespace db {

/**
 * @brief 数据库行
 */
typedef std::map<std::string, std::string> DbRow;

/**
 * @brief 查询结果集
 */
typedef std::vector<DbRow> DbResult;

class DbTransaction;

/**
 * @brief 数据库会话抽象
 * 
 * 一个 Session 对应一个数据库连接（或连接池中的逻辑连接）。
 * 事务期间会独占一个物理连接。
 */
class DbSession : public std::enable_shared_from_this<DbSession> {
public:
    typedef std::shared_ptr<DbSession> ptr;

    virtual ~DbSession() = default;

    /**
     * @brief 执行 SQL（insert/update/delete）
     * @param sql SQL 语句，可包含 ? 占位符
     * @param params 绑定参数
     * @return 是否成功
     */
    virtual bool execute(const std::string& sql,
                         const DbValues& params = {}) = 0;

    /**
     * @brief 查询 SQL
     * @param sql SQL 语句，可包含 ? 占位符
     * @param params 绑定参数
     * @return 结果集
     */
    virtual DbResult query(const std::string& sql,
                           const DbValues& params = {}) = 0;

    /**
     * @brief 最后插入的自增 ID
     */
    virtual int64_t lastInsertId() const = 0;

    /**
     * @brief 上次执行影响的行数
     */
    virtual int64_t affectedRows() const = 0;

    /**
     * @brief 是否处于事务中
     */
    virtual bool inTransaction() const = 0;

    /**
     * @brief 开始事务
     */
    virtual bool beginTransaction() = 0;

    /**
     * @brief 提交事务
     */
    virtual bool commit() = 0;

    /**
     * @brief 回滚事务
     */
    virtual bool rollback() = 0;

    /**
     * @brief 创建事务对象（编程式事务）
     */
    virtual std::shared_ptr<DbTransaction> openTransaction() = 0;

    /**
     * @brief 关闭会话
     */
    virtual void close() {}

    /**
     * @brief 添加 SQL 审计钩子
     */
    void addHook(DbSessionHook::ptr hook) {
        if (hook) m_hooks.push_back(hook);
    }

    void removeHooks() {
        m_hooks.clear();
    }

protected:
    std::vector<DbSessionHook::ptr> m_hooks;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_SESSION_H__

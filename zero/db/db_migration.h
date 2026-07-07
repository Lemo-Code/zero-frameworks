/**
 * @file db_migration.h
 * @brief 数据库迁移版本管理
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_MIGRATION_H__
#define __ZERO_DB_DB_MIGRATION_H__

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "db_session.h"
#include "db_value.h"

namespace zero {
namespace db {

/**
 * @brief 单个迁移版本接口
 */
class DbMigration {
public:
    typedef std::shared_ptr<DbMigration> ptr;
    virtual ~DbMigration() = default;

    virtual int64_t version() const = 0;
    virtual std::string name() const = 0;
    virtual bool up(DbSession::ptr session) = 0;
    virtual bool down(DbSession::ptr session) = 0;
};

/**
 * @brief Lambda 迁移实现
 */
class LambdaDbMigration : public DbMigration {
public:
    typedef std::shared_ptr<LambdaDbMigration> ptr;

    LambdaDbMigration(int64_t ver, const std::string& name,
                      std::function<bool(DbSession::ptr)> upFn,
                      std::function<bool(DbSession::ptr)> downFn)
        : m_version(ver), m_name(name), m_up(upFn), m_down(downFn) {
    }

    int64_t version() const override { return m_version; }
    std::string name() const override { return m_name; }

    bool up(DbSession::ptr session) override {
        return m_up ? m_up(session) : true;
    }

    bool down(DbSession::ptr session) override {
        return m_down ? m_down(session) : true;
    }

private:
    int64_t m_version;
    std::string m_name;
    std::function<bool(DbSession::ptr)> m_up;
    std::function<bool(DbSession::ptr)> m_down;
};

/**
 * @brief 迁移执行结果
 */
struct DbMigrationResult {
    bool ok = false;
    std::string message;
    int64_t currentVersion = 0;
    std::vector<std::string> applied;
};

/**
 * @brief 迁移管理器
 */
class DbMigrationManager {
public:
    typedef std::shared_ptr<DbMigrationManager> ptr;

    explicit DbMigrationManager(DbSession::ptr session,
                                const std::string& tableName = "zero_migration_history");

    void registerMigration(DbMigration::ptr migration);

    /**
     * @brief 升级到指定版本，0 表示最新
     */
    DbMigrationResult migrate(int64_t targetVersion = 0);

    /**
     * @brief 回退 steps 个版本
     */
    DbMigrationResult rollback(size_t steps = 1);

    /**
     * @brief 当前已应用版本号
     */
    int64_t currentVersion() const;

    /**
     * @brief 已注册迁移列表
     */
    std::vector<DbMigration::ptr> migrations() const;

private:
    bool ensureHistoryTable();
    bool recordApplied(int64_t version, const std::string& name);
    bool removeRecord(int64_t version);
    std::vector<int64_t> appliedVersions() const;

    DbSession::ptr m_session;
    std::string m_table;
    std::map<int64_t, DbMigration::ptr> m_migrations;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_MIGRATION_H__

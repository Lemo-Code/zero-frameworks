/**
 * @file db_migration.cc
 * @brief 数据库迁移管理器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "db_migration.h"
#include <sstream>
#include <algorithm>
#include <set>

namespace zero {
namespace db {

DbMigrationManager::DbMigrationManager(DbSession::ptr session, const std::string& tableName)
    : m_session(session), m_table(tableName) {
}

void DbMigrationManager::registerMigration(DbMigration::ptr migration) {
    if (!migration) return;
    m_migrations[migration->version()] = migration;
}

bool DbMigrationManager::ensureHistoryTable() {
    if (!m_session) return false;
    std::ostringstream oss;
    oss << "CREATE TABLE IF NOT EXISTS `" << m_table << "` ("
        << "`id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,"
        << "`version` BIGINT NOT NULL,"
        << "`name` VARCHAR(255) NOT NULL,"
        << "`applied_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        << "UNIQUE KEY `uk_version` (`version`)"
        << ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    return m_session->execute(oss.str());
}

std::vector<int64_t> DbMigrationManager::appliedVersions() const {
    std::vector<int64_t> result;
    if (!m_session) return result;
    std::ostringstream oss;
    oss << "SELECT `version` FROM `" << m_table << "` ORDER BY `version` ASC";
    auto r = m_session->query(oss.str());
    for (const auto& row : r) {
        auto it = row.find("version");
        if (it != row.end()) {
            result.push_back(std::stoll(it->second));
        }
    }
    return result;
}

bool DbMigrationManager::recordApplied(int64_t version, const std::string& name) {
    if (!m_session) return false;
    std::ostringstream oss;
    oss << "INSERT INTO `" << m_table << "` (`version`, `name`) VALUES (?, ?)";
    DbValues params;
    params.push_back(DbValue::int64(version));
    params.push_back(DbValue::string(name));
    return m_session->execute(oss.str(), params);
}

bool DbMigrationManager::removeRecord(int64_t version) {
    if (!m_session) return false;
    std::ostringstream oss;
    oss << "DELETE FROM `" << m_table << "` WHERE `version` = ?";
    DbValues params;
    params.push_back(DbValue::int64(version));
    return m_session->execute(oss.str(), params);
}

int64_t DbMigrationManager::currentVersion() const {
    auto versions = appliedVersions();
    return versions.empty() ? 0 : versions.back();
}

std::vector<DbMigration::ptr> DbMigrationManager::migrations() const {
    std::vector<DbMigration::ptr> result;
    for (const auto& kv : m_migrations) {
        result.push_back(kv.second);
    }
    return result;
}

DbMigrationResult DbMigrationManager::migrate(int64_t targetVersion) {
    DbMigrationResult result;
    if (!m_session) {
        result.message = "session is null";
        return result;
    }
    if (!ensureHistoryTable()) {
        result.message = "failed to create migration history table";
        return result;
    }
    auto applied = appliedVersions();
    std::set<int64_t> appliedSet(applied.begin(), applied.end());

    std::vector<int64_t> pending;
    for (const auto& kv : m_migrations) {
        if (appliedSet.find(kv.first) == appliedSet.end()) {
            if (targetVersion == 0 || kv.first <= targetVersion) {
                pending.push_back(kv.first);
            }
        }
    }

    for (int64_t ver : pending) {
        auto it = m_migrations.find(ver);
        if (it == m_migrations.end()) continue;
        auto m = it->second;
        if (!m->up(m_session)) {
            result.message = "migration failed: " + m->name();
            result.currentVersion = currentVersion();
            return result;
        }
        if (!recordApplied(m->version(), m->name())) {
            result.message = "failed to record migration: " + m->name();
            result.currentVersion = currentVersion();
            return result;
        }
        result.applied.push_back(m->name());
    }

    result.ok = true;
    result.currentVersion = currentVersion();
    result.message = "migrate success";
    return result;
}

DbMigrationResult DbMigrationManager::rollback(size_t steps) {
    DbMigrationResult result;
    if (!m_session) {
        result.message = "session is null";
        return result;
    }
    if (!ensureHistoryTable()) {
        result.message = "failed to create migration history table";
        return result;
    }
    auto applied = appliedVersions();
    if (applied.empty()) {
        result.ok = true;
        result.message = "no migrations to rollback";
        return result;
    }

    size_t count = 0;
    for (auto it = applied.rbegin(); it != applied.rend() && count < steps; ++it, ++count) {
        int64_t ver = *it;
        auto mit = m_migrations.find(ver);
        if (mit == m_migrations.end()) {
            result.message = "migration not found for version: " + std::to_string(ver);
            result.currentVersion = currentVersion();
            return result;
        }
        auto m = mit->second;
        if (!m->down(m_session)) {
            result.message = "rollback failed: " + m->name();
            result.currentVersion = currentVersion();
            return result;
        }
        if (!removeRecord(ver)) {
            result.message = "failed to remove migration record: " + m->name();
            result.currentVersion = currentVersion();
            return result;
        }
        result.applied.push_back(m->name());
    }

    result.ok = true;
    result.currentVersion = currentVersion();
    result.message = "rollback success";
    return result;
}

} // namespace db
} // namespace zero

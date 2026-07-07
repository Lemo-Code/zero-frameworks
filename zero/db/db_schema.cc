/**
 * @file db_schema.cc
 * @brief Schema 构建器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "db_schema.h"
#include "zero/core/log/log.h"
#include <sstream>

namespace zero {
namespace db {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

std::string DbSchema::fieldTypeToSql(DbValueType type, size_t maxLength) {
    switch(type) {
        case DbValueType::Int64:
        case DbValueType::UInt64:
            return "BIGINT";
        case DbValueType::Double:
            return "DOUBLE";
        case DbValueType::String:
            if(maxLength > 0 && maxLength <= 65535) {
                return "VARCHAR(" + std::to_string(maxLength) + ")";
            }
            return "TEXT";
        case DbValueType::Blob:
            return "BLOB";
        case DbValueType::Null:
        default:
            return "VARCHAR(255)";
    }
}

std::string DbSchema::generateCreateTableSql(const std::string& table,
                                              const std::vector<DbModelField>& fields) {
    std::ostringstream sql;
    sql << "CREATE TABLE IF NOT EXISTS `" << table << "` (\n";

    std::vector<std::string> primaryKeys;
    std::vector<std::string> uniques;
    std::vector<std::string> indexes;

    for(size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        if(i > 0) sql << ",\n";
        sql << "  `" << f.columnName << "` " << fieldTypeToSql(f.type, f.maxLength);

        if((f.flags & NotNull) != 0) {
            sql << " NOT NULL";
        }
        if((f.flags & AutoIncrement) != 0) {
            sql << " AUTO_INCREMENT";
        }
        if(!f.defaultValue.empty()) {
            sql << " DEFAULT '" << f.defaultValue << "'";
        }

        if((f.flags & PrimaryKey) != 0) {
            primaryKeys.push_back(f.columnName);
        }
        if((f.flags & Unique) != 0) {
            uniques.push_back(f.columnName);
        }
        if((f.flags & Index) != 0 && (f.flags & PrimaryKey) == 0) {
            indexes.push_back(f.columnName);
        }
    }

    if(!primaryKeys.empty()) {
        sql << ",\n  PRIMARY KEY (";
        for(size_t i = 0; i < primaryKeys.size(); ++i) {
            if(i > 0) sql << ", ";
            sql << "`" << primaryKeys[i] << "`";
        }
        sql << ")";
    }

    for(const auto& col : uniques) {
        sql << ",\n  UNIQUE KEY `uk_" << col << "` (`" << col << "`)";
    }

    for(const auto& col : indexes) {
        sql << ",\n  KEY `idx_" << col << "` (`" << col << "`)";
    }

    sql << "\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
    return sql.str();
}

bool DbSchema::createTable(DbSession::ptr session, const std::string& table,
                           const std::vector<DbModelField>& fields) {
    if(!session) return false;
    std::string sql = generateCreateTableSql(table, fields);
    ZERO_LOG_INFO(g_logger) << "CREATE TABLE SQL: " << sql;
    return session->execute(sql);
}

bool DbSchema::dropTable(DbSession::ptr session, const std::string& table) {
    if(!session) return false;
    return session->execute("DROP TABLE IF EXISTS `" + table + "`");
}

bool DbSchema::addColumn(DbSession::ptr session, const std::string& table,
                         const DbModelField& field) {
    if(!session) return false;
    std::ostringstream sql;
    sql << "ALTER TABLE `" << table << "` ADD COLUMN `" << field.columnName << "` "
        << fieldTypeToSql(field.type, field.maxLength);
    if((field.flags & NotNull) != 0) sql << " NOT NULL";
    return session->execute(sql.str());
}

} // namespace db
} // namespace zero

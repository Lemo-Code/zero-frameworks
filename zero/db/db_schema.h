/**
 * @file db_schema.h
 * @brief Schema 构建器：根据模型元数据生成 DDL
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_SCHEMA_H__
#define __ZERO_DB_DB_SCHEMA_H__

#include <memory>
#include <string>
#include <vector>
#include "db_model.h"
#include "db_session.h"

namespace zero {
namespace db {

/**
 * @brief Schema 构建器
 *
 * 目前主要支持 MySQL DDL 生成，后续可扩展为驱动感知。
 */
class DbSchema {
public:
    /**
     * @brief 根据模型元数据创建表
     */
    static bool createTable(DbSession::ptr session, const std::string& table,
                            const std::vector<DbModelField>& fields);

    /**
     * @brief 根据模型创建表
     */
    template<typename T>
    static bool createTable(DbSession::ptr session) {
        T model;
        return createTable(session, model.tableName(), model.fields());
    }

    /**
     * @brief 删除表
     */
    static bool dropTable(DbSession::ptr session, const std::string& table);

    /**
     * @brief 添加列
     */
    static bool addColumn(DbSession::ptr session, const std::string& table,
                          const DbModelField& field);

    /**
     * @brief 生成 CREATE TABLE SQL（不执行）
     */
    static std::string generateCreateTableSql(const std::string& table,
                                               const std::vector<DbModelField>& fields);

    /**
     * @brief 字段类型转 MySQL 类型字符串
     */
    static std::string fieldTypeToSql(DbValueType type, size_t maxLength);
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_SCHEMA_H__

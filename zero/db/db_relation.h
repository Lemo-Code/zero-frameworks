/**
 * @file db_relation.h
 * @brief ORM 关联查询：hasOne / hasMany / belongsTo
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_RELATION_H__
#define __ZERO_DB_DB_RELATION_H__

#include <memory>
#include <vector>
#include <set>
#include "db_model.h"
#include "db_session.h"
#include "db_query.h"

namespace zero {
namespace db {

/**
 * @brief 关联查询工具模板
 *
 * 示例：
 *   auto profile = DbRelation<User, UserProfile>::hasOne(
 *       session, "user_id", "id", DbValue::int64(user.id));
 */
template<typename Owner, typename Related>
class DbRelation {
public:
    /**
     * @brief Owner 拥有一条 Related 记录
     * @param session 数据库会话
     * @param foreignKey Related 表中的外键列
     * @param ownerKey   Owner 表中的关联键列（仅用于生成，实际值由 ownerValue 提供）
     * @param ownerValue 外键值
     */
    static std::shared_ptr<Related> hasOne(DbSession::ptr session,
                                           const std::string& foreignKey,
                                           const std::string& /*ownerKey*/,
                                           const DbValue& ownerValue) {
        if (!session) return nullptr;
        Related proto;
        auto q = std::make_shared<DbQuery>(session, proto.tableName());
        q->select()->whereEq(foreignKey, ownerValue)->limit(1);
        auto r = q->get();
        if (r.empty()) return nullptr;
        auto p = std::make_shared<Related>();
        p->fromRow(r[0]);
        return p;
    }

    /**
     * @brief Owner 拥有多条 Related 记录
     */
    static std::vector<std::shared_ptr<Related>> hasMany(DbSession::ptr session,
                                                         const std::string& foreignKey,
                                                         const std::string& /*ownerKey*/,
                                                         const DbValue& ownerValue) {
        std::vector<std::shared_ptr<Related>> result;
        if (!session) return result;
        Related proto;
        auto q = std::make_shared<DbQuery>(session, proto.tableName());
        q->select()->whereEq(foreignKey, ownerValue);
        auto r = q->get();
        for (const auto& row : r) {
            auto p = std::make_shared<Related>();
            p->fromRow(row);
            result.push_back(p);
        }
        return result;
    }

    /**
     * @brief Owner 属于一条 Related 记录
     * @param foreignKey Owner 表中的外键列
     * @param ownerKey   Related 表中的主键列
     * @param foreignValue 外键值
     */
    static std::shared_ptr<Related> belongsTo(DbSession::ptr session,
                                              const std::string& /*foreignKey*/,
                                              const std::string& ownerKey,
                                              const DbValue& foreignValue) {
        if (!session) return nullptr;
        Related proto;
        auto q = std::make_shared<DbQuery>(session, proto.tableName());
        q->select()->whereEq(ownerKey, foreignValue)->limit(1);
        auto r = q->get();
        if (r.empty()) return nullptr;
        auto p = std::make_shared<Related>();
        p->fromRow(r[0]);
        return p;
    }

    /**
     * @brief 预加载多条关联记录，避免 N+1
     * @param owners     Owner 对象列表
     * @param ownerKey   Owner 表中用于关联的字段名，如 "id"
     * @param foreignKey Related 表中的外键字段名，如 "user_id"
     */
    static std::vector<std::shared_ptr<Related>> withMany(
            DbSession::ptr session,
            const std::vector<std::shared_ptr<Owner>>& owners,
            const std::string& ownerKey,
            const std::string& foreignKey) {
        std::vector<std::shared_ptr<Related>> result;
        if (!session || owners.empty()) return result;

        DbValues values;
        std::set<std::string> seen;
        for (const auto& o : owners) {
            if (!o) continue;
            auto row = o->toRow();
            auto it = row.find(ownerKey);
            if (it != row.end() && !it->second.empty() && seen.insert(it->second).second) {
                values.push_back(DbValue::string(it->second));
            }
        }
        if (values.empty()) return result;

        Related proto;
        auto q = std::make_shared<DbQuery>(session, proto.tableName());
        q->select()->whereIn(foreignKey, values);
        auto r = q->get();
        for (const auto& row : r) {
            auto p = std::make_shared<Related>();
            p->fromRow(row);
            result.push_back(p);
        }
        return result;
    }
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_RELATION_H__

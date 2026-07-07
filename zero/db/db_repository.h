/**
 * @file db_repository.h
 * @brief ActiveRecord 风格仓储，支持自动时间戳/软删除/乐观锁
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_REPOSITORY_H__
#define __ZERO_DB_DB_REPOSITORY_H__

#include <memory>
#include <vector>
#include <type_traits>
#include "db_model.h"
#include "db_session.h"
#include "db_query.h"
#include "db_result.h"
#include "db_cache.h"

namespace zero {
namespace db {

namespace detail {

inline DbError makeError(DbSession::ptr session, const std::string& sql = "") {
    DbError err;
    err.code = DbErrorCode::SqlError;
    err.sql = sql;
    (void)session;
    return err;
}

inline std::string encodeCacheKey(const std::string& table, const std::string& id) {
    return "orm:" + table + ":" + id;
}

inline std::string rowToString(const DbRow& row) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& kv : row) {
        if (!first) oss << "\n";
        first = false;
        oss << kv.first << "=" << kv.second.size() << ":" << kv.second;
    }
    return oss.str();
}

inline DbRow rowFromString(const std::string& s) {
    DbRow row;
    size_t start = 0;
    while (start < s.size()) {
        size_t eq = s.find('=', start);
        if (eq == std::string::npos) break;
        size_t colon = s.find(':', eq + 1);
        if (colon == std::string::npos) break;
        size_t len = 0;
        try {
            len = static_cast<size_t>(std::stoull(s.substr(eq + 1, colon - eq - 1)));
        } catch (...) {
            break;
        }
        std::string key = s.substr(start, eq - start);
        std::string value = s.substr(colon + 1, len);
        row[key] = value;
        start = colon + 1 + len;
        if (start < s.size() && s[start] == '\n') {
            ++start;
        }
    }
    return row;
}

inline void applyAutoTimestampOnInsert(DbModel& model, std::map<std::string, DbValue>& values) {
    if (!model.isAutoTimestamp()) return;
    std::string now = detail::nowTimestamp();
    if (!model.createdAtColumn().empty()) {
        values[model.createdAtColumn()] = DbValue::string(now);
    }
    if (!model.updatedAtColumn().empty()) {
        values[model.updatedAtColumn()] = DbValue::string(now);
    }
}

inline void applyAutoTimestampOnUpdate(DbModel& model, std::map<std::string, DbValue>& values) {
    if (!model.isAutoTimestamp()) return;
    if (!model.updatedAtColumn().empty()) {
        values[model.updatedAtColumn()] = DbValue::string(detail::nowTimestamp());
    }
}

inline void applySoftDeleteFilter(DbModel& model, DbQuery::ptr q) {
    if (!model.isSoftDelete()) return;
    q->whereNull(model.softDeleteColumn());
}

} // namespace detail

/**
 * @brief 仓储模板，T 必须继承 DbModel
 */
template<typename T>
class DbRepository {
public:
    typedef std::shared_ptr<DbRepository<T>> ptr;

    explicit DbRepository(DbSession::ptr session, DbCache::ptr cache = nullptr)
        : m_session(session), m_cache(cache) {
        static_assert(std::is_base_of<DbModel, T>::value, "T must inherit from DbModel");
    }

    void setCache(DbCache::ptr cache) { m_cache = cache; }
    DbCache::ptr cache() const { return m_cache; }

    std::string cacheKey(const std::string& id) const {
        T proto;
        return detail::encodeCacheKey(proto.tableName(), id);
    }

    bool invalidateCache(const std::string& id) {
        if (!m_cache) return true;
        return m_cache->remove(cacheKey(id));
    }

    // ========================================================================
    // 基础 CRUD
    // ========================================================================

    bool save(T& model) {
        if (!m_session) return false;
        auto err = model.validate();
        if (!err.ok()) return false;
        model.beforeSave();
        bool ok = model.isNew() ? insert(model) : update(model);
        if (ok) model.afterSave();
        return ok;
    }

    DbResultT<bool> save2(T& model) {
        if (!m_session) {
            return DbResultT<bool>::fail(DbErrorCode::ConnectFail, "session is null");
        }
        auto err = model.validate();
        if (!err.ok()) {
            return DbResultT<bool>::fail(DbErrorCode::ConstraintViolation, err.message());
        }
        model.beforeSave();
        bool ok = model.isNew() ? insert(model) : update(model);
        if (ok) model.afterSave();
        return DbResultT<bool>::success(ok);
    }

    bool insert(T& model) {
        if (!m_session) return false;
        model.beforeInsert();
        DbRow row = model.toRow();
        if (model.isAutoIncrement()) {
            row.erase(model.primaryKey());
        }
        std::map<std::string, DbValue> values;
        for (const auto& kv : row) {
            values[kv.first] = DbValue::string(kv.second);
        }
        detail::applyAutoTimestampOnInsert(model, values);
        if (values.empty()) return false;

        auto q = std::make_shared<DbQuery>(m_session, model.tableName());
        auto p = q->buildInsert(values);
        bool ok = m_session->execute(p.first, p.second);
        if (ok) {
            if (model.isAutoIncrement()) {
                auto r = m_session->query("SELECT LAST_INSERT_ID() AS id");
                if (!r.empty()) {
                    auto it = r[0].find("id");
                    if (it != r[0].end()) {
                        model.setPrimaryKeyValue(it->second);
                    }
                }
            }
            if (m_cache) {
                DbRow row = model.toRow();
                m_cache->set(cacheKey(model.primaryKeyValue()), detail::rowToString(row), 300);
            }
            model.afterInsert();
        }
        return ok;
    }

    bool update(const T& model) {
        if (!m_session) return false;
        auto err = model.validate();
        if (!err.ok()) return false;
        if (model.isNew()) return false;
        const_cast<T&>(model).beforeUpdate();
        DbRow row = model.toRow();
        row.erase(model.primaryKey());
        std::map<std::string, DbValue> values;
        for (const auto& kv : row) {
            values[kv.first] = DbValue::string(kv.second);
        }
        detail::applyAutoTimestampOnUpdate(const_cast<T&>(model), values);
        if (values.empty()) return false;

        auto q = std::make_shared<DbQuery>(m_session, model.tableName());
        q->whereEq(model.primaryKey(), DbValue::string(model.primaryKeyValue()));
        auto p = q->buildUpdate(values);
        bool ok = m_session->execute(p.first, p.second);
        if (ok) {
            invalidateCache(model.primaryKeyValue());
            const_cast<T&>(model).afterUpdate();
        }
        return ok;
    }

    /**
     * @brief 乐观锁更新，成功时回写 version+1
     */
    bool updateWithVersion(T& model) {
        if (!m_session) return false;
        auto err = model.validate();
        if (!err.ok()) return false;
        if (model.isNew()) return false;
        if (!model.isOptimisticLock()) return update(model);
        model.beforeUpdate();

        DbRow row = model.toRow();
        row.erase(model.primaryKey());
        std::map<std::string, DbValue> values;
        for (const auto& kv : row) {
            values[kv.first] = DbValue::string(kv.second);
        }
        detail::applyAutoTimestampOnUpdate(model, values);

        int64_t oldVer = model.versionValue();
        values[model.versionColumn()] = DbValue::int64(oldVer + 1);

        auto q = std::make_shared<DbQuery>(m_session, model.tableName());
        q->whereEq(model.primaryKey(), DbValue::string(model.primaryKeyValue()))
          ->whereEq(model.versionColumn(), DbValue::int64(oldVer));
        auto p = q->buildUpdate(values);
        bool ok = m_session->execute(p.first, p.second);
        if (ok && m_session->affectedRows() > 0) {
            model.setFieldValue(model.versionColumn(), std::to_string(oldVer + 1));
            model.afterUpdate();
        }
        return ok && m_session->affectedRows() > 0;
    }

    std::shared_ptr<T> findById(const std::string& id) {
        if (!m_session) return nullptr;
        T proto;
        if (m_cache) {
            std::string cached;
            if (m_cache->get(cacheKey(id), cached)) {
                auto result = std::make_shared<T>();
                result->fromRow(detail::rowFromString(cached));
                result->afterLoad();
                return result;
            }
        }
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        q->select()->whereEq(proto.primaryKey(), DbValue::string(id))->limit(1);
        detail::applySoftDeleteFilter(proto, q);
        auto p = q->buildSelect();
        auto r = m_session->query(p.first, p.second);
        if (r.empty()) return nullptr;
        auto result = std::make_shared<T>();
        result->fromRow(r[0]);
        result->afterLoad();
        if (m_cache) {
            m_cache->set(cacheKey(id), detail::rowToString(r[0]), 300);
        }
        return result;
    }

    DbResultT<std::shared_ptr<T>> findById2(const std::string& id) {
        if (!m_session) {
            return DbResultT<std::shared_ptr<T>>::fail(DbErrorCode::ConnectFail, "session is null");
        }
        auto p = findById(id);
        if (!p) {
            return DbResultT<std::shared_ptr<T>>::fail(DbErrorCode::NotFound, "record not found");
        }
        return DbResultT<std::shared_ptr<T>>::success(p);
    }

    bool removeById(const std::string& id) {
        if (!m_session) return false;
        T proto;
        proto.beforeDelete();
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        q->whereEq(proto.primaryKey(), DbValue::string(id));
        bool ok = false;
        if (proto.isSoftDelete()) {
            std::map<std::string, DbValue> values;
            values[proto.softDeleteColumn()] = DbValue::string(detail::nowTimestamp());
            ok = m_session->execute(q->buildUpdate(values).first, q->buildUpdate(values).second);
        } else {
            auto p = q->buildDelete();
            ok = m_session->execute(p.first, p.second);
        }
        if (ok) {
            invalidateCache(id);
            proto.afterDelete();
        }
        return ok;
    }

    bool remove(const T& model) {
        return removeById(model.primaryKeyValue());
    }

    /**
     * @brief 硬删除（忽略软删除标志）
     */
    bool forceRemoveById(const std::string& id) {
        if (!m_session) return false;
        T proto;
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        q->whereEq(proto.primaryKey(), DbValue::string(id));
        auto p = q->buildDelete();
        return m_session->execute(p.first, p.second);
    }

    bool exists(const std::string& id) {
        if (!m_session) return false;
        T proto;
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        q->whereEq(proto.primaryKey(), DbValue::string(id));
        detail::applySoftDeleteFilter(proto, q);
        return q->exists();
    }

    // ========================================================================
    // QueryBuilder
    // ========================================================================

    DbQuery::ptr query() {
        if (!m_session) return nullptr;
        T proto;
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        detail::applySoftDeleteFilter(proto, q);
        return q;
    }

    /**
     * @brief 包含已软删除记录的查询构造器
     */
    DbQuery::ptr queryWithTrashed() {
        if (!m_session) return nullptr;
        T proto;
        return std::make_shared<DbQuery>(m_session, proto.tableName());
    }

    std::vector<std::shared_ptr<T>> list(DbQuery::ptr q) {
        std::vector<std::shared_ptr<T>> result;
        if (!m_session || !q) return result;
        auto p = q->buildSelect();
        auto r = m_session->query(p.first, p.second);
        for (const auto& row : r) {
            auto p = std::make_shared<T>();
            p->fromRow(row);
            p->afterLoad();
            result.push_back(p);
        }
        return result;
    }

    DbResultT<std::vector<std::shared_ptr<T>>> list2(DbQuery::ptr q) {
        if (!m_session) {
            return DbResultT<std::vector<std::shared_ptr<T>>>::fail(
                DbErrorCode::ConnectFail, "session is null");
        }
        return DbResultT<std::vector<std::shared_ptr<T>>>::success(list(q));
    }

    std::vector<std::shared_ptr<T>> whereEq(const std::string& col, const DbValue& val) {
        auto q = query();
        if (!q) return {};
        q->select()->whereEq(col, val);
        return list(q);
    }

    // ========================================================================
    // 分页
    // ========================================================================

    PageResult<T> findPage(PageInfo& page, DbQuery::ptr q = nullptr) {
        PageResult<T> result;
        result.page = page;
        if (!m_session) return result;

        if (!q) q = query();
        if (!q) return result;

        result.page.total = q->count();
        result.page.calcTotalPages();

        q->limit(page.pageSize)->offset(page.offset());
        result.rows = list(q);
        return result;
    }

    // ========================================================================
    // 批量操作
    // ========================================================================

    bool batchInsert(std::vector<T>& models) {
        if (!m_session || models.empty()) return false;
        T proto;
        std::vector<std::map<std::string, DbValue>> rows;
        for (auto& m : models) {
            DbRow row = m.toRow();
            if (m.isAutoIncrement()) {
                row.erase(m.primaryKey());
            }
            std::map<std::string, DbValue> values;
            for (const auto& kv : row) {
                values[kv.first] = DbValue::string(kv.second);
            }
            detail::applyAutoTimestampOnInsert(m, values);
            rows.push_back(values);
        }
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        return q->insertBatch(rows);
    }

    bool batchUpdate(const std::vector<T>& models) {
        if (!m_session || models.empty()) return false;
        bool ok = true;
        for (const auto& m : models) {
            if (!update(m)) ok = false;
        }
        return ok;
    }

    bool batchRemoveByIds(const std::vector<std::string>& ids) {
        if (!m_session || ids.empty()) return false;
        T proto;
        if (proto.isSoftDelete()) {
            auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
            DbValues values;
            for (const auto& id : ids) values.push_back(DbValue::string(id));
            q->whereIn(proto.primaryKey(), values);
            std::map<std::string, DbValue> upd;
            upd[proto.softDeleteColumn()] = DbValue::string(detail::nowTimestamp());
            return m_session->execute(q->buildUpdate(upd).first, q->buildUpdate(upd).second);
        }
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        DbValues values;
        for (const auto& id : ids) values.push_back(DbValue::string(id));
        q->whereIn(proto.primaryKey(), values);
        return q->remove();
    }

    // ========================================================================
    // 按条件更新/删除
    // ========================================================================

    bool updateWhere(DbQuery::ptr q, const std::map<std::string, DbValue>& values) {
        if (!m_session || !q) return false;
        auto p = q->buildUpdate(values);
        return m_session->execute(p.first, p.second);
    }

    bool removeWhere(DbQuery::ptr q) {
        if (!m_session || !q) return false;
        T proto;
        if (proto.isSoftDelete()) {
            std::map<std::string, DbValue> upd;
            upd[proto.softDeleteColumn()] = DbValue::string(detail::nowTimestamp());
            return m_session->execute(q->buildUpdate(upd).first, q->buildUpdate(upd).second);
        }
        auto p = q->buildDelete();
        return m_session->execute(p.first, p.second);
    }

    // ========================================================================
    // 聚合
    // ========================================================================

    uint64_t count() {
        if (!m_session) return 0;
        T proto;
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        detail::applySoftDeleteFilter(proto, q);
        return q->count();
    }

    DbValue sum(const std::string& column) {
        if (!m_session) return DbValue::null();
        T proto;
        auto q = std::make_shared<DbQuery>(m_session, proto.tableName());
        detail::applySoftDeleteFilter(proto, q);
        return q->sum(column);
    }

private:
    DbSession::ptr m_session;
    DbCache::ptr m_cache;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_REPOSITORY_H__

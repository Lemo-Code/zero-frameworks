/**
 * @file db_query.h
 * @brief 链式 SQL 构建器
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_QUERY_H__
#define __ZERO_DB_DB_QUERY_H__

#include <memory>
#include <string>
#include <vector>
#include <map>
#include "db_value.h"
#include "db_session.h"

namespace zero {
namespace db {

enum class SqlOp {
    EQ, NE, GT, GE, LT, LE, LIKE, NOT_LIKE, IN, NOT_IN, IS_NULL, IS_NOT_NULL, RAW
};

struct WhereClause {
    std::string column;
    SqlOp op;
    DbValue value;
    std::vector<DbValue> values; // for IN / NOT_IN
    std::string raw;             // 原始条件（RAW 模式）
    DbValues rawParams;          // 原始条件参数
    std::string conjunction;     // "AND" / "OR"
};

struct OrderByClause {
    std::string column;
    bool asc;
};

enum class LockMode { None, ForUpdate, LockInShareMode };

/**
 * @brief 链式 SQL 构建器
 */
class DbQuery : public std::enable_shared_from_this<DbQuery> {
public:
    typedef std::shared_ptr<DbQuery> ptr;

    explicit DbQuery(DbSession::ptr session);
    explicit DbQuery(DbSession::ptr session, const std::string& table);

    DbQuery::ptr table(const std::string& t);
    DbQuery::ptr from(const std::string& t) { return table(t); }

    // SELECT
    DbQuery::ptr select();
    DbQuery::ptr select(const std::vector<std::string>& columns);

    // WHERE
    DbQuery::ptr where(const std::string& column, const std::string& op, const DbValue& value);
    DbQuery::ptr where(const std::string& column, const DbValue& value);
    DbQuery::ptr whereEq(const std::string& column, const DbValue& value) { return where(column, "=", value); }
    DbQuery::ptr andWhere(const std::string& column, const std::string& op, const DbValue& value);
    DbQuery::ptr orWhere(const std::string& column, const std::string& op, const DbValue& value);
    DbQuery::ptr whereNull(const std::string& column);
    DbQuery::ptr whereNotNull(const std::string& column);
    DbQuery::ptr whereIn(const std::string& column, const DbValues& values);
    DbQuery::ptr whereNotIn(const std::string& column, const DbValues& values);
    DbQuery::ptr whereRaw(const std::string& raw, const DbValues& params = {});
    DbQuery::ptr andWhereRaw(const std::string& raw, const DbValues& params = {});
    DbQuery::ptr orWhereRaw(const std::string& raw, const DbValues& params = {});
    DbQuery::ptr whereBetween(const std::string& column, const DbValue& lo, const DbValue& hi);

    // HAVING
    DbQuery::ptr having(const std::string& raw, const DbValues& params = {});
    DbQuery::ptr andHaving(const std::string& raw, const DbValues& params = {});
    DbQuery::ptr orHaving(const std::string& raw, const DbValues& params = {});

    // ORDER / GROUP / LIMIT
    DbQuery::ptr orderBy(const std::string& column, bool asc = true);
    DbQuery::ptr orderByDesc(const std::string& column) { return orderBy(column, false); }
    DbQuery::ptr groupBy(const std::string& column);
    DbQuery::ptr limit(size_t n);
    DbQuery::ptr offset(size_t n);

    // JOIN
    DbQuery::ptr innerJoin(const std::string& table, const std::string& on);
    DbQuery::ptr leftJoin(const std::string& table, const std::string& on);
    DbQuery::ptr rightJoin(const std::string& table, const std::string& on);

    // LOCK
    DbQuery::ptr lockForUpdate();
    DbQuery::ptr sharedLock();

    // 执行
    DbResult get();
    std::shared_ptr<DbRow> first();
    DbValue value(const std::string& column);
    bool exists();

    bool insert(const std::map<std::string, DbValue>& values);
    bool insert(const DbRow& values);
    int64_t insertGetId(const std::map<std::string, DbValue>& values);
    bool insertBatch(const std::vector<std::map<std::string, DbValue>>& rows);
    bool insertIgnore(const std::map<std::string, DbValue>& values);
    bool upsert(const std::map<std::string, DbValue>& values,
                const std::vector<std::string>& updateColumns = {});

    bool update(const std::map<std::string, DbValue>& values);
    bool update(const DbRow& values);

    bool remove();

    size_t count(const std::string& column = "*");
    size_t countDistinct(const std::string& column);
    DbValue sum(const std::string& column);
    DbValue avg(const std::string& column);
    DbValue max(const std::string& column);
    DbValue min(const std::string& column);

    // SQL 构建
    std::pair<std::string, DbValues> buildSelect();
    std::pair<std::string, DbValues> buildInsert(const std::map<std::string, DbValue>& values);
    std::pair<std::string, DbValues> buildInsertBatch(const std::vector<std::map<std::string, DbValue>>& rows);
    std::pair<std::string, DbValues> buildInsertIgnore(const std::map<std::string, DbValue>& values);
    std::pair<std::string, DbValues> buildUpsert(const std::map<std::string, DbValue>& values,
                                                 const std::vector<std::string>& updateColumns);
    std::pair<std::string, DbValues> buildUpdate(const std::map<std::string, DbValue>& values);
    std::pair<std::string, DbValues> buildDelete();
    std::pair<std::string, DbValues> buildCount(const std::string& column);
    std::pair<std::string, DbValues> buildAggregate(const std::string& agg, const std::string& column);

private:
    void addWhere(const std::string& column, SqlOp op, const DbValue& value,
                  const std::string& conjunction);
    void addWhereRaw(const std::string& raw, const DbValues& params, const std::string& conjunction);
    void addHaving(const std::string& raw, const DbValues& params, const std::string& conjunction);
    std::string escapeColumn(const std::string& col);
    std::string escapeTable(const std::string& t);
    std::string opToString(SqlOp op) const;
    SqlOp parseOp(const std::string& op) const;
    void buildWhere(std::ostringstream& sql, DbValues& params);
    void buildOrderBy(std::ostringstream& sql);
    void buildGroupBy(std::ostringstream& sql, DbValues& params);
    void buildLimitOffset(std::ostringstream& sql);
    void buildJoins(std::ostringstream& sql);
    void buildLock(std::ostringstream& sql);

    DbSession::ptr m_session;
    std::string m_table;
    std::vector<std::string> m_columns;
    std::vector<WhereClause> m_wheres;
    std::vector<WhereClause> m_havings;
    std::vector<std::pair<std::string, std::string>> m_joins; // type, sql
    std::vector<OrderByClause> m_orderBy;
    std::vector<std::string> m_groupBy;
    size_t m_limit = 0;
    size_t m_offset = 0;
    LockMode m_lock = LockMode::None;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_QUERY_H__

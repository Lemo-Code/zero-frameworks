/**
 * @file db_query.cc
 * @brief 链式 SQL 构建器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "db_query.h"
#include "zero/core/log/log.h"
#include <sstream>
#include <set>

namespace zero {
namespace db {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

DbQuery::DbQuery(DbSession::ptr session)
    : m_session(session) {
}

DbQuery::DbQuery(DbSession::ptr session, const std::string& table)
    : m_session(session), m_table(table) {
}

DbQuery::ptr DbQuery::table(const std::string& t) {
    m_table = t;
    return shared_from_this();
}

DbQuery::ptr DbQuery::select() {
    m_columns.clear();
    return shared_from_this();
}

DbQuery::ptr DbQuery::select(const std::vector<std::string>& columns) {
    m_columns = columns;
    return shared_from_this();
}

// ============================================================================
// WHERE
// ============================================================================

void DbQuery::addWhere(const std::string& column, SqlOp op, const DbValue& value,
                       const std::string& conjunction) {
    WhereClause w;
    w.column = column;
    w.op = op;
    w.value = value;
    w.conjunction = m_wheres.empty() ? "" : conjunction;
    m_wheres.push_back(w);
}

void DbQuery::addWhereRaw(const std::string& raw, const DbValues& params, const std::string& conjunction) {
    WhereClause w;
    w.op = SqlOp::RAW;
    w.raw = raw;
    w.rawParams = params;
    w.conjunction = m_wheres.empty() ? "" : conjunction;
    m_wheres.push_back(w);
}

DbQuery::ptr DbQuery::where(const std::string& column, const std::string& op, const DbValue& value) {
    addWhere(column, parseOp(op), value, "AND");
    return shared_from_this();
}

DbQuery::ptr DbQuery::where(const std::string& column, const DbValue& value) {
    return where(column, "=", value);
}

DbQuery::ptr DbQuery::andWhere(const std::string& column, const std::string& op, const DbValue& value) {
    return where(column, op, value);
}

DbQuery::ptr DbQuery::orWhere(const std::string& column, const std::string& op, const DbValue& value) {
    addWhere(column, parseOp(op), value, "OR");
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereNull(const std::string& column) {
    addWhere(column, SqlOp::IS_NULL, DbValue::null(), "AND");
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereNotNull(const std::string& column) {
    addWhere(column, SqlOp::IS_NOT_NULL, DbValue::null(), "AND");
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereIn(const std::string& column, const DbValues& values) {
    WhereClause w;
    w.column = column;
    w.op = SqlOp::IN;
    w.values = values;
    w.conjunction = m_wheres.empty() ? "" : "AND";
    m_wheres.push_back(w);
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereNotIn(const std::string& column, const DbValues& values) {
    WhereClause w;
    w.column = column;
    w.op = SqlOp::NOT_IN;
    w.values = values;
    w.conjunction = m_wheres.empty() ? "" : "AND";
    m_wheres.push_back(w);
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereRaw(const std::string& raw, const DbValues& params) {
    addWhereRaw(raw, params, "AND");
    return shared_from_this();
}

DbQuery::ptr DbQuery::andWhereRaw(const std::string& raw, const DbValues& params) {
    return whereRaw(raw, params);
}

DbQuery::ptr DbQuery::orWhereRaw(const std::string& raw, const DbValues& params) {
    addWhereRaw(raw, params, "OR");
    return shared_from_this();
}

DbQuery::ptr DbQuery::whereBetween(const std::string& column, const DbValue& lo, const DbValue& hi) {
    addWhereRaw(escapeColumn(column) + " BETWEEN ? AND ?", {lo, hi}, "AND");
    return shared_from_this();
}

// ============================================================================
// HAVING
// ============================================================================

void DbQuery::addHaving(const std::string& raw, const DbValues& params, const std::string& conjunction) {
    WhereClause w;
    w.op = SqlOp::RAW;
    w.raw = raw;
    w.rawParams = params;
    w.conjunction = m_havings.empty() ? "" : conjunction;
    m_havings.push_back(w);
}

DbQuery::ptr DbQuery::having(const std::string& raw, const DbValues& params) {
    addHaving(raw, params, "AND");
    return shared_from_this();
}

DbQuery::ptr DbQuery::andHaving(const std::string& raw, const DbValues& params) {
    return having(raw, params);
}

DbQuery::ptr DbQuery::orHaving(const std::string& raw, const DbValues& params) {
    addHaving(raw, params, "OR");
    return shared_from_this();
}

// ============================================================================
// ORDER / GROUP / LIMIT
// ============================================================================

DbQuery::ptr DbQuery::orderBy(const std::string& column, bool asc) {
    m_orderBy.push_back({escapeColumn(column), asc});
    return shared_from_this();
}

DbQuery::ptr DbQuery::groupBy(const std::string& column) {
    m_groupBy.push_back(escapeColumn(column));
    return shared_from_this();
}

DbQuery::ptr DbQuery::limit(size_t n) {
    m_limit = n;
    return shared_from_this();
}

DbQuery::ptr DbQuery::offset(size_t n) {
    m_offset = n;
    return shared_from_this();
}

// ============================================================================
// JOIN
// ============================================================================

DbQuery::ptr DbQuery::innerJoin(const std::string& table, const std::string& on) {
    m_joins.push_back({"INNER JOIN", escapeTable(table) + " ON " + on});
    return shared_from_this();
}

DbQuery::ptr DbQuery::leftJoin(const std::string& table, const std::string& on) {
    m_joins.push_back({"LEFT JOIN", escapeTable(table) + " ON " + on});
    return shared_from_this();
}

DbQuery::ptr DbQuery::rightJoin(const std::string& table, const std::string& on) {
    m_joins.push_back({"RIGHT JOIN", escapeTable(table) + " ON " + on});
    return shared_from_this();
}

// ============================================================================
// LOCK
// ============================================================================

DbQuery::ptr DbQuery::lockForUpdate() {
    m_lock = LockMode::ForUpdate;
    return shared_from_this();
}

DbQuery::ptr DbQuery::sharedLock() {
    m_lock = LockMode::LockInShareMode;
    return shared_from_this();
}

// ============================================================================
// 执行
// ============================================================================

DbResult DbQuery::get() {
    if(!m_session) return DbResult();
    auto p = buildSelect();
    return m_session->query(p.first, p.second);
}

std::shared_ptr<DbRow> DbQuery::first() {
    limit(1);
    auto r = get();
    if(r.empty()) return nullptr;
    return std::shared_ptr<DbRow>(new DbRow(r[0]));
}

DbValue DbQuery::value(const std::string& column) {
    auto row = first();
    if(!row) return DbValue::null();
    auto it = row->find(column);
    if(it == row->end()) return DbValue::null();
    return DbValue::string(it->second);
}

bool DbQuery::exists() {
    if(!m_session) return false;
    auto p = buildCount("*");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return false;
    auto it = r[0].begin();
    if(it == r[0].end()) return false;
    try {
        return std::stoull(it->second) > 0;
    } catch(...) {
        return false;
    }
}

bool DbQuery::insert(const std::map<std::string, DbValue>& values) {
    if(!m_session) return false;
    auto p = buildInsert(values);
    return m_session->execute(p.first, p.second);
}

bool DbQuery::insert(const DbRow& values) {
    std::map<std::string, DbValue> m;
    for(auto& kv : values) {
        m[kv.first] = DbValue::string(kv.second);
    }
    return insert(m);
}

int64_t DbQuery::insertGetId(const std::map<std::string, DbValue>& values) {
    if(!insert(values)) return 0;
    return m_session->lastInsertId();
}

bool DbQuery::insertBatch(const std::vector<std::map<std::string, DbValue>>& rows) {
    if(!m_session || rows.empty()) return false;
    auto p = buildInsertBatch(rows);
    return m_session->execute(p.first, p.second);
}

bool DbQuery::insertIgnore(const std::map<std::string, DbValue>& values) {
    if(!m_session) return false;
    auto p = buildInsertIgnore(values);
    return m_session->execute(p.first, p.second);
}

bool DbQuery::upsert(const std::map<std::string, DbValue>& values,
                     const std::vector<std::string>& updateColumns) {
    if(!m_session) return false;
    auto p = buildUpsert(values, updateColumns);
    return m_session->execute(p.first, p.second);
}

bool DbQuery::update(const std::map<std::string, DbValue>& values) {
    if(!m_session) return false;
    auto p = buildUpdate(values);
    return m_session->execute(p.first, p.second);
}

bool DbQuery::update(const DbRow& values) {
    std::map<std::string, DbValue> m;
    for(auto& kv : values) {
        m[kv.first] = DbValue::string(kv.second);
    }
    return update(m);
}

bool DbQuery::remove() {
    if(!m_session) return false;
    auto p = buildDelete();
    return m_session->execute(p.first, p.second);
}

size_t DbQuery::count(const std::string& column) {
    if(!m_session) return 0;
    auto p = buildCount(column);
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return 0;
    auto it = r[0].begin();
    if(it == r[0].end()) return 0;
    try {
        return static_cast<size_t>(std::stoull(it->second));
    } catch(...) {
        return 0;
    }
}

size_t DbQuery::countDistinct(const std::string& column) {
    if(!m_session) return 0;
    auto p = buildAggregate("COUNT(DISTINCT ", column + ")");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return 0;
    auto it = r[0].begin();
    if(it == r[0].end()) return 0;
    try {
        return static_cast<size_t>(std::stoull(it->second));
    } catch(...) {
        return 0;
    }
}

DbValue DbQuery::sum(const std::string& column) {
    if(!m_session) return DbValue::null();
    auto p = buildAggregate("SUM(", column + ")");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return DbValue::null();
    auto it = r[0].begin();
    if(it == r[0].end()) return DbValue::null();
    return DbValue::string(it->second);
}

DbValue DbQuery::avg(const std::string& column) {
    if(!m_session) return DbValue::null();
    auto p = buildAggregate("AVG(", column + ")");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return DbValue::null();
    auto it = r[0].begin();
    if(it == r[0].end()) return DbValue::null();
    return DbValue::string(it->second);
}

DbValue DbQuery::max(const std::string& column) {
    if(!m_session) return DbValue::null();
    auto p = buildAggregate("MAX(", column + ")");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return DbValue::null();
    auto it = r[0].begin();
    if(it == r[0].end()) return DbValue::null();
    return DbValue::string(it->second);
}

DbValue DbQuery::min(const std::string& column) {
    if(!m_session) return DbValue::null();
    auto p = buildAggregate("MIN(", column + ")");
    auto r = m_session->query(p.first, p.second);
    if(r.empty()) return DbValue::null();
    auto it = r[0].begin();
    if(it == r[0].end()) return DbValue::null();
    return DbValue::string(it->second);
}

// ============================================================================
// SQL 构建
// ============================================================================

std::pair<std::string, DbValues> DbQuery::buildSelect() {
    std::ostringstream sql;
    DbValues params;

    sql << "SELECT ";
    if(m_columns.empty()) {
        sql << "*";
    } else {
        for(size_t i = 0; i < m_columns.size(); ++i) {
            if(i > 0) sql << ", ";
            sql << escapeColumn(m_columns[i]);
        }
    }

    sql << " FROM " << escapeTable(m_table);
    buildJoins(sql);
    buildWhere(sql, params);
    buildGroupBy(sql, params);
    buildOrderBy(sql);
    buildLimitOffset(sql);
    buildLock(sql);

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildInsert(const std::map<std::string, DbValue>& values) {
    std::ostringstream sql;
    DbValues params;

    sql << "INSERT INTO " << escapeTable(m_table) << " (";
    bool first = true;
    for(auto& kv : values) {
        if(!first) sql << ", ";
        sql << escapeColumn(kv.first);
        first = false;
    }
    sql << ") VALUES (";
    first = true;
    for(auto& kv : values) {
        if(!first) sql << ", ";
        sql << "?";
        params.push_back(kv.second);
        first = false;
    }
    sql << ")";

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildInsertBatch(const std::vector<std::map<std::string, DbValue>>& rows) {
    std::ostringstream sql;
    DbValues params;

    if(rows.empty()) return {std::string(), params};

    // 以第一行确定列顺序
    std::vector<std::string> columns;
    for(auto& kv : rows[0]) {
        columns.push_back(kv.first);
    }

    sql << "INSERT INTO " << escapeTable(m_table) << " (";
    for(size_t i = 0; i < columns.size(); ++i) {
        if(i > 0) sql << ", ";
        sql << escapeColumn(columns[i]);
    }
    sql << ") VALUES ";

    for(size_t r = 0; r < rows.size(); ++r) {
        if(r > 0) sql << ", ";
        sql << "(";
        for(size_t i = 0; i < columns.size(); ++i) {
            if(i > 0) sql << ", ";
            sql << "?";
            auto it = rows[r].find(columns[i]);
            params.push_back(it != rows[r].end() ? it->second : DbValue::null());
        }
        sql << ")";
    }

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildInsertIgnore(const std::map<std::string, DbValue>& values) {
    auto p = buildInsert(values);
    p.first += " ON DUPLICATE KEY UPDATE id=id"; // MySQL 占位，保证忽略
    return p;
}

std::pair<std::string, DbValues> DbQuery::buildUpsert(const std::map<std::string, DbValue>& values,
                                                      const std::vector<std::string>& updateColumns) {
    auto p = buildInsert(values);
    std::ostringstream sql;
    sql << p.first << " ON DUPLICATE KEY UPDATE ";

    std::vector<std::string> cols = updateColumns;
    if(cols.empty()) {
        for(auto& kv : values) {
            cols.push_back(kv.first);
        }
    }

    for(size_t i = 0; i < cols.size(); ++i) {
        if(i > 0) sql << ", ";
        sql << escapeColumn(cols[i]) << " = VALUES(" << escapeColumn(cols[i]) << ")";
    }

    return {sql.str(), p.second};
}

std::pair<std::string, DbValues> DbQuery::buildUpdate(const std::map<std::string, DbValue>& values) {
    std::ostringstream sql;
    DbValues params;

    sql << "UPDATE " << escapeTable(m_table) << " SET ";
    bool first = true;
    for(auto& kv : values) {
        if(!first) sql << ", ";
        sql << escapeColumn(kv.first) << " = ?";
        params.push_back(kv.second);
        first = false;
    }

    buildWhere(sql, params);
    buildLimitOffset(sql);

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildDelete() {
    std::ostringstream sql;
    DbValues params;

    sql << "DELETE FROM " << escapeTable(m_table);
    buildWhere(sql, params);
    buildLimitOffset(sql);

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildCount(const std::string& column) {
    std::ostringstream sql;
    DbValues params;

    sql << "SELECT COUNT(" << escapeColumn(column) << ") AS cnt FROM " << escapeTable(m_table);
    buildJoins(sql);
    buildWhere(sql, params);

    return {sql.str(), params};
}

std::pair<std::string, DbValues> DbQuery::buildAggregate(const std::string& agg, const std::string& column) {
    std::ostringstream sql;
    DbValues params;

    sql << "SELECT " << agg << escapeColumn(column) << " AS agg FROM " << escapeTable(m_table);
    buildJoins(sql);
    buildWhere(sql, params);

    return {sql.str(), params};
}

// ============================================================================
// 辅助构建方法
// ============================================================================

void DbQuery::buildWhere(std::ostringstream& sql, DbValues& params) {
    if(m_wheres.empty()) return;

    sql << " WHERE ";
    for(size_t i = 0; i < m_wheres.size(); ++i) {
        auto& w = m_wheres[i];
        if(i > 0) {
            sql << " " << (w.conjunction.empty() ? "AND" : w.conjunction) << " ";
        }

        if(w.op == SqlOp::RAW) {
            sql << "(" << w.raw << ")";
            params.insert(params.end(), w.rawParams.begin(), w.rawParams.end());
            continue;
        }

        switch(w.op) {
            case SqlOp::IS_NULL:
                sql << escapeColumn(w.column) << " IS NULL";
                break;
            case SqlOp::IS_NOT_NULL:
                sql << escapeColumn(w.column) << " IS NOT NULL";
                break;
            case SqlOp::IN:
            case SqlOp::NOT_IN: {
                sql << escapeColumn(w.column) << " " << opToString(w.op) << " (";
                for(size_t j = 0; j < w.values.size(); ++j) {
                    if(j > 0) sql << ", ";
                    sql << "?";
                    params.push_back(w.values[j]);
                }
                sql << ")";
                break;
            }
            default:
                sql << escapeColumn(w.column) << " " << opToString(w.op) << " ?";
                params.push_back(w.value);
                break;
        }
    }
}

void DbQuery::buildOrderBy(std::ostringstream& sql) {
    if(m_orderBy.empty()) return;
    sql << " ORDER BY ";
    for(size_t i = 0; i < m_orderBy.size(); ++i) {
        if(i > 0) sql << ", ";
        sql << m_orderBy[i].column << (m_orderBy[i].asc ? " ASC" : " DESC");
    }
}

void DbQuery::buildGroupBy(std::ostringstream& sql, DbValues& params) {
    if(m_groupBy.empty()) return;
    sql << " GROUP BY ";
    for(size_t i = 0; i < m_groupBy.size(); ++i) {
        if(i > 0) sql << ", ";
        sql << m_groupBy[i];
    }
    if(!m_havings.empty()) {
        sql << " HAVING ";
        for(size_t i = 0; i < m_havings.size(); ++i) {
            auto& h = m_havings[i];
            if(i > 0) {
                sql << " " << (h.conjunction.empty() ? "AND" : h.conjunction) << " ";
            }
            sql << "(" << h.raw << ")";
            for(auto& p : h.rawParams) params.push_back(p);
        }
    }
}

void DbQuery::buildLimitOffset(std::ostringstream& sql) {
    if(m_limit > 0) {
        sql << " LIMIT " << m_limit;
    }
    if(m_offset > 0) {
        sql << " OFFSET " << m_offset;
    }
}

void DbQuery::buildJoins(std::ostringstream& sql) {
    for(auto& join : m_joins) {
        sql << " " << join.first << " " << join.second;
    }
}

void DbQuery::buildLock(std::ostringstream& sql) {
    if(m_lock == LockMode::ForUpdate) {
        sql << " FOR UPDATE";
    } else if(m_lock == LockMode::LockInShareMode) {
        sql << " LOCK IN SHARE MODE";
    }
}

// ============================================================================
// 转义与操作符
// ============================================================================

std::string DbQuery::escapeColumn(const std::string& col) {
    if(col.empty()) return col;
    if(col == "*") return col;
    if(col.find('(') != std::string::npos) return col;
    if(col.find('.') != std::string::npos) {
        // table.column 形式
        std::string r;
        std::stringstream ss(col);
        std::string part;
        bool first = true;
        while(std::getline(ss, part, '.')) {
            if(!first) r += ".";
            r += escapeColumn(part);
            first = false;
        }
        return r;
    }
    std::string r = "`";
    for(char c : col) {
        if(c == '`') r += '`';
        r += c;
    }
    r += "`";
    return r;
}

std::string DbQuery::escapeTable(const std::string& t) {
    return escapeColumn(t);
}

SqlOp DbQuery::parseOp(const std::string& op) const {
    if(op == "=" || op == "==") return SqlOp::EQ;
    if(op == "!=" || op == "<>") return SqlOp::NE;
    if(op == ">") return SqlOp::GT;
    if(op == ">=") return SqlOp::GE;
    if(op == "<") return SqlOp::LT;
    if(op == "<=") return SqlOp::LE;
    if(op == "like" || op == "LIKE") return SqlOp::LIKE;
    if(op == "not like" || op == "NOT LIKE") return SqlOp::NOT_LIKE;
    if(op == "in" || op == "IN") return SqlOp::IN;
    if(op == "not in" || op == "NOT IN") return SqlOp::NOT_IN;
    return SqlOp::EQ;
}

std::string DbQuery::opToString(SqlOp op) const {
    switch(op) {
        case SqlOp::EQ: return "=";
        case SqlOp::NE: return "!=";
        case SqlOp::GT: return ">";
        case SqlOp::GE: return ">=";
        case SqlOp::LT: return "<";
        case SqlOp::LE: return "<=";
        case SqlOp::LIKE: return "LIKE";
        case SqlOp::NOT_LIKE: return "NOT LIKE";
        case SqlOp::IN: return "IN";
        case SqlOp::NOT_IN: return "NOT IN";
        case SqlOp::IS_NULL: return "IS NULL";
        case SqlOp::IS_NOT_NULL: return "IS NOT NULL";
        case SqlOp::RAW: return "";
    }
    return "=";
}

} // namespace db
} // namespace zero

/**
 * @file db_result.h
 * @brief 统一数据库错误码与结果类型
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_RESULT_H__
#define __ZERO_DB_DB_RESULT_H__

#include <string>
#include <memory>
#include <vector>
#include "db_value.h"

namespace zero {
namespace db {

/**
 * @brief 数据库错误码
 */
enum class DbErrorCode {
    None = 0,
    ConnectFail,
    SqlError,
    Timeout,
    NotFound,
    DuplicateKey,
    ConstraintViolation,
    InvalidArgument,
    Unknown
};

/**
 * @brief 数据库错误详情
 */
struct DbError {
    DbErrorCode code = DbErrorCode::None;
    std::string message;
    std::string sql;
    int mysqlErrno = 0;

    bool ok() const { return code == DbErrorCode::None; }
    explicit operator bool() const { return ok(); }
};

/**
 * @brief 通用数据库结果（含错误信息）
 */
template<typename T>
struct DbResultT {
    DbError error;
    T data;

    bool ok() const { return error.ok(); }
    explicit operator bool() const { return ok(); }

    static DbResultT<T> success(T&& value) {
        DbResultT<T> r;
        r.data = std::move(value);
        return r;
    }

    static DbResultT<T> success(const T& value) {
        DbResultT<T> r;
        r.data = value;
        return r;
    }

    static DbResultT<T> fail(DbErrorCode code, const std::string& msg,
                             const std::string& sql = "", int mysqlErrno = 0) {
        DbResultT<T> r;
        r.error.code = code;
        r.error.message = msg;
        r.error.sql = sql;
        r.error.mysqlErrno = mysqlErrno;
        return r;
    }
};

/**
 * @brief 分页信息
 */
struct PageInfo {
    size_t page = 1;
    size_t pageSize = 20;
    size_t total = 0;
    size_t totalPages = 0;

    size_t offset() const { return (page > 0 ? page - 1 : 0) * pageSize; }

    void calcTotalPages() {
        totalPages = pageSize > 0 ? (total + pageSize - 1) / pageSize : 0;
    }
};

template<typename T>
struct PageResult {
    PageInfo page;
    std::vector<std::shared_ptr<T>> rows;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_RESULT_H__

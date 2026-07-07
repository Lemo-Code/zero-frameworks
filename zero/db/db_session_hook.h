/**
 * @file db_session_hook.h
 * @brief SQL 审计/监控钩子接口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_SESSION_HOOK_H__
#define __ZERO_DB_DB_SESSION_HOOK_H__

#include <string>
#include <memory>
#include <vector>
#include <map>
#include "db_value.h"

namespace zero {
namespace db {

/**
 * @brief SQL 执行钩子接口
 *
 * 可用于：慢查询日志、SQL 审计、指标上报、参数打印、权限检查。
 */
class DbSessionHook {
public:
    typedef std::shared_ptr<DbSessionHook> ptr;

    virtual ~DbSessionHook() = default;

    /**
     * @brief execute 执行前
     */
    virtual void beforeExecute(const std::string& sql, const DbValues& params) {
        (void)sql; (void)params;
    }

    /**
     * @brief execute 执行后
     * @param costMs 耗时毫秒（由实现层传入，钩子可忽略）
     */
    virtual void afterExecute(const std::string& sql, const DbValues& params,
                              bool success, uint64_t costMs) {
        (void)sql; (void)params; (void)success; (void)costMs;
    }

    /**
     * @brief query 执行前
     */
    virtual void beforeQuery(const std::string& sql, const DbValues& params) {
        (void)sql; (void)params;
    }

    /**
     * @brief query 执行后
     */
    virtual void afterQuery(const std::string& sql, const DbValues& params,
                            const std::vector<std::map<std::string, std::string>>& result,
                            uint64_t costMs) {
        (void)sql; (void)params; (void)result; (void)costMs;
    }
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_SESSION_HOOK_H__

/**
 * @file db_validator.h
 * @brief ORM 模型校验器
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_VALIDATOR_H__
#define __ZERO_DB_DB_VALIDATOR_H__

#include <string>
#include <vector>
#include <regex>
#include <cstdint>
#include <cctype>
#include "db_value.h"

namespace zero {
namespace db {

/**
 * @brief 校验规则类型
 */
enum class DbValidateRule {
    NotNull,
    NotEmpty,
    MaxLength,
    MinLength,
    Min,
    Max,
    Range,
    Regex,
    Email,
    Custom
};

/**
 * @brief 校验错误
 */
struct DbValidationError {
    std::vector<std::string> errors;

    bool ok() const { return errors.empty(); }
    explicit operator bool() const { return ok(); }

    std::string message() const {
        std::string r;
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i > 0) r += "; ";
            r += errors[i];
        }
        return r;
    }
};

/**
 * @brief 字段校验规则
 */
struct DbValidator {
    std::string column;
    DbValidateRule rule;
    std::string pattern;   // Regex / Custom
    double min = 0;        // Min / Range
    double max = 0;        // Max / Range
    size_t maxLength = 0;  // MaxLength
    size_t minLength = 0;  // MinLength
    std::string message;
    std::function<bool(const std::string&)> customFn;

    DbValidator() = default;
    DbValidator(const std::string& col, DbValidateRule r, const std::string& msg)
        : column(col), rule(r), message(msg) {}
};

namespace detail {

inline bool isEmail(const std::string& s) {
    if (s.empty()) return false;
    size_t at = s.find('@');
    if (at == std::string::npos || at == 0 || at == s.size() - 1) return false;
    size_t dot = s.find('.', at + 1);
    return dot != std::string::npos && dot != s.size() - 1;
}

inline bool validateOne(const std::string& value, const DbValidator& v, DbValidationError& out) {
    switch (v.rule) {
        case DbValidateRule::NotNull:
            if (value.empty()) {
                out.errors.push_back(v.message.empty() ? v.column + " cannot be null" : v.message);
                return false;
            }
            break;
        case DbValidateRule::NotEmpty: {
            bool empty = true;
            for (char c : value) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    empty = false;
                    break;
                }
            }
            if (empty) {
                out.errors.push_back(v.message.empty() ? v.column + " cannot be empty" : v.message);
                return false;
            }
            break;
        }
        case DbValidateRule::MaxLength:
            if (value.size() > v.maxLength) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " length exceeds " + std::to_string(v.maxLength)
                    : v.message);
                return false;
            }
            break;
        case DbValidateRule::MinLength:
            if (value.size() < v.minLength) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " length less than " + std::to_string(v.minLength)
                    : v.message);
                return false;
            }
            break;
        case DbValidateRule::Min: {
            double d = DbValue::string(value).toDouble();
            if (d < v.min) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " must be >= " + std::to_string(v.min)
                    : v.message);
                return false;
            }
            break;
        }
        case DbValidateRule::Max: {
            double d = DbValue::string(value).toDouble();
            if (d > v.max) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " must be <= " + std::to_string(v.max)
                    : v.message);
                return false;
            }
            break;
        }
        case DbValidateRule::Range: {
            double d = DbValue::string(value).toDouble();
            if (d < v.min || d > v.max) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " must be in [" + std::to_string(v.min) + ", " + std::to_string(v.max) + "]"
                    : v.message);
                return false;
            }
            break;
        }
        case DbValidateRule::Regex:
            if (!std::regex_match(value, std::regex(v.pattern))) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " format invalid"
                    : v.message);
                return false;
            }
            break;
        case DbValidateRule::Email:
            if (!isEmail(value)) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " is not a valid email"
                    : v.message);
                return false;
            }
            break;
        case DbValidateRule::Custom:
            if (v.customFn && !v.customFn(value)) {
                out.errors.push_back(v.message.empty()
                    ? v.column + " custom validation failed"
                    : v.message);
                return false;
            }
            break;
    }
    return true;
}

} // namespace detail

/**
 * @brief 校验字段集合
 */
inline DbValidationError validateFields(const std::map<std::string, std::string>& row,
                                         const std::vector<DbValidator>& validators) {
    DbValidationError err;
    for (const auto& v : validators) {
        auto it = row.find(v.column);
        std::string value = (it != row.end()) ? it->second : "";
        detail::validateOne(value, v, err);
    }
    return err;
}

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_VALIDATOR_H__

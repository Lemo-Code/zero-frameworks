/**
 * @file db_value.h
 * @brief 数据库通用值类型
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_VALUE_H__
#define __ZERO_DB_DB_VALUE_H__

#include <string>
#include <vector>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace zero {
namespace db {

/**
 * @brief 数据库值类型
 */
enum class DbValueType {
    Null,
    Int64,
    UInt64,
    Double,
    String,
    Blob
};

/**
 * @brief 数据库通用值
 * 
 * 用于 SQL 参数绑定和结果字段存储。
 * 兼容 C++14，使用 union + std::string。
 */
struct DbValue {
    DbValueType type = DbValueType::Null;
    union {
        int64_t i64 = 0;
        uint64_t u64;
        double d;
    } value;
    std::string str;

    DbValue() = default;

    static DbValue null() {
        DbValue v;
        v.type = DbValueType::Null;
        return v;
    }

    static DbValue int64(int64_t v) {
        DbValue r;
        r.type = DbValueType::Int64;
        r.value.i64 = v;
        return r;
    }

    static DbValue uint64(uint64_t v) {
        DbValue r;
        r.type = DbValueType::UInt64;
        r.value.u64 = v;
        return r;
    }

    static DbValue double_(double v) {
        DbValue r;
        r.type = DbValueType::Double;
        r.value.d = v;
        return r;
    }

    static DbValue string(const std::string& v) {
        DbValue r;
        r.type = DbValueType::String;
        r.str = v;
        return r;
    }

    static DbValue string(const char* v) {
        DbValue r;
        r.type = DbValueType::String;
        r.str = v ? v : "";
        return r;
    }

    static DbValue blob(const std::string& v) {
        DbValue r;
        r.type = DbValueType::Blob;
        r.str = v;
        return r;
    }

    static DbValue blob(const void* data, size_t len) {
        DbValue r;
        r.type = DbValueType::Blob;
        if(data && len > 0) {
            r.str.assign(static_cast<const char*>(data), len);
        }
        return r;
    }

    /**
     * @brief 从字符串解析，按类型转换
     */
    static DbValue parse(const std::string& s, DbValueType t) {
        switch(t) {
            case DbValueType::Null: return null();
            case DbValueType::Int64: return int64(std::stoll(s));
            case DbValueType::UInt64: return uint64(std::stoull(s));
            case DbValueType::Double: return double_(std::stod(s));
            case DbValueType::String:
            case DbValueType::Blob: return string(s);
        }
        return null();
    }

    bool isNull() const { return type == DbValueType::Null; }

    int64_t toInt64(int64_t defaultVal = 0) const {
        if(type == DbValueType::Int64) return value.i64;
        if(type == DbValueType::UInt64) return static_cast<int64_t>(value.u64);
        if(type == DbValueType::Double) return static_cast<int64_t>(value.d);
        if(type == DbValueType::String || type == DbValueType::Blob) {
            try { return std::stoll(str); } catch(...) {}
        }
        return defaultVal;
    }

    uint64_t toUInt64(uint64_t defaultVal = 0) const {
        if(type == DbValueType::UInt64) return value.u64;
        if(type == DbValueType::Int64) return static_cast<uint64_t>(value.i64);
        if(type == DbValueType::Double) return static_cast<uint64_t>(value.d);
        if(type == DbValueType::String || type == DbValueType::Blob) {
            try { return std::stoull(str); } catch(...) {}
        }
        return defaultVal;
    }

    double toDouble(double defaultVal = 0.0) const {
        if(type == DbValueType::Double) return value.d;
        if(type == DbValueType::Int64) return static_cast<double>(value.i64);
        if(type == DbValueType::UInt64) return static_cast<double>(value.u64);
        if(type == DbValueType::String || type == DbValueType::Blob) {
            try { return std::stod(str); } catch(...) {}
        }
        return defaultVal;
    }

    std::string toString(const std::string& defaultVal = "") const {
        if(type == DbValueType::String || type == DbValueType::Blob) return str;
        if(type == DbValueType::Int64) return std::to_string(value.i64);
        if(type == DbValueType::UInt64) return std::to_string(value.u64);
        if(type == DbValueType::Double) {
            std::ostringstream oss;
            oss << std::setprecision(15) << value.d;
            return oss.str();
        }
        return defaultVal;
    }

    /**
     * @brief 返回 SQL 字面量（仅用于日志/调试，不用于拼接 SQL）
     */
    std::string toSqlLiteral() const {
        if(type == DbValueType::Null) return "NULL";
        if(type == DbValueType::Int64) return std::to_string(value.i64);
        if(type == DbValueType::UInt64) return std::to_string(value.u64);
        if(type == DbValueType::Double) {
            std::ostringstream oss;
            oss << std::setprecision(15) << value.d;
            return oss.str();
        }
        // String / Blob 做简单转义展示
        std::string s = "'";
        for(char c : str) {
            if(c == '\\' || c == '\'') s += '\\';
            s += c;
        }
        s += "'";
        return s;
    }
};

/**
 * @brief 参数列表便捷类型
 */
typedef std::vector<DbValue> DbValues;

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_VALUE_H__

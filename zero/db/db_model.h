/**
 * @file db_model.h
 * @brief ORM 模型基类与声明式宏
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_MODEL_H__
#define __ZERO_DB_DB_MODEL_H__

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <functional>
#include <chrono>
#include <ctime>
#include "db_value.h"
#include "db_validator.h"

namespace zero {
namespace db {

class DbModel;

/**
 * @brief 字段约束标志
 */
enum DbFieldFlag {
    None = 0,
    PrimaryKey = 1 << 0,
    AutoIncrement = 1 << 1,
    NotNull = 1 << 2,
    Unique = 1 << 3,
    Index = 1 << 4,
    AutoNow = 1 << 5,       // 自动填充当前时间（update）
    AutoNowAdd = 1 << 6     // 自动填充当前时间（insert）
};

/**
 * @brief 模型字段元数据
 */
struct DbModelField {
    std::string memberName;
    std::string columnName;
    int flags;
    DbValueType type;
    size_t maxLength = 0;   // 0 表示未限制
    std::string defaultValue;
};

/**
 * @brief 数据库行
 */
typedef std::map<std::string, std::string> DbRow;

namespace detail {

inline std::string nowTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

inline int64_t parseInt64(const std::string& s) {
    try { return std::stoll(s); } catch(...) { return 0; }
}

inline uint64_t parseUInt64(const std::string& s) {
    try { return std::stoull(s); } catch(...) { return 0; }
}

inline double parseDouble(const std::string& s) {
    try { return std::stod(s); } catch(...) { return 0.0; }
}

inline bool parseBool(const std::string& s) {
    return s == "1" || s == "true" || s == "TRUE" || s == "True";
}

inline std::string doubleToString(double v) {
    std::ostringstream oss;
    oss << std::setprecision(15) << v;
    return oss.str();
}

} // namespace detail

/**
 * @brief 字段绑定器（不持有 this 指针，对象可安全拷贝）
 */
struct DbFieldBinder {
    std::string column;
    DbValueType type;
    std::function<std::string(DbModel*)> getter;
    std::function<void(DbModel*, const std::string&)> setter;
};

/**
 * @brief ORM 模型基类
 *
 * 派生类通过 ZERO_DB_MODEL / ZERO_DB_FIELD_* / ZERO_DB_MODEL_END 宏声明字段，
 * 框架自动生成 toRow/fromRow/fields/tableName/主键相关方法。
 */
class DbModel {
public:
    virtual ~DbModel() = default;

    /**
     * @brief 表名
     */
    virtual std::string tableName() const = 0;

    /**
     * @brief 模型字段元数据
     */
    virtual std::vector<DbModelField> fields() const = 0;

    /**
     * @brief 转换为数据库行（默认基于字段绑定器）
     */
    virtual DbRow toRow() const {
        DbRow row;
        for (const auto& b : m_binders) {
            row[b.column] = b.getter(const_cast<DbModel*>(this));
        }
        return row;
    }

    /**
     * @brief 从数据库行填充（默认基于字段绑定器）
     */
    virtual void fromRow(const DbRow& row) {
        for (const auto& b : m_binders) {
            auto it = row.find(b.column);
            if (it != row.end()) {
                b.setter(this, it->second);
            }
        }
    }

    /**
     * @brief 主键列名
     */
    virtual std::string primaryKey() const { return m_primaryKey; }

    /**
     * @brief 主键是否自增
     */
    virtual bool isAutoIncrement() const { return m_autoIncrement; }

    /**
     * @brief 返回主键值（字符串形式）
     */
    virtual std::string primaryKeyValue() const {
        auto row = toRow();
        auto it = row.find(m_primaryKey);
        return it == row.end() ? "" : it->second;
    }

    /**
     * @brief 设置主键值
     */
    virtual void setPrimaryKeyValue(const std::string& v) {
        setFieldValue(m_primaryKey, v);
    }

    virtual void setFieldValue(const std::string& column, const std::string& value) {
        for (const auto& b : m_binders) {
            if (b.column == column) {
                b.setter(this, value);
                return;
            }
        }
    }

    /**
     * @brief 是否新增记录（主键为空或 0）
     */
    virtual bool isNew() const {
        std::string v = primaryKeyValue();
        return v.empty() || v == "0";
    }

    // ========================================================================
    // 自动时间戳
    // ========================================================================
    virtual bool isAutoTimestamp() const { return m_autoTimestamp; }
    virtual std::string createdAtColumn() const { return m_createdAtColumn; }
    virtual std::string updatedAtColumn() const { return m_updatedAtColumn; }

    // ========================================================================
    // 软删除
    // ========================================================================
    virtual bool isSoftDelete() const { return m_softDelete; }
    virtual std::string softDeleteColumn() const { return m_softDeleteColumn; }
    virtual bool isSoftDeleted() const {
        if (!m_softDelete) return false;
        auto row = toRow();
        auto it = row.find(m_softDeleteColumn);
        return it != row.end() && !it->second.empty();
    }

    // ========================================================================
    // 乐观锁
    // ========================================================================
    virtual bool isOptimisticLock() const { return m_optimisticLock; }
    virtual std::string versionColumn() const { return m_versionColumn; }
    virtual int64_t versionValue() const {
        auto row = toRow();
        auto it = row.find(m_versionColumn);
        return it != row.end() ? detail::parseInt64(it->second) : 0;
    }

    // ========================================================================
    // 校验
    // ========================================================================
    virtual DbValidationError validate() const {
        return validateFields(toRow(), m_validators);
    }

    void addValidator(const DbValidator& v) {
        m_validators.push_back(v);
    }

    // ========================================================================
    // 生命周期事件钩子（可在派生类中覆盖）
    // ========================================================================
    virtual void beforeInsert() {}
    virtual void afterInsert() {}
    virtual void beforeUpdate() {}
    virtual void afterUpdate() {}
    virtual void beforeSave() {}
    virtual void afterSave() {}
    virtual void beforeDelete() {}
    virtual void afterDelete() {}
    virtual void afterLoad() {}

protected:
    std::vector<DbModelField> m_fieldsCache;
    std::vector<DbFieldBinder> m_binders;
    std::string m_primaryKey;
    bool m_autoIncrement = false;

    bool m_autoTimestamp = false;
    std::string m_createdAtColumn;
    std::string m_updatedAtColumn;

    bool m_softDelete = false;
    std::string m_softDeleteColumn;

    bool m_optimisticLock = false;
    std::string m_versionColumn;

    std::vector<DbValidator> m_validators;
};

} // namespace db
} // namespace zero

// ============================================================================
// 宏定义
// ============================================================================

/**
 * @brief 声明 ORM 模型
 *
 * 在 class 的 public 区域使用：
 *   class User : public zero::db::DbModel {
 *   public:
 *       int64_t id = 0;
 *       std::string name;
 *
 *       ZERO_DB_MODEL(User, "user")
 *       ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
 *       ZERO_DB_PRIMARY_KEY("id")
 *       ZERO_DB_AUTO_INCREMENT()
 *       ZERO_DB_FIELD_STRING(name, "name", NotNull)
 *       ZERO_DB_MODEL_END()
 *   };
 */
#define ZERO_DB_MODEL(ClassName, TableName) \
public: \
    using _DbModelSelf = ClassName; \
    ClassName() { _bindFields(); } \
    std::string tableName() const override { return TableName; } \
    std::vector< ::zero::db::DbModelField> fields() const override { return m_fieldsCache; } \
private: \
    void _bindFields() { \
        m_fieldsCache.clear(); \
        m_binders.clear();

#define ZERO_DB_FIELD_INT64(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::Int64}); \
    m_binders.push_back({column, ::zero::db::DbValueType::Int64, \
        [](::zero::db::DbModel* _self) { return std::to_string(static_cast<_DbModelSelf*>(_self)->member); }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = ::zero::db::detail::parseInt64(_v); }});

#define ZERO_DB_FIELD_INT32(member, column, flags) \
    ZERO_DB_FIELD_INT64(member, column, flags)

#define ZERO_DB_FIELD_UINT64(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::UInt64}); \
    m_binders.push_back({column, ::zero::db::DbValueType::UInt64, \
        [](::zero::db::DbModel* _self) { return std::to_string(static_cast<_DbModelSelf*>(_self)->member); }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = ::zero::db::detail::parseUInt64(_v); }});

#define ZERO_DB_FIELD_DOUBLE(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::Double}); \
    m_binders.push_back({column, ::zero::db::DbValueType::Double, \
        [](::zero::db::DbModel* _self) { return ::zero::db::detail::doubleToString(static_cast<_DbModelSelf*>(_self)->member); }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = ::zero::db::detail::parseDouble(_v); }});

#define ZERO_DB_FIELD_STRING(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::String}); \
    m_binders.push_back({column, ::zero::db::DbValueType::String, \
        [](::zero::db::DbModel* _self) { return static_cast<_DbModelSelf*>(_self)->member; }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = _v; }});

#define ZERO_DB_FIELD_BOOL(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::Int64}); \
    m_binders.push_back({column, ::zero::db::DbValueType::Int64, \
        [](::zero::db::DbModel* _self) { return static_cast<_DbModelSelf*>(_self)->member ? std::string("1") : std::string("0"); }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = ::zero::db::detail::parseBool(_v); }});

#define ZERO_DB_FIELD_BLOB(member, column, flags) \
    m_fieldsCache.push_back({#member, column, flags, ::zero::db::DbValueType::Blob}); \
    m_binders.push_back({column, ::zero::db::DbValueType::Blob, \
        [](::zero::db::DbModel* _self) { return static_cast<_DbModelSelf*>(_self)->member; }, \
        [](::zero::db::DbModel* _self, const std::string& _v) { static_cast<_DbModelSelf*>(_self)->member = _v; }});

#define ZERO_DB_PRIMARY_KEY(column) \
    m_primaryKey = column;

#define ZERO_DB_AUTO_INCREMENT() \
    m_autoIncrement = true;

#define ZERO_DB_AUTO_TIMESTAMP(created_col, updated_col) \
    m_autoTimestamp = true; \
    m_createdAtColumn = created_col; \
    m_updatedAtColumn = updated_col;

#define ZERO_DB_SOFT_DELETE(col) \
    m_softDelete = true; \
    m_softDeleteColumn = col;

#define ZERO_DB_VERSION(col) \
    m_optimisticLock = true; \
    m_versionColumn = col;

#define ZERO_DB_VALIDATE_NOT_NULL(col) \
    m_validators.push_back(::zero::db::DbValidator(col, ::zero::db::DbValidateRule::NotNull, ""));

#define ZERO_DB_VALIDATE_NOT_EMPTY(col) \
    m_validators.push_back(::zero::db::DbValidator(col, ::zero::db::DbValidateRule::NotEmpty, ""));

#define ZERO_DB_VALIDATE_MAX_LENGTH(col, len) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::MaxLength, ""); _v.maxLength = len; m_validators.push_back(_v); }

#define ZERO_DB_VALIDATE_MIN_LENGTH(col, len) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::MinLength, ""); _v.minLength = len; m_validators.push_back(_v); }

#define ZERO_DB_VALIDATE_MIN(col, val) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::Min, ""); _v.min = val; m_validators.push_back(_v); }

#define ZERO_DB_VALIDATE_MAX(col, val) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::Max, ""); _v.max = val; m_validators.push_back(_v); }

#define ZERO_DB_VALIDATE_RANGE(col, lo, hi) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::Range, ""); _v.min = lo; _v.max = hi; m_validators.push_back(_v); }

#define ZERO_DB_VALIDATE_EMAIL(col) \
    m_validators.push_back(::zero::db::DbValidator(col, ::zero::db::DbValidateRule::Email, ""));

#define ZERO_DB_VALIDATE_REGEX(col, pat) \
    { ::zero::db::DbValidator _v(col, ::zero::db::DbValidateRule::Regex, ""); _v.pattern = pat; m_validators.push_back(_v); }

#define ZERO_DB_MODEL_END() \
    } \
public:

/**
 * @brief 便捷辅助：从 DbRow 取字段值（兼容旧代码手动实现）
 */
#define DB_GET_INT64(row, col, var) \
    do { auto _it = (row).find(col); if(_it != (row).end()) var = ::zero::db::detail::parseInt64(_it->second); } while(0)

#define DB_GET_UINT64(row, col, var) \
    do { auto _it = (row).find(col); if(_it != (row).end()) var = ::zero::db::detail::parseUInt64(_it->second); } while(0)

#define DB_GET_STRING(row, col, var) \
    do { auto _it = (row).find(col); if(_it != (row).end()) var = _it->second; } while(0)

#define DB_GET_DOUBLE(row, col, var) \
    do { auto _it = (row).find(col); if(_it != (row).end()) var = ::zero::db::detail::parseDouble(_it->second); } while(0)

#endif // __ZERO_DB_DB_MODEL_H__

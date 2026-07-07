/**
 * @file db_router.h
 * @brief 分库分表路由策略
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_ROUTER_H__
#define __ZERO_DB_DB_ROUTER_H__

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "db_value.h"

namespace zero {
namespace db {

/**
 * @brief 分片信息
 */
struct DbShardInfo {
    std::string dbNode;     ///< 数据库节点名
    std::string tableName;  ///< 真实表名（含后缀）
};

/**
 * @brief 路由接口
 */
class DbRouter {
public:
    typedef std::shared_ptr<DbRouter> ptr;
    virtual ~DbRouter() = default;

    /**
     * @brief 根据分片键返回数据库节点名
     */
    virtual std::string route(const std::string& table, const DbValue& key) = 0;

    /**
     * @brief 返回完整分片信息（库节点 + 真实表名）
     */
    virtual DbShardInfo routeFull(const std::string& table, const DbValue& key) = 0;

    /**
     * @brief 返回所有节点名
     */
    virtual std::vector<std::string> nodes() const = 0;
};

/**
 * @brief 取模分片路由
 * @tparam KeyExtractor 从 DbValue 提取 uint64_t 的函数对象
 */
class HashDbRouter : public DbRouter {
public:
    typedef std::shared_ptr<HashDbRouter> ptr;

    static ptr Create(const std::vector<std::string>& nodes, size_t tableCountPerNode = 1) {
        return std::make_shared<HashDbRouter>(nodes, tableCountPerNode);
    }

    HashDbRouter(const std::vector<std::string>& nodes, size_t tableCountPerNode = 1)
        : m_nodes(nodes), m_tableCountPerNode(tableCountPerNode) {
        if (m_tableCountPerNode < 1) m_tableCountPerNode = 1;
    }

    std::string route(const std::string& table, const DbValue& key) override {
        uint64_t k = extractKey(key);
        size_t totalTables = m_nodes.size() * m_tableCountPerNode;
        size_t slot = totalTables == 0 ? 0 : (k % totalTables);
        size_t nodeIdx = slot / m_tableCountPerNode;
        if (nodeIdx >= m_nodes.size()) nodeIdx = m_nodes.size() - 1;
        return m_nodes[nodeIdx];
    }

    DbShardInfo routeFull(const std::string& table, const DbValue& key) override {
        uint64_t k = extractKey(key);
        size_t totalTables = m_nodes.size() * m_tableCountPerNode;
        size_t slot = totalTables == 0 ? 0 : (k % totalTables);
        size_t nodeIdx = slot / m_tableCountPerNode;
        size_t tableIdx = slot % m_tableCountPerNode;
        if (nodeIdx >= m_nodes.size()) nodeIdx = m_nodes.size() - 1;
        DbShardInfo info;
        info.dbNode = m_nodes[nodeIdx];
        info.tableName = table + "_" + std::to_string(tableIdx);
        return info;
    }

    std::vector<std::string> nodes() const override {
        return m_nodes;
    }

private:
    uint64_t extractKey(const DbValue& key) {
        if (key.isNull()) return 0;
        if (key.type == DbValueType::Int64) {
            int64_t v = key.value.i64;
            return v < 0 ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v);
        }
        if (key.type == DbValueType::UInt64) {
            return key.value.u64;
        }
        const std::string& s = key.str;
        uint64_t h = 14695981039346656037ULL; // FNV offset basis
        for (char c : s) {
            h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        return h;
    }

    std::vector<std::string> m_nodes;
    size_t m_tableCountPerNode;
};

/**
 * @brief 范围路由（按 key 区间）
 */
class RangeDbRouter : public DbRouter {
public:
    typedef std::shared_ptr<RangeDbRouter> ptr;

    struct RangeNode {
        uint64_t lower;
        uint64_t upper;
        std::string node;
    };

    static ptr Create(const std::vector<RangeNode>& ranges) {
        return std::make_shared<RangeDbRouter>(ranges);
    }

    explicit RangeDbRouter(const std::vector<RangeNode>& ranges)
        : m_ranges(ranges) {
    }

    std::string route(const std::string& table, const DbValue& key) override {
        (void)table;
        uint64_t k = extractKey(key);
        for (const auto& r : m_ranges) {
            if (k >= r.lower && k < r.upper) {
                return r.node;
            }
        }
        return m_ranges.empty() ? "" : m_ranges.back().node;
    }

    DbShardInfo routeFull(const std::string& table, const DbValue& key) override {
        DbShardInfo info;
        info.dbNode = route(table, key);
        info.tableName = table;
        return info;
    }

    std::vector<std::string> nodes() const override {
        std::vector<std::string> result;
        for (const auto& r : m_ranges) {
            result.push_back(r.node);
        }
        return result;
    }

private:
    uint64_t extractKey(const DbValue& key) {
        if (key.isNull()) return 0;
        if (key.type == DbValueType::Int64) {
            int64_t v = key.value.i64;
            return v < 0 ? static_cast<uint64_t>(-v) : static_cast<uint64_t>(v);
        }
        if (key.type == DbValueType::UInt64) return key.value.u64;
        return 0;
    }

    std::vector<RangeNode> m_ranges;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_ROUTER_H__

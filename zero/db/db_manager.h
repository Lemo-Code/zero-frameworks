/**
 * @file db_manager.h
 * @brief 多数据源管理器，支持按名称注册、默认 session、读写分离、分库分表路由、缓存
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_MANAGER_H__
#define __ZERO_DB_DB_MANAGER_H__

#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <atomic>
#include "db_session.h"
#include "db_driver.h"
#include "db_query.h"
#include "db_cache.h"
#include "db_router.h"

namespace zero {
namespace db {

/**
 * @brief 数据库管理器（单例）
 */
class DbManager {
public:
    static DbManager* instance() {
        static DbManager s_mgr;
        return &s_mgr;
    }

    /**
     * @brief 注册命名 session（默认写库）
     */
    void registerSession(const std::string& name, DbSession::ptr session) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sessions[name] = session;
        if (m_default.empty()) {
            m_default = name;
        }
    }

    /**
     * @brief 注册驱动（可选，用于懒加载）
     */
    void registerDriver(const std::string& name, DbDriver::ptr driver) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_drivers[name] = driver;
    }

    /**
     * @brief 注册主库
     */
    void registerMaster(const std::string& name, DbSession::ptr session) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_master[name] = session;
        if (m_default.empty()) {
            m_default = name;
        }
    }

    /**
     * @brief 注册从库（可多次注册同一组）
     */
    void registerSlave(const std::string& name, DbSession::ptr session) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slaves[name].push_back(session);
    }

    /**
     * @brief 注册表路由
     */
    void registerRouter(const std::string& table, DbRouter::ptr router) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_routers[table] = router;
    }

    /**
     * @brief 设置全局缓存
     */
    void setCache(DbCache::ptr cache) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache = cache;
    }

    DbCache::ptr cache() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_cache;
    }

    /**
     * @brief 获取指定 session
     */
    DbSession::ptr session(const std::string& name = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = name.empty() ? m_default : name;
        auto it = m_sessions.find(key);
        if (it != m_sessions.end()) {
            return it->second;
        }
        auto dit = m_drivers.find(key);
        if (dit != m_drivers.end()) {
            DbSession::ptr s = dit->second->openSession();
            m_sessions[key] = s;
            return s;
        }
        return nullptr;
    }

    /**
     * @brief 获取写库 session
     */
    DbSession::ptr sessionForWrite(const std::string& name = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = name.empty() ? m_default : name;
        auto it = m_master.find(key);
        if (it != m_master.end()) {
            return it->second;
        }
        auto sit = m_sessions.find(key);
        if (sit != m_sessions.end()) {
            return sit->second;
        }
        return nullptr;
    }

    /**
     * @brief 获取读库 session（轮询从库）
     */
    DbSession::ptr sessionForRead(const std::string& name = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = name.empty() ? m_default : name;
        auto it = m_slaves.find(key);
        if (it != m_slaves.end() && !it->second.empty()) {
            size_t idx = m_slaveIndex++ % it->second.size();
            return it->second[idx];
        }
        auto mit = m_master.find(key);
        if (mit != m_master.end()) {
            return mit->second;
        }
        auto sit = m_sessions.find(key);
        if (sit != m_sessions.end()) {
            return sit->second;
        }
        return nullptr;
    }

    /**
     * @brief 根据表名返回 session（后续可扩展分库分表）
     * @param table 表名
     * @param write 是否为写操作
     */
    DbSession::ptr routeForTable(const std::string& table, bool write = false) {
        (void)table;
        return write ? sessionForWrite() : sessionForRead();
    }

    /**
     * @brief 根据表名和分片键路由
     */
    DbSession::ptr routeForTable(const std::string& table, const DbValue& key, bool write = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_routers.find(table);
        if (it != m_routers.end() && it->second) {
            std::string node = it->second->route(table, key);
            if (!node.empty()) {
                auto sit = m_sessions.find(node);
                if (sit != m_sessions.end()) return sit->second;
                auto mit = m_master.find(node);
                if (mit != m_master.end()) return mit->second;
            }
        }
        return write ? sessionForWrite() : sessionForRead();
    }

    void setDefault(const std::string& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_default = name;
    }

    /**
     * @brief 查询构造器（读库）
     */
    DbQuery::ptr query(const std::string& table) {
        auto s = sessionForRead();
        if (!s) return nullptr;
        return std::make_shared<DbQuery>(s, table);
    }

    /**
     * @brief 查询构造器（指定数据源）
     */
    DbQuery::ptr query(const std::string& name, const std::string& table) {
        auto s = session(name);
        if (!s) return nullptr;
        return std::make_shared<DbQuery>(s, table);
    }

    /**
     * @brief 查询构造器（分片路由）
     */
    DbQuery::ptr query(const std::string& table, const DbValue& key, bool write = false) {
        auto s = routeForTable(table, key, write);
        if (!s) return nullptr;
        return std::make_shared<DbQuery>(s, table);
    }

    /**
     * @brief 返回所有已注册驱动的统计 JSON
     */
    std::string getStatsJson() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream oss;
        oss << "{";
        bool first = true;
        for (auto& kv : m_drivers) {
            if (!first) oss << ",";
            first = false;
            oss << "\"" << kv.first << "\":" << (kv.second ? kv.second->getStatsJson() : "{}")
                << "";
        }
        oss << "}";
        return oss.str();
    }

    /**
     * @brief 关闭所有数据源
     */
    void closeAll() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& kv : m_sessions) {
            if (kv.second) kv.second->close();
        }
        for (auto& kv : m_master) {
            if (kv.second) kv.second->close();
        }
        for (auto& kv : m_slaves) {
            for (auto& s : kv.second) {
                if (s) s->close();
            }
        }
        m_sessions.clear();
        m_drivers.clear();
        m_master.clear();
        m_slaves.clear();
        m_routers.clear();
        m_cache.reset();
        m_default.clear();
    }

private:
    DbManager() = default;
    ~DbManager() { closeAll(); }
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    std::mutex m_mutex;
    std::map<std::string, DbSession::ptr> m_sessions;
    std::map<std::string, DbDriver::ptr> m_drivers;
    std::map<std::string, DbSession::ptr> m_master;
    std::map<std::string, std::vector<DbSession::ptr>> m_slaves;
    std::map<std::string, DbRouter::ptr> m_routers;
    DbCache::ptr m_cache;
    std::string m_default;
    std::atomic<size_t> m_slaveIndex{0};
};

inline DbSession::ptr defaultDbSession() {
    return DbManager::instance()->session();
}

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_MANAGER_H__

/**
 * @file db_cache.h
 * @brief ORM 缓存抽象与内存实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_DB_CACHE_H__
#define __ZERO_DB_DB_CACHE_H__

#include <memory>
#include <string>
#include <mutex>
#include <map>
#include <chrono>

namespace zero {
namespace db {

/**
 * @brief 缓存接口，可对接 Redis/Memcached/本地内存
 */
class DbCache {
public:
    typedef std::shared_ptr<DbCache> ptr;
    virtual ~DbCache() = default;

    /**
     * @brief 获取缓存值，命中返回 true 并通过 value 传出
     */
    virtual bool get(const std::string& key, std::string& value) = 0;

    /**
     * @brief 设置缓存，ttlSec <= 0 表示不过期
     */
    virtual bool set(const std::string& key, const std::string& value, int ttlSec = 0) = 0;

    /**
     * @brief 删除缓存
     */
    virtual bool remove(const std::string& key) = 0;
};

/**
 * @brief 内存缓存实现（线程安全，带 TTL）
 */
class MemoryDbCache : public DbCache {
public:
    typedef std::shared_ptr<MemoryDbCache> ptr;

    static ptr Create() {
        return std::make_shared<MemoryDbCache>();
    }

    bool get(const std::string& key, std::string& value) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_data.find(key);
        if (it == m_data.end()) return false;
        if (it->second.expireAt > 0 &&
            std::chrono::steady_clock::now().time_since_epoch().count() > it->second.expireAt) {
            m_data.erase(it);
            return false;
        }
        value = it->second.value;
        return true;
    }

    bool set(const std::string& key, const std::string& value, int ttlSec = 0) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        Item item;
        item.value = value;
        if (ttlSec > 0) {
            int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
            // steady_clock 精度为纳秒，ttl 转成纳秒
            item.expireAt = now + static_cast<int64_t>(ttlSec) * 1000000000LL;
        } else {
            item.expireAt = 0;
        }
        m_data[key] = item;
        return true;
    }

    bool remove(const std::string& key) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data.erase(key);
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_data.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_data.clear();
    }

private:
    struct Item {
        std::string value;
        int64_t expireAt = 0;
    };
    mutable std::mutex m_mutex;
    std::map<std::string, Item> m_data;
};

} // namespace db
} // namespace zero

#endif // __ZERO_DB_DB_CACHE_H__

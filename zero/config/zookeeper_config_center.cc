/**
 * @file zookeeper_config_center.cc
 * @brief ZooKeeper 配置中心实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zookeeper_config_center.h"
#include "zero/core/log/log.h"
#include <sstream>
#include <chrono>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

ZooKeeperConfigCenter::ptr ZooKeeperConfigCenter::create(
        const std::vector<std::string>& hosts,
        const std::string& prefix) {
    return std::shared_ptr<ZooKeeperConfigCenter>(new ZooKeeperConfigCenter(hosts, prefix));
}

ZooKeeperConfigCenter::ZooKeeperConfigCenter(const std::vector<std::string>& hosts,
                                             const std::string& prefix)
    : m_prefix(prefix) {
    std::ostringstream oss;
    for(size_t i = 0; i < hosts.size(); ++i) {
        if(i > 0) oss << ",";
        oss << hosts[i];
    }
    m_hosts = oss.str();
}

ZooKeeperConfigCenter::~ZooKeeperConfigCenter() {
    stopWatch();
#ifdef HAS_ZOOKEEPER
    if(m_zh) {
        zookeeper_close(m_zh);
        m_zh = nullptr;
    }
#endif
}

std::string ZooKeeperConfigCenter::makeZkPath(const std::string& key) const {
    std::string path = m_prefix;
    if(!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if(!key.empty() && key.front() == '/') {
        path += key;
    } else {
        path += "/" + key;
    }
    return path;
}

std::string ZooKeeperConfigCenter::shortKey(const std::string& zkPath) const {
    std::string base = m_prefix;
    if(!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if(zkPath.find(base) == 0) {
        std::string rest = zkPath.substr(base.size());
        if(!rest.empty() && rest.front() == '/') {
            rest = rest.substr(1);
        }
        return rest;
    }
    return zkPath;
}

std::string ZooKeeperConfigCenter::get(const std::string& key, const std::string& def) {
    Mutex::Lock lock(m_mutex);
    auto it = m_configs.find(key);
    return it == m_configs.end() ? def : it->second;
}

bool ZooKeeperConfigCenter::set(const std::string& key, const std::string& value) {
#ifdef HAS_ZOOKEEPER
    if(!m_zh || !m_connected) {
        ZERO_LOG_ERROR(g_logger) << "ZooKeeperConfigCenter not connected";
        return false;
    }
    std::string path = makeZkPath(key);
    int rc = zoo_exists(m_zh, path.c_str(), 0, nullptr);
    if(rc == ZNONODE) {
        rc = zoo_create(m_zh, path.c_str(), value.c_str(), value.size(),
                        &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
    } else if(rc == ZOK) {
        rc = zoo_set(m_zh, path.c_str(), value.c_str(), value.size(), -1);
    }
    if(rc != ZOK) {
        ZERO_LOG_ERROR(g_logger) << "ZooKeeperConfigCenter set failed: " << rc;
        return false;
    }
    Mutex::Lock lock(m_mutex);
    m_configs[key] = value;
    return true;
#else
    ZERO_LOG_WARN(g_logger) << "ZooKeeper support not compiled in";
    Mutex::Lock lock(m_mutex);
    m_configs[key] = value;
    return true;
#endif
}

void ZooKeeperConfigCenter::addListener(const std::string& key, ConfigListener listener) {
    Mutex::Lock lock(m_mutex);
    m_listeners[key].push_back(listener);
}

std::map<std::string, std::string> ZooKeeperConfigCenter::getAll() {
    Mutex::Lock lock(m_mutex);
    return m_configs;
}

void ZooKeeperConfigCenter::startWatch(int intervalMs) {
    bool expected = false;
    if(!m_running.compare_exchange_strong(expected, true)) return;
    m_intervalMs = intervalMs;
    syncFromZk();
    m_thread = std::thread(&ZooKeeperConfigCenter::watchLoop, this);
}

void ZooKeeperConfigCenter::stopWatch() {
    bool expected = true;
    if(!m_running.compare_exchange_strong(expected, false)) return;
    if(m_thread.joinable()) m_thread.join();
}

void ZooKeeperConfigCenter::watchLoop() {
    while(m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
        if(!m_running) break;
        syncFromZk();
    }
}

#ifdef HAS_ZOOKEEPER
static void collectChildren(ZooKeeperConfigCenter* center, zhandle_t* zh,
                            const std::string& basePath,
                            std::map<std::string, std::string>& out) {
    struct String_vector children;
    int rc = zoo_get_children(zh, basePath.c_str(), 0, &children);
    if(rc != ZOK) return;
    for(int i = 0; i < children.count; ++i) {
        std::string child = children.data[i];
        std::string fullPath = basePath;
        if(!fullPath.empty() && fullPath.back() != '/') fullPath += "/";
        fullPath += child;
        char buffer[4096] = {0};
        int bufLen = sizeof(buffer);
        rc = zoo_get(zh, fullPath.c_str(), 0, buffer, &bufLen, nullptr);
        if(rc == ZOK && bufLen > 0) {
            out[center->getAll().empty() ? child : ""] = std::string(buffer, bufLen);
        }
    }
    deallocate_String_vector(&children);
}
#endif

void ZooKeeperConfigCenter::syncFromZk() {
#ifdef HAS_ZOOKEEPER
    if(!m_zh) {
        m_zh = zookeeper_init(m_hosts.c_str(), watcherCallback, 10000, nullptr, this, 0);
        if(!m_zh) {
            ZERO_LOG_WARN(g_logger) << "ZooKeeper init failed: " << m_hosts;
            return;
        }
    }
    if(!m_connected) {
        // 未连接时不拉取，等待 watcher 触发
        return;
    }

    std::string base = m_prefix;
    if(!base.empty() && base.back() == '/') base.pop_back();

    std::map<std::string, std::string> newConfigs;
    struct String_vector children;
    int rc = zoo_get_children(m_zh, base.c_str(), 0, &children);
    if(rc == ZOK) {
        for(int i = 0; i < children.count; ++i) {
            std::string child = children.data[i];
            std::string fullPath = base + "/" + child;
            char buffer[4096] = {0};
            int bufLen = sizeof(buffer);
            rc = zoo_get(m_zh, fullPath.c_str(), 0, buffer, &bufLen, nullptr);
            if(rc == ZOK && bufLen >= 0) {
                newConfigs[child] = std::string(buffer, bufLen);
            }
        }
        deallocate_String_vector(&children);
    } else {
        ZERO_LOG_WARN(g_logger) << "ZooKeeper get children failed: " << rc;
        return;
    }
#else
    std::map<std::string, std::string> newConfigs;
    {
        Mutex::Lock lock(m_mutex);
        newConfigs = m_configs;
    }
#endif

    Mutex::Lock lock(m_mutex);
    // deleted
    for(auto it = m_configs.begin(); it != m_configs.end();) {
        if(newConfigs.find(it->first) == newConfigs.end()) {
            std::string key = it->first;
            it = m_configs.erase(it);
            lock.unlock();
            notify(key, "", ConfigChangeType::Deleted);
            lock.lock();
        } else {
            ++it;
        }
    }
    // added / modified
    for(auto& kv : newConfigs) {
        auto it = m_configs.find(kv.first);
        ConfigChangeType type = (it == m_configs.end()) ? ConfigChangeType::Added
                                                        : ConfigChangeType::Modified;
        if(it == m_configs.end() || it->second != kv.second) {
            m_configs[kv.first] = kv.second;
            lock.unlock();
            notify(kv.first, kv.second, type);
            lock.lock();
        }
    }
}

void ZooKeeperConfigCenter::notify(const std::string& key, const std::string& value, ConfigChangeType type) {
    std::vector<ConfigListener> listeners;
    {
        Mutex::Lock lock(m_mutex);
        auto it = m_listeners.find(key);
        if(it != m_listeners.end()) listeners = it->second;
    }
    ConfigChangeEvent event{key, value, type};
    for(auto& listener : listeners) {
        try {
            listener(event);
        } catch(...) {
            ZERO_LOG_ERROR(g_logger) << "ZooKeeperConfigCenter listener exception: " << key;
        }
    }
}

#ifdef HAS_ZOOKEEPER
void ZooKeeperConfigCenter::watcherCallback(zhandle_t* /*zh*/, int type, int state,
                                            const char* path, void* watcherCtx) {
    auto* center = static_cast<ZooKeeperConfigCenter*>(watcherCtx);
    if(center) {
        center->handleWatcherEvent(type, state, path ? path : "");
    }
}

void ZooKeeperConfigCenter::handleWatcherEvent(int type, int state, const std::string& path) {
    if(state == ZOO_CONNECTED_STATE) {
        m_connected = true;
        ZERO_LOG_INFO(g_logger) << "ZooKeeper connected";
    } else if(state == ZOO_EXPIRED_SESSION_STATE) {
        m_connected = false;
        if(m_zh) {
            zookeeper_close(m_zh);
            m_zh = nullptr;
        }
    }
    if(type == ZOO_CHILD_EVENT || type == ZOO_CHANGED_EVENT) {
        ZERO_LOG_INFO(g_logger) << "ZooKeeper watcher event path=" << path;
        syncFromZk();
    }
}
#endif

} // namespace zero

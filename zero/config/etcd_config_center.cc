/**
 * @file etcd_config_center.cc
 * @brief etcd 配置中心实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "etcd_config_center.h"
#include "zero/core/log/log.h"
#include "zero/util/json_util.h"
#include "zero/util/hash_util.h"

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

EtcdConfigCenter::ptr EtcdConfigCenter::create(const std::vector<std::string>& endpoints,
                                               const std::string& prefix) {
    return std::shared_ptr<EtcdConfigCenter>(new EtcdConfigCenter(endpoints, prefix));
}

EtcdConfigCenter::EtcdConfigCenter(const std::vector<std::string>& endpoints,
                                   const std::string& prefix)
    : m_client(std::make_shared<EtcdHttpClient>(endpoints)), m_prefix(prefix) {
}

EtcdConfigCenter::~EtcdConfigCenter() {
    stopWatch();
}

std::string EtcdConfigCenter::get(const std::string& key, const std::string& def) {
    Mutex::Lock lock(m_mutex);
    auto it = m_configs.find(key);
    return it == m_configs.end() ? def : it->second;
}

bool EtcdConfigCenter::set(const std::string& key, const std::string& value) {
    std::string etcdKey = m_prefix + key;
    auto resp = m_client->put(etcdKey, value);
    if (!resp.ok) {
        ZERO_LOG_ERROR(g_logger) << "EtcdConfigCenter set failed: " << resp.error;
        return false;
    }
    Mutex::Lock lock(m_mutex);
    m_configs[key] = value;
    return true;
}

void EtcdConfigCenter::addListener(const std::string& key, ConfigListener listener) {
    Mutex::Lock lock(m_mutex);
    m_listeners[key].push_back(listener);
}

std::map<std::string, std::string> EtcdConfigCenter::getAll() {
    Mutex::Lock lock(m_mutex);
    return m_configs;
}

void EtcdConfigCenter::startWatch(int intervalMs) {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return;
    m_intervalMs = intervalMs;
    syncFromEtcd();
    m_thread = std::thread(&EtcdConfigCenter::watchLoop, this);
}

void EtcdConfigCenter::stopWatch() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) return;
    if (m_thread.joinable()) m_thread.join();
}

void EtcdConfigCenter::watchLoop() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
        if (!m_running) break;
        syncFromEtcd();
    }
}

static std::string base64Decode(const std::string& in);

void EtcdConfigCenter::syncFromEtcd() {
    auto resp = m_client->getPrefix(m_prefix);
    if (!resp.ok) {
        ZERO_LOG_WARN(g_logger) << "EtcdConfigCenter sync failed: " << resp.error;
        return;
    }

    // 简单解析 JSON：从 kvs 数组中提取 key/value（value 是 base64）
    std::map<std::string, std::string> newConfigs;
    Json::Value root;
    Json::Reader reader;
    if (reader.parse(resp.body, root) && root.isMember("kvs")) {
        const Json::Value& kvs = root["kvs"];
        for (const auto& kv : kvs) {
            if (!kv.isMember("key") || !kv.isMember("value")) continue;
            std::string fullKey = base64Decode(kv["key"].asString());
            std::string val = base64Decode(kv["value"].asString());
            if (fullKey.find(m_prefix) == 0) {
                std::string shortKey = fullKey.substr(m_prefix.size());
                newConfigs[shortKey] = val;
            }
        }
    }

    Mutex::Lock lock(m_mutex);
    // deleted
    for (auto it = m_configs.begin(); it != m_configs.end();) {
        if (newConfigs.find(it->first) == newConfigs.end()) {
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
    for (auto& kv : newConfigs) {
        auto it = m_configs.find(kv.first);
        ConfigChangeType type = (it == m_configs.end()) ? ConfigChangeType::Added
                                                        : ConfigChangeType::Modified;
        if (it == m_configs.end() || it->second != kv.second) {
            m_configs[kv.first] = kv.second;
            lock.unlock();
            notify(kv.first, kv.second, type);
            lock.lock();
        }
    }
}

void EtcdConfigCenter::notify(const std::string& key, const std::string& value, ConfigChangeType type) {
    std::vector<ConfigListener> listeners;
    {
        Mutex::Lock lock(m_mutex);
        auto it = m_listeners.find(key);
        if (it != m_listeners.end()) listeners = it->second;
    }
    ConfigChangeEvent event{key, value, type};
    for (auto& listener : listeners) {
        try {
            listener(event);
        } catch (...) {
            ZERO_LOG_ERROR(g_logger) << "EtcdConfigCenter listener exception: " << key;
        }
    }
}

static std::string base64Decode(const std::string& in) {
    // 复用框架 base64 解码
    return zero::base64decode(in);
}

} // namespace zero

/**
 * @file config_center.cc
 * @brief 配置中心实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "config_center.h"
#include "zero/core/log/log.h"
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <sys/stat.h>
#include <chrono>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

MemoryConfigCenter::MemoryConfigCenter() = default;

MemoryConfigCenter::~MemoryConfigCenter() {
    stopHotReload();
}

MemoryConfigCenter::ptr MemoryConfigCenter::create() {
    return std::shared_ptr<MemoryConfigCenter>(new MemoryConfigCenter);
}

void MemoryConfigCenter::startHotReload(const std::string& path, int intervalMs) {
    bool expected = false;
    if (!m_watching.compare_exchange_strong(expected, true)) {
        return;
    }
    m_watchPath = path;
    m_watchIntervalMs = intervalMs;
    // 先加载一次
    loadFromFile(path);
    m_watchThread = std::thread(&MemoryConfigCenter::hotReloadLoop, this);
}

void MemoryConfigCenter::stopHotReload() {
    bool expected = true;
    if (!m_watching.compare_exchange_strong(expected, false)) {
        return;
    }
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
}

void MemoryConfigCenter::hotReloadLoop() {
    while (m_watching) {
        std::this_thread::sleep_for(std::chrono::milliseconds(m_watchIntervalMs));
        if (!m_watching) break;
        struct stat st;
        if (stat(m_watchPath.c_str(), &st) == 0) {
            if (st.st_mtime != m_lastMtime) {
                m_lastMtime = st.st_mtime;
                loadFromFile(m_watchPath);
            }
        }
    }
}

std::string MemoryConfigCenter::get(const std::string& key, const std::string& def) {
    Mutex::Lock lock(m_mutex);
    auto it = m_configs.find(key);
    if(it == m_configs.end()) {
        return def;
    }
    return it->second;
}

bool MemoryConfigCenter::set(const std::string& key, const std::string& value) {
    ConfigChangeType type = ConfigChangeType::Added;
    {
        Mutex::Lock lock(m_mutex);
        auto it = m_configs.find(key);
        if(it != m_configs.end()) {
            if(it->second == value) {
                return true;
            }
            type = ConfigChangeType::Modified;
        }
        m_configs[key] = value;
    }
    notify(key, value, type);
    return true;
}

void MemoryConfigCenter::addListener(const std::string& key, ConfigListener listener) {
    Mutex::Lock lock(m_mutex);
    m_listeners[key].push_back(listener);
}

std::map<std::string, std::string> MemoryConfigCenter::getAll() {
    Mutex::Lock lock(m_mutex);
    return m_configs;
}

void MemoryConfigCenter::notify(const std::string& key, const std::string& value, ConfigChangeType type) {
    std::vector<ConfigListener> listeners;
    {
        Mutex::Lock lock(m_mutex);
        auto it = m_listeners.find(key);
        if(it != m_listeners.end()) {
            listeners = it->second;
        }
    }
    ConfigChangeEvent event;
    event.key = key;
    event.value = value;
    event.type = type;
    for(auto& listener : listeners) {
        try {
            listener(event);
        } catch(...) {
            ZERO_LOG_ERROR(g_logger) << "Config listener exception for key: " << key;
        }
    }
}

static void flattenYaml(const YAML::Node& node, const std::string& prefix,
                        std::map<std::string, std::string>& out) {
    if(node.IsMap()) {
        for(auto it = node.begin(); it != node.end(); ++it) {
            std::string key = prefix.empty() ? it->first.as<std::string>()
                                             : prefix + "." + it->first.as<std::string>();
            flattenYaml(it->second, key, out);
        }
    } else if(node.IsSequence()) {
        // 序列类型暂不支持
    } else {
        out[prefix] = node.as<std::string>();
    }
}

bool MemoryConfigCenter::loadFromFile(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        m_lastMtime = st.st_mtime;
    }
    try {
        YAML::Node root = YAML::LoadFile(path);
        std::map<std::string, std::string> newConfigs;
        flattenYaml(root, "", newConfigs);

        Mutex::Lock lock(m_mutex);
        // 删除已不存在的 key
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

        // 新增或修改
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
        return true;
    } catch(const std::exception& e) {
        ZERO_LOG_ERROR(g_logger) << "Failed to load config file: " << path
                                 << ", error: " << e.what();
        return false;
    }
}

} // namespace zero

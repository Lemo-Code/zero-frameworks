/**
 * @file config_center.h
 * @brief 配置中心 / 动态配置热更新
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_CONFIG_CENTER_H__
#define __ZERO_CONFIG_CENTER_H__

#include <memory>
#include <string>
#include <map>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include "zero/core/concurrency/mutex.h"

namespace zero {

/**
 * @brief 配置变更类型
 */
enum class ConfigChangeType {
    Added,
    Modified,
    Deleted
};

/**
 * @brief 配置变更事件
 */
struct ConfigChangeEvent {
    std::string key;
    std::string value;
    ConfigChangeType type;
};

/**
 * @brief 配置监听器
 */
typedef std::function<void(const ConfigChangeEvent&)> ConfigListener;

/**
 * @brief 配置中心接口
 */
class ConfigCenter {
public:
    typedef std::shared_ptr<ConfigCenter> ptr;
    virtual ~ConfigCenter() = default;

    /**
     * @brief 获取配置值
     */
    virtual std::string get(const std::string& key, const std::string& def = "") = 0;

    /**
     * @brief 设置配置值（本地或远程）
     */
    virtual bool set(const std::string& key, const std::string& value) = 0;

    /**
     * @brief 监听配置变更
     */
    virtual void addListener(const std::string& key, ConfigListener listener) = 0;

    /**
     * @brief 加载所有配置
     */
    virtual std::map<std::string, std::string> getAll() = 0;
};

/**
 * @brief 内存配置中心
 * 
 * 支持本地热更新和监听器回调。
 */
class MemoryConfigCenter : public ConfigCenter {
public:
    typedef std::shared_ptr<MemoryConfigCenter> ptr;

    MemoryConfigCenter();
    ~MemoryConfigCenter();

    static MemoryConfigCenter::ptr create();

    std::string get(const std::string& key, const std::string& def = "") override;
    bool set(const std::string& key, const std::string& value) override;
    void addListener(const std::string& key, ConfigListener listener) override;
    std::map<std::string, std::string> getAll() override;

    /**
     * @brief 从 YAML 文件加载配置并热更新
     */
    bool loadFromFile(const std::string& path);

    /**
     * @brief 启动文件热更新监听
     */
    void startHotReload(const std::string& path, int intervalMs = 3000);

    /**
     * @brief 停止文件热更新监听
     */
    void stopHotReload();

private:
    void hotReloadLoop();
    void notify(const std::string& key, const std::string& value, ConfigChangeType type);

    std::map<std::string, std::string> m_configs;
    std::map<std::string, std::vector<ConfigListener>> m_listeners;
    mutable Mutex m_mutex;

    std::atomic<bool> m_watching{false};
    std::string m_watchPath;
    int m_watchIntervalMs = 3000;
    std::thread m_watchThread;
    time_t m_lastMtime = 0;
};

} // namespace zero

#endif // __ZERO_CONFIG_CENTER_H__

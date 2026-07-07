/**
 * @file zookeeper_config_center.h
 * @brief 基于 ZooKeeper 的远程配置中心
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_CONFIG_ZOOKEEPER_CONFIG_CENTER_H__
#define __ZERO_CONFIG_ZOOKEEPER_CONFIG_CENTER_H__

#include "zero/config/config_center.h"
#include <atomic>
#include <thread>

#ifdef HAS_ZOOKEEPER
#include <zookeeper/zookeeper.h>
#endif

namespace zero {

/**
 * @brief ZooKeeper 配置中心
 */
class ZooKeeperConfigCenter : public ConfigCenter {
public:
    typedef std::shared_ptr<ZooKeeperConfigCenter> ptr;

    static ZooKeeperConfigCenter::ptr create(const std::vector<std::string>& hosts,
                                             const std::string& prefix = "/zero/config");

    std::string get(const std::string& key, const std::string& def = "") override;
    bool set(const std::string& key, const std::string& value) override;
    void addListener(const std::string& key, ConfigListener listener) override;
    std::map<std::string, std::string> getAll() override;

    /**
     * @brief 启动后台监听（轮询+Watcher）
     */
    void startWatch(int intervalMs = 3000);

    /**
     * @brief 停止监听
     */
    void stopWatch();

public:
    ~ZooKeeperConfigCenter();

private:
    ZooKeeperConfigCenter(const std::vector<std::string>& hosts, const std::string& prefix);

    std::string makeZkPath(const std::string& key) const;
    std::string shortKey(const std::string& zkPath) const;
    void watchLoop();
    void syncFromZk();
    void notify(const std::string& key, const std::string& value, ConfigChangeType type);

#ifdef HAS_ZOOKEEPER
    static void watcherCallback(zhandle_t* zh, int type, int state,
                                const char* path, void* watcherCtx);
    void handleWatcherEvent(int type, int state, const std::string& path);
    zhandle_t* m_zh = nullptr;
#endif

    std::string m_hosts;
    std::string m_prefix;
    std::map<std::string, std::string> m_configs;
    std::map<std::string, std::vector<ConfigListener>> m_listeners;
    mutable Mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    int m_intervalMs = 3000;
    bool m_connected = false;
};

} // namespace zero

#endif // __ZERO_CONFIG_ZOOKEEPER_CONFIG_CENTER_H__

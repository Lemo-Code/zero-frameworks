/**
 * @file etcd_config_center.h
 * @brief 基于 etcd 的远程配置中心
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_CONFIG_ETCD_CONFIG_CENTER_H__
#define __ZERO_CONFIG_ETCD_CONFIG_CENTER_H__

#include "zero/config/config_center.h"
#include "zero/registry/etcd_http_client.h"
#include <atomic>
#include <thread>

namespace zero {

/**
 * @brief etcd 配置中心
 */
class EtcdConfigCenter : public ConfigCenter {
public:
    typedef std::shared_ptr<EtcdConfigCenter> ptr;

    static EtcdConfigCenter::ptr create(const std::vector<std::string>& endpoints,
                                        const std::string& prefix = "/zero/config/");

    std::string get(const std::string& key, const std::string& def = "") override;
    bool set(const std::string& key, const std::string& value) override;
    void addListener(const std::string& key, ConfigListener listener) override;
    std::map<std::string, std::string> getAll() override;

    /**
     * @brief 启动后台轮询监听
     */
    void startWatch(int intervalMs = 3000);

    /**
     * @brief 停止监听
     */
    void stopWatch();

public:
    ~EtcdConfigCenter();

private:
    EtcdConfigCenter(const std::vector<std::string>& endpoints, const std::string& prefix);
    void watchLoop();
    void syncFromEtcd();
    void notify(const std::string& key, const std::string& value, ConfigChangeType type);

    EtcdHttpClient::ptr m_client;
    std::string m_prefix;
    std::map<std::string, std::string> m_configs;
    std::map<std::string, std::vector<ConfigListener>> m_listeners;
    mutable Mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    int m_intervalMs = 3000;
};

} // namespace zero

#endif // __ZERO_CONFIG_ETCD_CONFIG_CENTER_H__

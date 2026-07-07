/**
 * @file etcd_registry.h
 * @brief etcd 服务注册与发现实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_REGISTRY_ETCD_REGISTRY_H__
#define __ZERO_REGISTRY_ETCD_REGISTRY_H__

#include "service_registry.h"
#include "etcd_http_client.h"
#include "zero/core/concurrency/mutex.h"
#include <thread>
#include <atomic>

namespace zero {

/**
 * @brief etcd 服务注册中心
 * 
 * 基于 etcd v3 API 的服务注册与发现。
 * 使用 HTTP/JSON 接口与 etcd 通信。
 */
class EtcdRegistry : public ServiceRegistry {
public:
    typedef std::shared_ptr<EtcdRegistry> ptr;

    /**
     * @brief 构造函数
     * @param[in] endpoints etcd 地址列表，如 "http://127.0.0.1:2379"
     */
    EtcdRegistry(const std::vector<std::string>& endpoints);
    ~EtcdRegistry();

    bool registerService(const ServiceInstance& instance) override;
    bool deregisterService(const std::string& serviceId) override;
    bool heartbeat(const std::string& serviceId) override;
    std::vector<ServiceInstance> discover(const std::string& serviceName) override;
    void watch(const std::string& serviceName, ServiceWatcher watcher) override;
    void unwatch(const std::string& serviceName) override;

    /**
     * @brief 启动心跳保活线程
     */
    void startHeartbeat(int intervalSec = 30);

    /**
     * @brief 停止心跳保活线程
     */
    void stopHeartbeat();

private:
    std::string getKey(const std::string& serviceName, const std::string& instanceId);
    std::string serializeInstance(const ServiceInstance& instance);
    ServiceInstance deserializeInstance(const std::string& json);
    bool put(const std::string& key, const std::string& value, int ttl = 0);
    bool del(const std::string& key);
    std::string get(const std::string& key);
    std::vector<std::string> getPrefix(const std::string& prefix);

private:
    std::vector<std::string> m_endpoints;
    std::string m_leader;
    EtcdHttpClient::ptr m_client;
    std::atomic<bool> m_running{false};
    std::thread m_heartbeatThread;
    int m_heartbeatInterval = 30;
    std::map<std::string, ServiceInstance> m_registeredServices;
    std::map<std::string, ServiceWatcher> m_watchers;
    Mutex m_mutex;
};

} // namespace zero

#endif

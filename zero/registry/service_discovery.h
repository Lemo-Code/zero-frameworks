/**
 * @file service_discovery.h
 * @brief 面向调用方的服务发现客户端（支持健康检查、元数据过滤、订阅变更）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_REGISTRY_SERVICE_DISCOVERY_H__
#define __ZERO_REGISTRY_SERVICE_DISCOVERY_H__

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include "service_registry.h"

namespace zero {

/**
 * @brief 健康检查配置
 */
struct HealthCheckConfig {
    bool enabled = true;
    int intervalMs = 5000;      ///< 主动探测间隔
    int timeoutMs = 2000;       ///< 探测超时
    int failThreshold = 2;      ///< 连续失败次数视为不健康
    int successThreshold = 1;   ///< 连续成功次数恢复健康
};

/**
 * @brief 服务发现客户端
 */
class ServiceDiscovery {
public:
    typedef std::shared_ptr<ServiceDiscovery> ptr;
    typedef std::function<bool(const ServiceInstance&)> InstanceFilter;

    explicit ServiceDiscovery(ServiceRegistry::ptr registry,
                              const HealthCheckConfig& hc = HealthCheckConfig());
    ~ServiceDiscovery();

    /**
     * @brief 发现指定服务的健康实例
     */
    std::vector<ServiceInstance> discover(const std::string& serviceName);

    /**
     * @brief 带过滤条件的发现
     */
    std::vector<ServiceInstance> discover(const std::string& serviceName, InstanceFilter filter);

    /**
     * @brief 按版本发现
     */
    std::vector<ServiceInstance> discoverByVersion(const std::string& serviceName,
                                                   const std::string& version);

    /**
     * @brief 按 metadata 键值发现
     */
    std::vector<ServiceInstance> discoverByMetadata(const std::string& serviceName,
                                                    const std::string& key,
                                                    const std::string& value);

    /**
     * @brief 订阅服务变化
     */
    void watch(const std::string& serviceName,
               ServiceRegistry::ServiceWatcher watcher);

    /**
     * @brief 取消订阅
     */
    void unwatch(const std::string& serviceName);

    /**
     * @brief 手动标记实例健康状态
     */
    void setInstanceHealthy(const std::string& serviceId, bool healthy);

    /**
     * @brief 启动/停止后台健康检查
     */
    void start();
    void stop();

private:
    void healthCheckLoop();
    bool probeInstance(const ServiceInstance& instance);

    ServiceRegistry::ptr m_registry;
    HealthCheckConfig m_hcConfig;
    std::mutex m_mutex;
    std::map<std::string, std::vector<ServiceInstance>> m_cache;
    std::map<std::string, ServiceRegistry::ServiceWatcher> m_watchers;
    std::map<std::string, int> m_failCount;
    std::map<std::string, int> m_successCount;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace zero

#endif // __ZERO_REGISTRY_SERVICE_DISCOVERY_H__

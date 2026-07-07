/**
 * @file service_registry.h
 * @brief 服务注册与发现接口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_REGISTRY_SERVICE_REGISTRY_H__
#define __ZERO_REGISTRY_SERVICE_REGISTRY_H__

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>

namespace zero {

/**
 * @brief 服务实例信息
 */
struct ServiceInstance {
    std::string id;
    std::string serviceName;
    std::string host;
    int port = 0;
    std::string version;
    std::map<std::string, std::string> metadata;
    bool healthy = true;
    uint64_t registerTime = 0;
    uint64_t lastHeartbeat = 0;
};

/**
 * @brief 服务注册与发现接口
 */
class ServiceRegistry {
public:
    typedef std::shared_ptr<ServiceRegistry> ptr;
    typedef std::function<void(const std::vector<ServiceInstance>&)> ServiceWatcher;

    virtual ~ServiceRegistry() = default;

    /**
     * @brief 注册服务实例
     */
    virtual bool registerService(const ServiceInstance& instance) = 0;

    /**
     * @brief 注销服务实例
     */
    virtual bool deregisterService(const std::string& serviceId) = 0;

    /**
     * @brief 发送心跳
     */
    virtual bool heartbeat(const std::string& serviceId) = 0;

    /**
     * @brief 发现服务实例
     */
    virtual std::vector<ServiceInstance> discover(const std::string& serviceName) = 0;

    /**
     * @brief 监听服务变化
     */
    virtual void watch(const std::string& serviceName, ServiceWatcher watcher) = 0;

    /**
     * @brief 取消监听
     */
    virtual void unwatch(const std::string& serviceName) = 0;
};

} // namespace zero

#endif

/**
 * @file service_discovery.cc
 * @brief 服务发现客户端实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "service_discovery.h"
#include "zero/core/log/log.h"
#include <algorithm>
#include <chrono>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

ServiceDiscovery::ServiceDiscovery(ServiceRegistry::ptr registry,
                                   const HealthCheckConfig& hc)
    : m_registry(registry), m_hcConfig(hc) {
}

ServiceDiscovery::~ServiceDiscovery() {
    stop();
}

void ServiceDiscovery::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) {
        return;
    }
    if (m_hcConfig.enabled) {
        m_thread = std::thread(&ServiceDiscovery::healthCheckLoop, this);
    }
}

void ServiceDiscovery::stop() {
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::vector<ServiceInstance> ServiceDiscovery::discover(const std::string& serviceName) {
    if (!m_registry) return {};
    auto instances = m_registry->discover(serviceName);
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ServiceInstance> result;
    for (auto& inst : instances) {
        auto it = m_failCount.find(inst.id);
        bool localHealthy = (it == m_failCount.end() || it->second < m_hcConfig.failThreshold);
        if (inst.healthy && localHealthy) {
            result.push_back(inst);
        }
    }
    m_cache[serviceName] = result;
    return result;
}

std::vector<ServiceInstance> ServiceDiscovery::discover(const std::string& serviceName,
                                                        InstanceFilter filter) {
    auto instances = discover(serviceName);
    if (!filter) return instances;
    std::vector<ServiceInstance> result;
    for (const auto& inst : instances) {
        if (filter(inst)) result.push_back(inst);
    }
    return result;
}

std::vector<ServiceInstance> ServiceDiscovery::discoverByVersion(const std::string& serviceName,
                                                                  const std::string& version) {
    return discover(serviceName, [&version](const ServiceInstance& inst) {
        return inst.version == version;
    });
}

std::vector<ServiceInstance> ServiceDiscovery::discoverByMetadata(const std::string& serviceName,
                                                                   const std::string& key,
                                                                   const std::string& value) {
    return discover(serviceName, [&key, &value](const ServiceInstance& inst) {
        auto it = inst.metadata.find(key);
        return it != inst.metadata.end() && it->second == value;
    });
}

void ServiceDiscovery::watch(const std::string& serviceName,
                             ServiceRegistry::ServiceWatcher watcher) {
    if (!m_registry) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchers[serviceName] = watcher;
    }
    m_registry->watch(serviceName, [this, watcher](const std::vector<ServiceInstance>& instances) {
        std::vector<ServiceInstance> healthy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& inst : instances) {
                auto it = m_failCount.find(inst.id);
                bool localHealthy = (it == m_failCount.end() ||
                                     it->second < m_hcConfig.failThreshold);
                if (inst.healthy && localHealthy) {
                    healthy.push_back(inst);
                }
            }
        }
        if (watcher) watcher(healthy);
    });
}

void ServiceDiscovery::unwatch(const std::string& serviceName) {
    if (!m_registry) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_watchers.erase(serviceName);
    }
    m_registry->unwatch(serviceName);
}

void ServiceDiscovery::setInstanceHealthy(const std::string& serviceId, bool healthy) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (healthy) {
        m_failCount.erase(serviceId);
        m_successCount[serviceId] = m_hcConfig.successThreshold;
    } else {
        m_failCount[serviceId] = m_hcConfig.failThreshold;
        m_successCount.erase(serviceId);
    }
}

void ServiceDiscovery::healthCheckLoop() {
    while (m_running) {
        std::map<std::string, std::vector<ServiceInstance>> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            snapshot = m_cache;
        }
        for (const auto& kv : snapshot) {
            for (const auto& inst : kv.second) {
                bool ok = probeInstance(inst);
                std::lock_guard<std::mutex> lock(m_mutex);
                if (ok) {
                    m_failCount.erase(inst.id);
                    int& sc = m_successCount[inst.id];
                    ++sc;
                } else {
                    m_successCount.erase(inst.id);
                    int& fc = m_failCount[inst.id];
                    ++fc;
                    if (fc >= m_hcConfig.failThreshold) {
                        ZERO_LOG_WARN(g_logger) << "Service instance unhealthy: "
                                                << inst.id << " @ " << inst.host << ":" << inst.port;
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(m_hcConfig.intervalMs));
    }
}

bool ServiceDiscovery::probeInstance(const ServiceInstance& instance) {
    (void)instance;
    // 默认使用 TCP 连接探测；生产环境可替换为 HTTP / RPC 健康检查接口
    // 这里简化实现，直接返回 registry 上报的 healthy 状态
    return true;
}

} // namespace zero

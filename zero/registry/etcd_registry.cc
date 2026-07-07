/**
 * @file etcd_registry.cc
 * @brief etcd 服务注册与发现实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "etcd_registry.h"
#include "zero/core/log/log.h"
#include "zero/util/json_util.h"
#include <chrono>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

EtcdRegistry::EtcdRegistry(const std::vector<std::string>& endpoints)
    : m_endpoints(endpoints) {
    if(!endpoints.empty()) {
        m_leader = endpoints[0];
        m_client = std::make_shared<EtcdHttpClient>(endpoints);
    }
}

EtcdRegistry::~EtcdRegistry() {
    stopHeartbeat();
}

std::string EtcdRegistry::getKey(const std::string& serviceName, const std::string& instanceId) {
    return "/zero/services/" + serviceName + "/" + instanceId;
}

std::string EtcdRegistry::serializeInstance(const ServiceInstance& instance) {
    Json::Value root;
    root["id"] = instance.id;
    root["serviceName"] = instance.serviceName;
    root["host"] = instance.host;
    root["port"] = instance.port;
    root["version"] = instance.version;
    root["healthy"] = instance.healthy;
    root["registerTime"] = (Json::Value::Int64)instance.registerTime;
    root["lastHeartbeat"] = (Json::Value::Int64)instance.lastHeartbeat;
    
    Json::Value meta;
    for(auto& kv : instance.metadata) {
        meta[kv.first] = kv.second;
    }
    root["metadata"] = meta;
    
    return root.toStyledString();
}

ServiceInstance EtcdRegistry::deserializeInstance(const std::string& json) {
    ServiceInstance instance;
    Json::Value root;
    Json::Reader reader;
    if(!reader.parse(json, root)) {
        return instance;
    }
    
    instance.id = root.get("id", "").asString();
    instance.serviceName = root.get("serviceName", "").asString();
    instance.host = root.get("host", "").asString();
    instance.port = root.get("port", 0).asInt();
    instance.version = root.get("version", "").asString();
    instance.healthy = root.get("healthy", true).asBool();
    instance.registerTime = root.get("registerTime", 0).asUInt64();
    instance.lastHeartbeat = root.get("lastHeartbeat", 0).asUInt64();
    
    const Json::Value& meta = root["metadata"];
    if(meta.isObject()) {
        for(auto it = meta.begin(); it != meta.end(); ++it) {
            instance.metadata[it.name()] = it->asString();
        }
    }
    
    return instance;
}

bool EtcdRegistry::put(const std::string& key, const std::string& value, int ttl) {
    if(!m_client) return false;
    // 注意：etcd v3 PUT 的 ttl 通过 lease 实现，这里简化忽略 ttl
    auto resp = m_client->put(key, value);
    if(!resp.ok) {
        ZERO_LOG_ERROR(g_logger) << "Etcd PUT failed: " << key;
        return false;
    }
    ZERO_LOG_INFO(g_logger) << "Etcd PUT: " << key;
    return true;
}

bool EtcdRegistry::del(const std::string& key) {
    if(!m_client) return false;
    auto resp = m_client->del(key);
    if(!resp.ok) {
        ZERO_LOG_ERROR(g_logger) << "Etcd DELETE failed: " << key;
        return false;
    }
    ZERO_LOG_INFO(g_logger) << "Etcd DELETE: " << key;
    return true;
}

std::string EtcdRegistry::get(const std::string& key) {
    if(!m_client) return "";
    auto resp = m_client->get(key);
    if(!resp.ok || resp.body.empty()) return "";
    
    Json::Value root;
    Json::Reader reader;
    if(!reader.parse(resp.body, root)) return "";
    const Json::Value& kvs = root["kvs"];
    if(!kvs.isArray() || kvs.empty()) return "";
    return kvs[0].get("value", "").asString();
}

std::vector<std::string> EtcdRegistry::getPrefix(const std::string& prefix) {
    std::vector<std::string> values;
    if(!m_client) return values;
    auto resp = m_client->getPrefix(prefix);
    if(!resp.ok || resp.body.empty()) return values;
    
    Json::Value root;
    Json::Reader reader;
    if(!reader.parse(resp.body, root)) return values;
    const Json::Value& kvs = root["kvs"];
    if(!kvs.isArray()) return values;
    for(auto& kv : kvs) {
        values.push_back(kv.get("value", "").asString());
    }
    return values;
}

bool EtcdRegistry::registerService(const ServiceInstance& instance) {
    std::string key = getKey(instance.serviceName, instance.id);
    std::string value = serializeInstance(instance);
    
    if(!put(key, value, m_heartbeatInterval * 2)) {
        ZERO_LOG_ERROR(g_logger) << "Failed to register service: " << instance.id;
        return false;
    }
    
    Mutex::Lock lock(m_mutex);
    m_registeredServices[instance.id] = instance;
    auto watchers = m_watchers;
    lock.unlock();

    ZERO_LOG_INFO(g_logger) << "Registered service: " << instance.serviceName 
                             << "(" << instance.id << ") at " << instance.host << ":" << instance.port;

    // 通知已注册的 watcher
    auto it = watchers.find(instance.serviceName);
    if(it != watchers.end() && it->second) {
        it->second(discover(instance.serviceName));
    }
    return true;
}

bool EtcdRegistry::deregisterService(const std::string& serviceId) {
    Mutex::Lock lock(m_mutex);
    auto it = m_registeredServices.find(serviceId);
    if(it == m_registeredServices.end()) {
        return false;
    }

    std::string serviceName = it->second.serviceName;
    std::string key = getKey(serviceName, serviceId);
    m_registeredServices.erase(it);
    auto watchers = m_watchers;
    lock.unlock();

    if(!del(key)) {
        ZERO_LOG_WARN(g_logger) << "Failed to deregister service: " << serviceId;
    }
    ZERO_LOG_INFO(g_logger) << "Deregistered service: " << serviceId;

    auto wit = watchers.find(serviceName);
    if(wit != watchers.end() && wit->second) {
        wit->second(discover(serviceName));
    }
    return true;
}

bool EtcdRegistry::heartbeat(const std::string& serviceId) {
    Mutex::Lock lock(m_mutex);
    auto it = m_registeredServices.find(serviceId);
    if(it == m_registeredServices.end()) {
        return false;
    }
    
    ServiceInstance instance = it->second;
    instance.lastHeartbeat = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    lock.unlock();
    
    std::string key = getKey(instance.serviceName, serviceId);
    std::string value = serializeInstance(instance);
    return put(key, value, m_heartbeatInterval * 2);
}

std::vector<ServiceInstance> EtcdRegistry::discover(const std::string& serviceName) {
    std::vector<ServiceInstance> instances;
    Mutex::Lock lock(m_mutex);
    
    // 优先从本地缓存返回
    if(!m_registeredServices.empty()) {
        for(auto& kv : m_registeredServices) {
            if(kv.second.serviceName == serviceName && kv.second.healthy) {
                instances.push_back(kv.second);
            }
        }
        if(!instances.empty()) {
            return instances;
        }
    }
    
    // 本地无缓存，从 etcd 拉取
    lock.unlock();
    std::string prefix = "/zero/services/" + serviceName + "/";
    auto values = getPrefix(prefix);
    for(auto& v : values) {
        ServiceInstance inst = deserializeInstance(v);
        if(inst.healthy) {
            instances.push_back(inst);
        }
    }
    return instances;
}

void EtcdRegistry::watch(const std::string& serviceName, ServiceWatcher watcher) {
    Mutex::Lock lock(m_mutex);
    m_watchers[serviceName] = watcher;
}

void EtcdRegistry::unwatch(const std::string& serviceName) {
    Mutex::Lock lock(m_mutex);
    m_watchers.erase(serviceName);
}

void EtcdRegistry::startHeartbeat(int intervalSec) {
    m_heartbeatInterval = intervalSec;
    m_running = true;
    m_heartbeatThread = std::thread([this]() {
        while(m_running) {
            std::vector<std::string> serviceIds;
            {
                Mutex::Lock lock(m_mutex);
                for(auto& kv : m_registeredServices) {
                    serviceIds.push_back(kv.first);
                }
            }
            for(auto& id : serviceIds) {
                heartbeat(id);
            }
            std::this_thread::sleep_for(std::chrono::seconds(m_heartbeatInterval));
        }
    });
}

void EtcdRegistry::stopHeartbeat() {
    m_running = false;
    if(m_heartbeatThread.joinable()) {
        m_heartbeatThread.join();
    }
}

} // namespace zero

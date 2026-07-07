/**
 * @file rpc_config.cc
 * @brief RPC 模块 - rpc_config
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_config.h"

namespace zero {
namespace rpc {

static RpcConfig::ptr g_rpc_config;

RpcConfig::ptr RpcConfig::GetInstance() {
    if (!g_rpc_config) g_rpc_config.reset(new RpcConfig);
    return g_rpc_config;
}

void RpcConfig::setNodeId(const std::string& id) {
    zero::Mutex::Lock lock(m_mutex); m_nodeId = id;
}
const std::string& RpcConfig::nodeId() const {
    zero::Mutex::Lock lock(m_mutex); return m_nodeId;
}

void RpcConfig::setNodeHost(const std::string& host) {
    zero::Mutex::Lock lock(m_mutex); m_nodeHost = host;
}
const std::string& RpcConfig::nodeHost() const {
    zero::Mutex::Lock lock(m_mutex); return m_nodeHost;
}

void RpcConfig::setNodePort(int port) {
    zero::Mutex::Lock lock(m_mutex); m_nodePort = port;
}
int RpcConfig::nodePort() const {
    zero::Mutex::Lock lock(m_mutex); return m_nodePort;
}

void RpcConfig::setSentinelPeers(const std::vector<std::string>& peers) {
    zero::Mutex::Lock lock(m_mutex); m_sentinelPeers = peers;
}
std::vector<std::string> RpcConfig::sentinelPeers() const {
    zero::Mutex::Lock lock(m_mutex); return m_sentinelPeers;
}

void RpcConfig::setMonitoredNodes(const std::vector<std::string>& nodes) {
    zero::Mutex::Lock lock(m_mutex); m_monitoredNodes = nodes;
}
std::vector<std::string> RpcConfig::monitoredNodes() const {
    zero::Mutex::Lock lock(m_mutex); return m_monitoredNodes;
}

void RpcConfig::setSentinelMode(bool v) {
    zero::Mutex::Lock lock(m_mutex); m_sentinelMode = v;
}
bool RpcConfig::sentinelMode() const {
    zero::Mutex::Lock lock(m_mutex); return m_sentinelMode;
}

void RpcConfig::setHealthCheckIntervalMs(int64_t ms) {
    zero::Mutex::Lock lock(m_mutex); m_healthCheckIntervalMs = ms;
}
int64_t RpcConfig::healthCheckIntervalMs() const {
    zero::Mutex::Lock lock(m_mutex); return m_healthCheckIntervalMs;
}

void RpcConfig::setElectionTimeoutMinMs(int64_t ms) {
    zero::Mutex::Lock lock(m_mutex); m_electionTimeoutMinMs = ms;
}
int64_t RpcConfig::electionTimeoutMinMs() const {
    zero::Mutex::Lock lock(m_mutex); return m_electionTimeoutMinMs;
}

void RpcConfig::setElectionTimeoutMaxMs(int64_t ms) {
    zero::Mutex::Lock lock(m_mutex); m_electionTimeoutMaxMs = ms;
}
int64_t RpcConfig::electionTimeoutMaxMs() const {
    zero::Mutex::Lock lock(m_mutex); return m_electionTimeoutMaxMs;
}

void RpcConfig::setHeartbeatIntervalMs(int64_t ms) {
    zero::Mutex::Lock lock(m_mutex); m_heartbeatIntervalMs = ms;
}
int64_t RpcConfig::heartbeatIntervalMs() const {
    zero::Mutex::Lock lock(m_mutex); return m_heartbeatIntervalMs;
}

void RpcConfig::setRpcTimeoutMs(int64_t ms) {
    zero::Mutex::Lock lock(m_mutex); m_rpcTimeoutMs = ms;
}
int64_t RpcConfig::rpcTimeoutMs() const {
    zero::Mutex::Lock lock(m_mutex); return m_rpcTimeoutMs;
}

} // namespace rpc
} // namespace zero

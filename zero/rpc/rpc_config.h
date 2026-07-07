/**
 * @file rpc_config.h
 * @brief RPC module configuration singleton
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_CONFIG_H__
#define __ZERO_RPC_RPC_CONFIG_H__

#include "zero/core/concurrency/mutex.h"
#include <cstdint>
#include <string>
#include <vector>

namespace zero {
namespace rpc {

class RpcConfig {
public:
    typedef std::shared_ptr<RpcConfig> ptr;

    static RpcConfig::ptr GetInstance();

    // Node identity (set before start)
    void setNodeId(const std::string& id);
    const std::string& nodeId() const;

    void setNodeHost(const std::string& host);
    const std::string& nodeHost() const;

    void setNodePort(int port);
    int nodePort() const;

    // Sentinel peers as "host:port"
    void setSentinelPeers(const std::vector<std::string>& peers);
    std::vector<std::string> sentinelPeers() const;

    // KV nodes to monitor as "host:port"
    void setMonitoredNodes(const std::vector<std::string>& nodes);
    std::vector<std::string> monitoredNodes() const;

    void setSentinelMode(bool v);
    bool sentinelMode() const;

    // Timing knobs (milliseconds)
    void setHealthCheckIntervalMs(int64_t ms);
    int64_t healthCheckIntervalMs() const;

    void setElectionTimeoutMinMs(int64_t ms);
    int64_t electionTimeoutMinMs() const;
    void setElectionTimeoutMaxMs(int64_t ms);
    int64_t electionTimeoutMaxMs() const;

    void setHeartbeatIntervalMs(int64_t ms);
    int64_t heartbeatIntervalMs() const;

    void setRpcTimeoutMs(int64_t ms);
    int64_t rpcTimeoutMs() const;

private:
    RpcConfig() = default;

    mutable zero::Mutex m_mutex;

    std::string m_nodeId;
    std::string m_nodeHost = "0.0.0.0";
    int m_nodePort = 0;
    std::vector<std::string> m_sentinelPeers;
    std::vector<std::string> m_monitoredNodes;
    bool m_sentinelMode = false;
    int64_t m_healthCheckIntervalMs = 1000;
    int64_t m_electionTimeoutMinMs = 5000;
    int64_t m_electionTimeoutMaxMs = 10000;
    int64_t m_heartbeatIntervalMs = 2000;
    int64_t m_rpcTimeoutMs = 3000;
};

} // namespace rpc
} // namespace zero

#endif

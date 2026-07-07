/**
 * @file sentinel_manager.h
 * @brief Core sentinel logic — leader election, health monitoring, failover
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_SENTINEL_MANAGER_H__
#define __ZERO_RPC_SENTINEL_MANAGER_H__

#include "zero/core/concurrency/mutex.h"
#include "zero/rpc/proto/kv_node.pb.h"
#include "zero/core/concurrency/thread.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero {
namespace rpc {

// Called when this node needs to change role (promote to master, or follow another).
using FailoverCallback = std::function<void(
    bool promote_to_master,
    const std::string& new_master_host,
    int new_master_port)>;

enum class SentinelRole { Follower, Candidate, Leader };

struct MasterInfo {
    bool valid = false;
    std::string host;
    int port = 0;
    std::string nodeId;
};

class SentinelManager : public std::enable_shared_from_this<SentinelManager> {
public:
    typedef std::shared_ptr<SentinelManager> ptr;

    SentinelManager();
    ~SentinelManager();

    // ---- Configuration (set before start()) ----
    void setNodeId(const std::string& id);
    std::string nodeId() const;

    void setBindAddr(const std::string& host, int port);
    std::string bindHost() const;
    int bindPort() const;

    // Peers as "host"→port pairs
    void setSentinelPeers(const std::vector<std::pair<std::string, int>>& peers);

    // KV nodes to monitor
    void setMonitoredNodes(const std::vector<std::pair<std::string, int>>& nodes);

    void setFailoverCallback(FailoverCallback cb);

    // ---- Lifecycle ----
    void start();
    void stop();

    // ---- RPC handlers (called from RpcServer / fiber context) ----
    bool handleRequestVote(uint64_t term, const std::string& candidateId);
    bool handleHeartbeat(uint64_t term, const std::string& leaderId,
                         const Endpoint* currentMaster);

    // ---- Query ----
    uint64_t currentTerm() const;
    SentinelRole role() const;
    std::string leaderId() const;
    MasterInfo currentMaster() const;

private:
    void runLoop();

    void becomeFollower(uint64_t newTerm);
    void becomeCandidate();
    void becomeLeader();
    void startElection();
    void sendHeartbeats();
    void checkKVNodes();
    void handleFailover(const std::string& failedMasterId);
    std::string selectBestReplica();

    // RPC helpers (called from sentinel thread via RpcChannel)
    bool sendRequestVote(const std::string& host, int port,
                         uint64_t term, const std::string& candidateId,
                         uint64_t& outTerm, bool& outGranted);
    bool sendHeartbeatTo(const std::string& host, int port);
    bool sendHealthCheck(const std::string& host, int port,
                         HealthCheckResponse& out);
    bool sendReplicaOf(const std::string& host, int port,
                       const std::string& masterHost, int masterPort,
                       uint64_t term);

    uint64_t randomTimeoutMs() const;
    uint64_t nowMs() const;
    bool isQuorum(int votes) const;
    static bool parseAddr(const std::string& s, std::string& host, int& port);

    // ---- Configuration (immutable after start()) ----
    mutable zero::Mutex m_configMutex;
    std::string m_nodeId;
    std::string m_bindHost = "0.0.0.0";
    int m_bindPort = 0;
    std::vector<std::pair<std::string, int>> m_sentinelPeers;
    std::vector<std::pair<std::string, int>> m_monitoredNodes;
    FailoverCallback m_failoverCb;

    // ---- Election state (m_stateMutex) ----
    mutable zero::Mutex m_stateMutex;
    uint64_t m_currentTerm = 0;
    std::string m_votedFor;
    SentinelRole m_role = SentinelRole::Follower;
    std::string m_leaderId;
    uint64_t m_electionTimeoutMs = 0;
    uint64_t m_lastHeartbeatMs = 0;

    // ---- KV node tracking (m_stateMutex) ----
    struct NodeState {
        std::string addr;
        int port = 0;
        NodeRole role = NodeRole::MASTER;
        std::string replId;
        int64_t replOffset = 0;
        bool alive = false;
        int consecutiveFailures = 0;
    };
    std::unordered_map<std::string, NodeState> m_kvNodes;
    MasterInfo m_currentMaster;

    // ---- Thread ----
    std::atomic<bool> m_running{false};
    zero::Thread::ptr m_thread;

    // Per-instance rate-limit timestamps (avoid static variables shared across instances)
    uint64_t m_lastHbMs = 0;
    uint64_t m_lastCheckMs = 0;

    static constexpr int kMaxHealthCheckFailures = 3;
    static constexpr int kHealthCheckIntervalMs = 1000;
    static constexpr int kElectionTimeoutMinMs = 5000;
    static constexpr int kElectionTimeoutMaxMs = 10000;
    static constexpr int kHeartbeatIntervalMs = 2000;
};

} // namespace rpc
} // namespace zero

#endif

/**
 * @file sentinel_manager.cc
 * @brief RPC 模块 - sentinel_manager
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "sentinel_manager.h"
#include "rpc_channel.h"

#include "zero/core/log/log.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("sentinel");

// ---- Lifecycle ----

SentinelManager::SentinelManager() {
    m_electionTimeoutMs = randomTimeoutMs();
    m_lastHeartbeatMs = nowMs();
}

SentinelManager::~SentinelManager() { stop(); }

// ---- Configuration ----

void SentinelManager::setNodeId(const std::string& id) {
    zero::Mutex::Lock lock(m_configMutex); m_nodeId = id;
}
std::string SentinelManager::nodeId() const {
    zero::Mutex::Lock lock(m_configMutex); return m_nodeId;
}

void SentinelManager::setBindAddr(const std::string& host, int port) {
    zero::Mutex::Lock lock(m_configMutex);
    m_bindHost = host; m_bindPort = port;
}
std::string SentinelManager::bindHost() const {
    zero::Mutex::Lock lock(m_configMutex); return m_bindHost;
}
int SentinelManager::bindPort() const {
    zero::Mutex::Lock lock(m_configMutex); return m_bindPort;
}

void SentinelManager::setSentinelPeers(const std::vector<std::pair<std::string, int>>& peers) {
    zero::Mutex::Lock lock(m_configMutex); m_sentinelPeers = peers;
}

void SentinelManager::setMonitoredNodes(const std::vector<std::pair<std::string, int>>& nodes) {
    zero::Mutex::Lock lock(m_configMutex);
    m_monitoredNodes = nodes;
    zero::Mutex::Lock stateLock(m_stateMutex);
    for (const auto& n : nodes) {
        std::string key = n.first + ":" + std::to_string(n.second);
        if (m_kvNodes.find(key) == m_kvNodes.end()) {
            NodeState ns;
            ns.addr = n.first;
            ns.port = n.second;
            ns.alive = false;
            ns.role = NodeRole::MASTER;
            m_kvNodes[key] = ns;
        }
    }
}

void SentinelManager::setFailoverCallback(FailoverCallback cb) {
    zero::Mutex::Lock lock(m_configMutex); m_failoverCb = std::move(cb);
}

void SentinelManager::start() {
    if (m_running.exchange(true)) return;
    ZERO_LOG_INFO(g_logger) << "SentinelManager starting nodeId=" << nodeId();
    m_thread.reset(new zero::Thread(std::bind(&SentinelManager::runLoop, this), "sentinel"));
}

void SentinelManager::stop() {
    if (!m_running.exchange(false)) return;
    ZERO_LOG_INFO(g_logger) << "SentinelManager stopping";
    if (m_thread) { m_thread->join(); m_thread.reset(); }
    RpcChannelPool::GetInstance()->clear();
}

// ---- RPC handlers (called from fiber context) ----

bool SentinelManager::handleRequestVote(uint64_t term, const std::string& candidateId) {
    zero::Mutex::Lock lock(m_stateMutex);
    if (term <= m_currentTerm) return false;
    if (m_votedFor.empty() || m_votedFor == candidateId) {
        m_currentTerm = term;
        m_votedFor = candidateId;
        m_role = SentinelRole::Follower;
        m_lastHeartbeatMs = nowMs();
        ZERO_LOG_INFO(g_logger) << "Voted for " << candidateId << " term=" << term;
        return true;
    }
    return false;
}

bool SentinelManager::handleHeartbeat(uint64_t term, const std::string& leaderId,
                                       const Endpoint* currentMaster) {
    zero::Mutex::Lock lock(m_stateMutex);
    if (term < m_currentTerm) return false;
    m_currentTerm = term;
    m_role = SentinelRole::Follower;
    m_leaderId = leaderId;
    m_lastHeartbeatMs = nowMs();
    m_votedFor.clear();
    if (currentMaster && !currentMaster->host().empty()) {
        m_currentMaster.valid = true;
        m_currentMaster.host = currentMaster->host();
        m_currentMaster.port = currentMaster->port();
    }
    return true;
}

// ---- Query ----

uint64_t SentinelManager::currentTerm() const {
    zero::Mutex::Lock lock(m_stateMutex); return m_currentTerm;
}
SentinelRole SentinelManager::role() const {
    zero::Mutex::Lock lock(m_stateMutex); return m_role;
}
std::string SentinelManager::leaderId() const {
    zero::Mutex::Lock lock(m_stateMutex); return m_leaderId;
}
MasterInfo SentinelManager::currentMaster() const {
    zero::Mutex::Lock lock(m_stateMutex); return m_currentMaster;
}

// ---- Main loop (dedicated thread) ----

void SentinelManager::runLoop() {
    std::string myId = nodeId();
    ZERO_LOG_INFO(g_logger) << "Sentinel loop started nodeId=" << myId
        << " electionTimeout=" << m_electionTimeoutMs << "ms";

    while (m_running) {
        uint64_t now = nowMs();
        SentinelRole currentRole;
        uint64_t lastHb;
        {
            zero::Mutex::Lock lock(m_stateMutex);
            currentRole = m_role;
            lastHb = m_lastHeartbeatMs;
        }

        switch (currentRole) {
        case SentinelRole::Follower:
            if (now - lastHb > m_electionTimeoutMs) {
                ZERO_LOG_INFO(g_logger) << "Election timeout, becoming candidate";
                becomeCandidate();
            }
            break;

        case SentinelRole::Candidate:
            startElection();
            {
                // Check the result under the lock, but call becomeLeader()
                // outside the lock to avoid:
                //   1. recursive m_stateMutex lock (becomeLeader locks it again)
                //   2. lock ordering inversion (becomeLeader→nodeId locks
                //      m_configMutex, but setMonitoredNodes locks
                //      m_configMutex→m_stateMutex)
                bool won = false;
                uint64_t term = 0;
                {
                    zero::Mutex::Lock lock(m_stateMutex);
                    if (m_role == SentinelRole::Leader) {
                        won = true;
                        term = m_currentTerm;
                    } else {
                        m_role = SentinelRole::Follower;
                        m_electionTimeoutMs = randomTimeoutMs();
                    }
                }
                if (won) {
                    ZERO_LOG_INFO(g_logger) << "Won election term=" << term;
                    becomeLeader();
                }
            }
            break;

        case SentinelRole::Leader:
            sendHeartbeats();
            checkKVNodes();
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ZERO_LOG_INFO(g_logger) << "Sentinel loop stopped";
}

void SentinelManager::becomeFollower(uint64_t newTerm) {
    zero::Mutex::Lock lock(m_stateMutex);
    m_currentTerm = std::max(m_currentTerm, newTerm);
    m_role = SentinelRole::Follower;
    m_votedFor.clear();
    m_electionTimeoutMs = randomTimeoutMs();
    m_lastHeartbeatMs = nowMs();
}

void SentinelManager::becomeCandidate() {
    std::string myId;
    { zero::Mutex::Lock lock(m_configMutex); myId = m_nodeId; }
    zero::Mutex::Lock lock(m_stateMutex);
    m_currentTerm++;
    m_role = SentinelRole::Candidate;
    m_votedFor = myId;
    m_electionTimeoutMs = randomTimeoutMs();
    ZERO_LOG_INFO(g_logger) << "Became candidate term=" << m_currentTerm;
}

void SentinelManager::becomeLeader() {
    // Read nodeId BEFORE acquiring m_stateMutex to avoid lock ordering
    // inversion: setMonitoredNodes() locks m_configMutex → m_stateMutex,
    // so we must not lock m_stateMutex → m_configMutex via nodeId().
    std::string myId = nodeId();

    zero::Mutex::Lock lock(m_stateMutex);
    m_role = SentinelRole::Leader;
    m_leaderId = std::move(myId);
    ZERO_LOG_INFO(g_logger) << "Became leader term=" << m_currentTerm;
    for (auto& kv : m_kvNodes) {
        kv.second.alive = false;
        kv.second.consecutiveFailures = 0;
    }
}

void SentinelManager::startElection() {
    uint64_t term;
    std::string myId;
    std::vector<std::pair<std::string, int>> peers;
    {
        zero::Mutex::Lock lock(m_stateMutex); term = m_currentTerm;
    }
    {
        zero::Mutex::Lock lock(m_configMutex);
        myId = m_nodeId;
        peers = m_sentinelPeers;
    }

    int votes = 1;  // self
    int total = 1 + (int)peers.size();
    ZERO_LOG_INFO(g_logger) << "Election term=" << term
        << " peers=" << peers.size() << " quorum=" << (total / 2 + 1);

    for (const auto& peer : peers) {
        uint64_t peerTerm = 0;
        bool granted = false;
        if (sendRequestVote(peer.first, peer.second, term, myId, peerTerm, granted)) {
            if (granted) votes++;
            else if (peerTerm > term) { becomeFollower(peerTerm); return; }
        }
        if (isQuorum(votes)) {
            zero::Mutex::Lock lock(m_stateMutex);
            if (m_role == SentinelRole::Candidate && m_currentTerm == term)
                m_role = SentinelRole::Leader;
            return;
        }
    }
}

void SentinelManager::sendHeartbeats() {
    uint64_t now = nowMs();
    if (now - m_lastHbMs < kHeartbeatIntervalMs) return;
    m_lastHbMs = now;

    std::vector<std::pair<std::string, int>> peers;
    { zero::Mutex::Lock lock(m_configMutex); peers = m_sentinelPeers; }

    for (const auto& peer : peers) sendHeartbeatTo(peer.first, peer.second);
}

void SentinelManager::checkKVNodes() {
    uint64_t now = nowMs();
    if (now - m_lastCheckMs < kHealthCheckIntervalMs) return;
    m_lastCheckMs = now;

    std::vector<std::pair<std::string, int>> nodes;
    { zero::Mutex::Lock lock(m_configMutex); nodes = m_monitoredNodes; }

    for (const auto& node : nodes) {
        std::string key = node.first + ":" + std::to_string(node.second);
        HealthCheckResponse resp;
        bool ok = sendHealthCheck(node.first, node.second, resp);

        zero::Mutex::Lock lock(m_stateMutex);
        auto it = m_kvNodes.find(key);
        if (it == m_kvNodes.end()) {
            NodeState ns;
            ns.addr = node.first; ns.port = node.second;
            ns.alive = ok;
            ns.role = ok ? resp.role() : NodeRole::MASTER;
            ns.replId = ok ? resp.replication_id() : "";
            ns.replOffset = ok ? resp.replication_offset() : 0;
            ns.consecutiveFailures = ok ? 0 : 1;
            m_kvNodes[key] = ns;
        } else if (ok) {
            it->second.alive = true;
            it->second.role = resp.role();
            it->second.replId = resp.replication_id();
            it->second.replOffset = resp.replication_offset();
            it->second.consecutiveFailures = 0;
            if (resp.role() == NodeRole::MASTER) {
                m_currentMaster.valid = true;
                m_currentMaster.host = node.first;
                m_currentMaster.port = node.second;
                m_currentMaster.nodeId = resp.node_id();
            }
        } else {
            it->second.alive = false;
            it->second.consecutiveFailures++;
            if (it->second.role == NodeRole::MASTER &&
                it->second.consecutiveFailures >= kMaxHealthCheckFailures) {
                ZERO_LOG_WARN(g_logger) << "Master " << key << " down after "
                    << kMaxHealthCheckFailures << " failures";
                lock.unlock();
                handleFailover(key);
                return;
            }
        }
    }
}

void SentinelManager::handleFailover(const std::string& failedMasterId) {
    ZERO_LOG_INFO(g_logger) << "Failover for master " << failedMasterId;
    std::string best = selectBestReplica();
    if (best.empty()) { ZERO_LOG_ERROR(g_logger) << "No suitable replica"; return; }

    std::string host; int port = 0;
    if (!parseAddr(best, host, port)) {
        ZERO_LOG_ERROR(g_logger) << "Bad replica addr " << best; return;
    }

    uint64_t term;
    { zero::Mutex::Lock lock(m_stateMutex); term = m_currentTerm; }

    ZERO_LOG_INFO(g_logger) << "Promoting " << best << " to master term=" << term;
    if (!sendReplicaOf(host, port, "", 0, term)) {
        ZERO_LOG_ERROR(g_logger) << "Cannot promote " << best; return;
    }

    std::vector<std::pair<std::string, int>> allNodes;
    { zero::Mutex::Lock lock(m_configMutex); allNodes = m_monitoredNodes; }

    for (const auto& node : allNodes) {
        std::string key = node.first + ":" + std::to_string(node.second);
        if (key == best || key == failedMasterId) continue;
        ZERO_LOG_INFO(g_logger) << "Reconfiguring " << key << " → " << host << ":" << port;
        sendReplicaOf(node.first, node.second, host, port, term);
    }

    {
        zero::Mutex::Lock lock(m_stateMutex);
        m_currentMaster = {true, host, port, best};
        auto it = m_kvNodes.find(failedMasterId);
        if (it != m_kvNodes.end()) it->second.role = NodeRole::REPLICA;
        auto nit = m_kvNodes.find(best);
        if (nit != m_kvNodes.end()) nit->second.role = NodeRole::MASTER;
    }
    ZERO_LOG_INFO(g_logger) << "Failover complete: new master=" << best;
}

std::string SentinelManager::selectBestReplica() {
    std::string bestAddr;
    int64_t bestOffset = -1;
    zero::Mutex::Lock lock(m_stateMutex);
    for (const auto& kv : m_kvNodes) {
        if (kv.second.alive && kv.second.role == NodeRole::REPLICA &&
            kv.second.replOffset > bestOffset) {
            bestOffset = kv.second.replOffset;
            bestAddr = kv.first;
        }
    }
    if (bestAddr.empty()) {
        for (const auto& kv : m_kvNodes) {
            if (kv.second.alive && kv.second.role != NodeRole::MASTER &&
                kv.second.replOffset > bestOffset) {
                bestOffset = kv.second.replOffset;
                bestAddr = kv.first;
            }
        }
    }
    return bestAddr;
}

// ---- RPC helpers (called from sentinel thread via RpcChannel, raw I/O) ----

bool SentinelManager::sendRequestVote(const std::string& host, int port,
                                       uint64_t term, const std::string& candidateId,
                                       uint64_t& outTerm, bool& outGranted) {
    RpcChannel::ptr ch = RpcChannelPool::GetInstance()->getChannel(host, port);
    if (!ch) return false;

    RpcEnvelope req;
    req.set_request_id(static_cast<uint64_t>(nowMs()));
    auto* vr = req.mutable_request_vote_req();
    vr->set_term(term);
    vr->set_candidate_id(candidateId);

    RpcEnvelope resp;
    if (!ch->call(req, resp, 1000)) {
        RpcChannelPool::GetInstance()->removeChannel(host, port);
        return false;
    }
    if (resp.body_case() != RpcEnvelope::kRequestVoteResp) return false;

    outTerm = resp.request_vote_resp().term();
    outGranted = resp.request_vote_resp().vote_granted();
    return true;
}

bool SentinelManager::sendHeartbeatTo(const std::string& host, int port) {
    RpcChannel::ptr ch = RpcChannelPool::GetInstance()->getChannel(host, port);
    if (!ch) return false;

    MasterInfo master;
    uint64_t term;
    std::string myId;
    {
        zero::Mutex::Lock lock(m_stateMutex);
        master = m_currentMaster;
        term = m_currentTerm;
    }
    {
        zero::Mutex::Lock lock(m_configMutex);
        myId = m_nodeId;
    }

    RpcEnvelope req;
    req.set_request_id(static_cast<uint64_t>(nowMs()));
    auto* hb = req.mutable_heartbeat_req();
    hb->set_term(term);
    hb->set_leader_id(myId);
    if (master.valid) {
        hb->mutable_current_master()->set_host(master.host);
        hb->mutable_current_master()->set_port(master.port);
    }

    RpcEnvelope resp;
    if (!ch->call(req, resp, 500)) {
        RpcChannelPool::GetInstance()->removeChannel(host, port);
        return false;
    }
    if (resp.body_case() != RpcEnvelope::kHeartbeatResp) return false;

    uint64_t peerTerm = resp.heartbeat_resp().term();
    if (peerTerm > term) becomeFollower(peerTerm);
    return true;
}

bool SentinelManager::sendHealthCheck(const std::string& host, int port,
                                       HealthCheckResponse& out) {
    RpcChannel::ptr ch = RpcChannelPool::GetInstance()->getChannel(host, port);
    if (!ch) return false;

    RpcEnvelope req;
    req.set_request_id(static_cast<uint64_t>(nowMs()));
    req.mutable_health_check_req()->set_caller_id(nodeId());

    RpcEnvelope resp;
    if (!ch->call(req, resp, 1000)) {
        RpcChannelPool::GetInstance()->removeChannel(host, port);
        return false;
    }
    if (resp.body_case() != RpcEnvelope::kHealthCheckResp) return false;

    out = resp.health_check_resp();
    return true;
}

bool SentinelManager::sendReplicaOf(const std::string& host, int port,
                                     const std::string& masterHost, int masterPort,
                                     uint64_t term) {
    RpcChannel::ptr ch = RpcChannelPool::GetInstance()->getChannel(host, port);
    if (!ch) return false;

    RpcEnvelope req;
    req.set_request_id(static_cast<uint64_t>(nowMs()));
    auto* ro = req.mutable_replica_of_req();
    ro->set_master_host(masterHost);
    ro->set_master_port(masterPort);
    ro->set_term(term);

    RpcEnvelope resp;
    if (!ch->call(req, resp, 2000)) {
        RpcChannelPool::GetInstance()->removeChannel(host, port);
        return false;
    }
    if (resp.body_case() != RpcEnvelope::kReplicaOfResp) return false;
    return resp.replica_of_resp().accepted();
}

// ---- Utility ----

uint64_t SentinelManager::randomTimeoutMs() const {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(kElectionTimeoutMinMs, kElectionTimeoutMaxMs);
    return static_cast<uint64_t>(dist(gen));
}

uint64_t SentinelManager::nowMs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

bool SentinelManager::isQuorum(int votes) const {
    int total;
    {
        zero::Mutex::Lock lock(m_configMutex);
        total = 1 + (int)m_sentinelPeers.size();
    }
    return votes >= (total / 2 + 1);
}

bool SentinelManager::parseAddr(const std::string& s, std::string& host, int& port) {
    size_t colon = s.find(':');
    if (colon == std::string::npos) return false;
    host = s.substr(0, colon);
    try { port = std::stoi(s.substr(colon + 1)); } catch (...) { return false; }
    return true;
}

} // namespace rpc
} // namespace zero

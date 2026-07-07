/**
 * @file cluster_manager.cc
 * @brief Redis Cluster 状态管理实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cluster_manager.h"
#include "zero/kv/store/kv_store.h"
#include "zero/core/log/log.h"
#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>

namespace zero {
namespace kv {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static std::string generateNodeId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::ostringstream ss;
    for (int i = 0; i < 40; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

static int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 全局 ClusterManager 绑定
static ClusterManager::ptr g_cluster_manager;

void bindClusterManager(ClusterManager::ptr mgr) {
    g_cluster_manager = mgr;
}

ClusterManager::ptr getClusterManager() {
    return g_cluster_manager;
}

ClusterManager::ClusterManager() {
    for (auto& s : m_slot_states) {
        s.type = SlotState::Normal;
    }
    for (auto& id : m_slot_owner) {
        id.clear();
    }
}

ClusterManager::~ClusterManager() = default;

void ClusterManager::init(const std::string& my_node_id,
                          const std::string& bind_ip,
                          int port,
                          int cluster_port,
                          const std::string& announce_ip,
                          int announce_port) {
    RWMutex::WriteLock lock(m_mutex);
    m_enabled = true;
    m_bind_ip = bind_ip;
    m_port = port;
    m_cluster_port = cluster_port;
    m_announce_ip = announce_ip.empty() ? bind_ip : announce_ip;
    m_announce_port = announce_port > 0 ? announce_port : port;
    m_my_node_id = my_node_id.empty() ? generateNodeId() : my_node_id;

    auto myself = std::make_shared<ClusterNode>();
    myself->node_id = m_my_node_id;
    myself->ip = m_announce_ip;
    myself->port = m_announce_port;
    myself->cluster_port = cluster_port;
    myself->setFlag(ClusterNodeFlag::Myself);
    myself->setFlag(ClusterNodeFlag::Master);
    myself->pong_received = nowMs();

    m_myself = myself;
    m_nodes[m_my_node_id] = myself;
    m_slot_owner_dirty = true;
}

void ClusterManager::setMyselfFromConfig(const std::shared_ptr<ClusterNode>& myself_node) {
    RWMutex::WriteLock lock(m_mutex);
    if(!myself_node) return;
    m_my_node_id = myself_node->node_id;
    m_myself = myself_node;
    m_nodes[m_my_node_id] = myself_node;
    // 同步 announce 地址（nodes.conf 中保存的就是 announce 地址）
    m_announce_ip = myself_node->ip.empty() ? m_bind_ip : myself_node->ip;
    m_announce_port = myself_node->port > 0 ? myself_node->port : m_port;
    m_slot_owner_dirty = true;
}

int ClusterManager::keyToSlot(const std::string& key) const {
    return kv::keyToSlot(key);
}

bool ClusterManager::isMySlot(int slot) const {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::ReadLock lock(m_mutex);
    if (!m_myself) return false;
    return m_myself->slots.test(slot);
}

bool ClusterManager::isImportingSlot(int slot) const {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::ReadLock lock(m_mutex);
    return m_slot_states[slot].type == SlotState::Importing;
}

bool ClusterManager::isMigratingSlot(int slot) const {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::ReadLock lock(m_mutex);
    return m_slot_states[slot].type == SlotState::Migrating;
}

std::shared_ptr<ClusterNode> ClusterManager::getNode(const std::string& node_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end()) return it->second;
    return nullptr;
}

std::shared_ptr<ClusterNode> ClusterManager::getMyself() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_myself;
}

void ClusterManager::addNode(const std::shared_ptr<ClusterNode>& node) {
    if (!node) return;
    RWMutex::WriteLock lock(m_mutex);
    m_nodes[node->node_id] = node;
    m_slot_owner_dirty = true;
}

bool ClusterManager::removeNode(const std::string& node_id) {
    if (node_id == m_my_node_id) return false;  // 不能删除自己
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return false;
    m_nodes.erase(it);
    m_slot_owner_dirty = true;
    return true;
}

bool ClusterManager::meetNode(const std::string& ip, int port) {
    // CLUSTER MEET: 不创建临时节点，由 cluster bus 发送 MEET，
    // 收到对方的 PONG 后再创建真实节点记录。
    ZERO_LOG_INFO(g_logger) << "CLUSTER MEET " << ip << ":" << port;
    return true;
}

void ClusterManager::addSlots(const std::vector<int>& slots) {
    RWMutex::WriteLock lock(m_mutex);
    if (!m_myself) return;
    for (int slot : slots) {
        if (slot >= 0 && slot < kClusterSlotCount) {
            m_myself->slots.set(slot);
            m_slot_states[slot].type = SlotState::Normal;
        }
    }
    m_slot_owner_dirty = true;
}

void ClusterManager::delSlots(const std::vector<int>& slots) {
    RWMutex::WriteLock lock(m_mutex);
    if (!m_myself) return;
    for (int slot : slots) {
        if (slot >= 0 && slot < kClusterSlotCount) {
            m_myself->slots.reset(slot);
            m_slot_states[slot].type = SlotState::Normal;
        }
    }
    m_slot_owner_dirty = true;
}

void ClusterManager::setReplication(const std::string& master_id) {
    RWMutex::WriteLock lock(m_mutex);
    if (!m_myself) return;
    m_myself->master_id = master_id;
    m_myself->clearFlag(ClusterNodeFlag::Master);
    m_myself->setFlag(ClusterNodeFlag::Slave);
    // 清空本节点所有槽位（副本不直接负责槽位）
    m_myself->slots.reset();
    m_slot_owner_dirty = true;
}

bool ClusterManager::isSlave() const {
    RWMutex::ReadLock lock(m_mutex);
    if (!m_myself) return false;
    return m_myself->hasFlag(ClusterNodeFlag::Slave);
}

std::string ClusterManager::getMasterId() const {
    RWMutex::ReadLock lock(m_mutex);
    if (!m_myself) return "";
    return m_myself->master_id;
}

std::string ClusterManager::getMasterAddr() const {
    RWMutex::ReadLock lock(m_mutex);
    if (!m_myself || m_myself->master_id.empty()) return "";
    auto it = m_nodes.find(m_myself->master_id);
    if (it == m_nodes.end()) return "";
    const auto& node = it->second;
    return node->ip + ":" + std::to_string(node->port);
}

std::shared_ptr<ClusterNode> ClusterManager::getMasterNode() const {
    RWMutex::ReadLock lock(m_mutex);
    if (!m_myself || m_myself->master_id.empty()) return nullptr;
    auto it = m_nodes.find(m_myself->master_id);
    if (it == m_nodes.end()) return nullptr;
    return it->second;
}

void ClusterManager::updateNodePingSent(const std::string& node_id, int64_t now_ms) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end()) {
        it->second->ping_sent = now_ms;
    }
}

void ClusterManager::updateNodePongReceived(const std::string& node_id, int64_t now_ms) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end()) {
        it->second->pong_received = now_ms;
        it->second->clearFlag(ClusterNodeFlag::PFail);
    }
}

void ClusterManager::markNodePFail(const std::string& node_id) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end() && !it->second->hasFlag(ClusterNodeFlag::Myself)) {
        it->second->setFlag(ClusterNodeFlag::PFail);
    }
}

void ClusterManager::markNodeFail(const std::string& node_id) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end() && !it->second->hasFlag(ClusterNodeFlag::Myself)) {
        it->second->setFlag(ClusterNodeFlag::Fail);
        it->second->clearFlag(ClusterNodeFlag::PFail);
    }
}

void ClusterManager::clearNodeFail(const std::string& node_id) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it != m_nodes.end()) {
        it->second->clearFlag(ClusterNodeFlag::Fail);
        it->second->clearFlag(ClusterNodeFlag::PFail);
    }
}

bool ClusterManager::isNodeFailed(const std::string& node_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return false;
    return it->second->hasFlag(ClusterNodeFlag::Fail);
}

bool ClusterManager::isNodePFail(const std::string& node_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return false;
    return it->second->hasFlag(ClusterNodeFlag::PFail);
}

std::vector<std::shared_ptr<ClusterNode>> ClusterManager::getReplicas(const std::string& master_id) const {
    RWMutex::ReadLock lock(m_mutex);
    std::vector<std::shared_ptr<ClusterNode>> out;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Slave) && node->master_id == master_id) {
            out.push_back(node);
        }
    }
    return out;
}

bool ClusterManager::failoverReplica(const std::string& replica_node_id) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(replica_node_id);
    if (it == m_nodes.end()) return false;
    auto& replica = it->second;
    if (!replica->hasFlag(ClusterNodeFlag::Slave)) return false;
    
    std::string old_master_id = replica->master_id;
    if (old_master_id.empty()) return false;
    
    auto master_it = m_nodes.find(old_master_id);
    if (master_it == m_nodes.end()) return false;
    auto& old_master = master_it->second;
    
    // 将副本提升为主节点
    replica->clearFlag(ClusterNodeFlag::Slave);
    replica->setFlag(ClusterNodeFlag::Master);
    replica->master_id.clear();
    
    // 继承原主节点的槽位
    replica->slots = old_master->slots;
    replica->config_epoch = std::max(replica->config_epoch, old_master->config_epoch) + 1;
    
    // 如果本节点就是该副本，更新 myself
    if (replica->hasFlag(ClusterNodeFlag::Myself)) {
        m_myself->clearFlag(ClusterNodeFlag::Slave);
        m_myself->setFlag(ClusterNodeFlag::Master);
        m_myself->master_id.clear();
        m_myself->slots = old_master->slots;
        m_myself->config_epoch = replica->config_epoch;
    }
    
    // 其他副本重新指向新主节点
    for (auto& p : m_nodes) {
        auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Slave) && node->master_id == old_master_id) {
            node->master_id = replica_node_id;
        }
    }
    
    // 原主节点标记为 fail（如果还没标记）
    old_master->setFlag(ClusterNodeFlag::Fail);
    old_master->clearFlag(ClusterNodeFlag::PFail);
    old_master->slots.reset();
    
    m_slot_owner_dirty = true;
    if (replica->config_epoch > m_config_epoch) {
        m_config_epoch = replica->config_epoch;
    } else {
        ++m_config_epoch;
    }
    
    ZERO_LOG_INFO(g_logger) << "Cluster failover: replica " << replica_node_id 
                             << " promoted to master (was slave of " << old_master_id << ")";
    return true;
}

std::string ClusterManager::myAddr() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_bind_ip + ":" + std::to_string(m_port);
}

std::vector<std::shared_ptr<ClusterNode>> ClusterManager::getAliveMasters() const {
    RWMutex::ReadLock lock(m_mutex);
    std::vector<std::shared_ptr<ClusterNode>> out;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Master) && 
            !node->hasFlag(ClusterNodeFlag::Fail) &&
            !node->hasFlag(ClusterNodeFlag::PFail)) {
            out.push_back(node);
        }
    }
    return out;
}

void ClusterManager::checkFailureDetection(int64_t now_ms, int64_t timeout_ms) {
    RWMutex::WriteLock lock(m_mutex);
    // 计算存活主节点数（用于 quorum）
    int master_count = 0;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Master) && !node->hasFlag(ClusterNodeFlag::Fail)) {
            ++master_count;
        }
    }
    int quorum = (master_count / 2) + 1;
    
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Myself)) continue;
        if (node->hasFlag(ClusterNodeFlag::Fail)) continue;
        
        // 如果长时间未收到 PONG，标记为 PFail，并添加自己发出的 failure report
        if (node->pong_received > 0 && (now_ms - node->pong_received) > timeout_ms) {
            if (!node->hasFlag(ClusterNodeFlag::PFail)) {
                node->setFlag(ClusterNodeFlag::PFail);
                if (m_myself) {
                    m_failure_reports[node->node_id].insert(m_myself->node_id);
                }
                ZERO_LOG_INFO(g_logger) << "Node " << node->node_id << " marked as pfail";
            }
        }
        
        // 通过 quorum 投票决定是否标记为 Fail
        if (node->hasFlag(ClusterNodeFlag::PFail)) {
            auto it = m_failure_reports.find(node->node_id);
            int reports = (it != m_failure_reports.end()) ? (int)it->second.size() : 0;
            if (reports >= quorum) {
                node->setFlag(ClusterNodeFlag::Fail);
                node->clearFlag(ClusterNodeFlag::PFail);
                ZERO_LOG_INFO(g_logger) << "Node " << node->node_id << " marked as fail (quorum "
                                         << reports << "/" << quorum << ")";
            }
        }
    }
}

int ClusterManager::getMasterCount() const {
    int count = 0;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Master) && 
            !node->hasFlag(ClusterNodeFlag::Fail)) {
            ++count;
        }
    }
    return count;
}

void ClusterManager::addFailureReport(const std::string& failed_node_id, const std::string& reporting_node_id) {
    RWMutex::WriteLock lock(m_mutex);
    if (failed_node_id == reporting_node_id) return;
    m_failure_reports[failed_node_id].insert(reporting_node_id);
}

void ClusterManager::clearFailureReport(const std::string& failed_node_id, const std::string& reporting_node_id) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_failure_reports.find(failed_node_id);
    if (it != m_failure_reports.end()) {
        it->second.erase(reporting_node_id);
        if (it->second.empty()) {
            m_failure_reports.erase(it);
        }
    }
}

bool ClusterManager::hasQuorumFailureReports(const std::string& failed_node_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_failure_reports.find(failed_node_id);
    if (it == m_failure_reports.end()) return false;
    int reports = (int)it->second.size();
    int master_count = 0;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Master) && !node->hasFlag(ClusterNodeFlag::Fail)) {
            ++master_count;
        }
    }
    int quorum = (master_count / 2) + 1;
    return reports >= quorum;
}

uint64_t ClusterManager::getConfigEpoch() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_config_epoch;
}

void ClusterManager::bumpConfigEpoch() {
    RWMutex::WriteLock lock(m_mutex);
    // 确保新 epoch 严格大于集群中已知的最大 configEpoch，避免冲突
    uint64_t max_epoch = m_config_epoch;
    for (const auto& p : m_nodes) {
        if (p.second->config_epoch > max_epoch) {
            max_epoch = p.second->config_epoch;
        }
    }
    m_config_epoch = std::max(max_epoch + 1, m_config_epoch + 1);
}

void ClusterManager::updateNodeSlots(const std::string& node_id, const std::vector<int>& slots, uint64_t epoch) {
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return;
    auto& target = it->second;
    if (target->hasFlag(ClusterNodeFlag::Slave)) return;
    if (epoch < target->config_epoch) return; // 忽略过时的槽位信息
    target->config_epoch = std::max(target->config_epoch, epoch);

    // 清空目标节点旧槽位，然后按冲突仲裁重新设置
    target->slots.reset();
    for (int slot : slots) {
        if (slot < 0 || slot >= kClusterSlotCount) continue;
        std::string current_owner;
        for (const auto& np : m_nodes) {
            const auto& n = np.second;
            if (n->hasFlag(ClusterNodeFlag::Slave)) continue;
            if (n->node_id == node_id) continue;
            if (n->slots.test(slot)) {
                current_owner = n->node_id;
                break;
            }
        }
        if (current_owner.empty()) {
            target->slots.set(slot);
        } else {
            auto owner_it = m_nodes.find(current_owner);
            uint64_t owner_epoch = (owner_it != m_nodes.end()) ? owner_it->second->config_epoch : 0;
            bool take_over = false;
            if (epoch > owner_epoch) {
                take_over = true;
            } else if (epoch == owner_epoch) {
                // epoch 相等时，node_id 字典序大者获胜（Redis 规则）
                take_over = node_id > current_owner;
            }
            if (take_over) {
                if (owner_it != m_nodes.end()) {
                    owner_it->second->slots.reset(slot);
                }
                target->slots.set(slot);
            }
        }
    }
    m_slot_owner_dirty = true;
}

void ClusterManager::setRequireFullCoverage(bool v) {
    RWMutex::WriteLock lock(m_mutex);
    m_require_full_coverage = v;
}

bool ClusterManager::requireFullCoverage() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_require_full_coverage;
}

void ClusterManager::setSlaveValidityFactor(int v) {
    RWMutex::WriteLock lock(m_mutex);
    m_slave_validity_factor = v;
}

int ClusterManager::getSlaveValidityFactor() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_slave_validity_factor;
}

bool ClusterManager::slaveIsValidForFailover(const std::string& replica_node_id, int64_t now_ms, int64_t timeout_ms) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(replica_node_id);
    if (it == m_nodes.end()) return false;
    const auto& replica = it->second;
    if (!replica->hasFlag(ClusterNodeFlag::Slave)) return false;
    if (m_slave_validity_factor == 0) return true; // 0 表示不检查
    if (replica->master_id.empty()) return false;
    auto mit = m_nodes.find(replica->master_id);
    if (mit == m_nodes.end()) return false;
    const auto& master = mit->second;
    int64_t disconnected_ms = now_ms - master->pong_received;
    int64_t max_disconnected_ms = timeout_ms * m_slave_validity_factor;
    return disconnected_ms <= max_disconnected_ms;
}

void ClusterManager::setMigrationBarrier(int v) {
    RWMutex::WriteLock lock(m_mutex);
    m_migration_barrier = v;
}

int ClusterManager::getMigrationBarrier() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_migration_barrier;
}

bool ClusterManager::canDonateReplica(const std::string& master_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(master_id);
    if(it == m_nodes.end()) return false;
    if(!it->second->hasFlag(ClusterNodeFlag::Master)) return false;
    int replicas = 0;
    for(const auto& p : m_nodes) {
        const auto& node = p.second;
        if(node->hasFlag(ClusterNodeFlag::Slave) && node->master_id == master_id) {
            ++replicas;
        }
    }
    return replicas >= m_migration_barrier;
}

void ClusterManager::reset(bool hard) {
    RWMutex::WriteLock lock(m_mutex);
    // SOFT: 保留 myself，清空槽位和节点；HARD: 重新生成 node_id
    m_slot_states = {};
    for(auto& s : m_slot_states) {
        s.type = SlotState::Normal;
    }
    if(hard) {
        m_my_node_id = generateNodeId();
        if(m_myself) {
            m_myself->node_id = m_my_node_id;
        }
    }
    if(m_myself) {
        m_myself->slots.reset();
        m_myself->setFlag(ClusterNodeFlag::Master);
        m_myself->clearFlag(ClusterNodeFlag::Slave);
        m_myself->master_id.clear();
        m_myself->config_epoch = 0;
    }
    m_nodes.clear();
    m_nodes[m_my_node_id] = m_myself;
    m_config_epoch = 0;
    m_slot_owner_dirty = true;
}

std::shared_ptr<ClusterNode> ClusterManager::findNodeByClientAddr(const std::string& addr) const {
    size_t colon = addr.find(':');
    if (colon == std::string::npos) return nullptr;
    std::string ip = addr.substr(0, colon);
    int port = std::stoi(addr.substr(colon + 1));
    return findNodeByAddr(ip, port);
}

std::string ClusterManager::getSlotOwner(int slot) const {
    if (slot < 0 || slot >= kClusterSlotCount) return "";
    RWMutex::ReadLock lock(m_mutex);
    if (m_slot_owner_dirty) {
        lock.unlock();
        rebuildSlotOwner();
        lock.lock();
    }
    const std::string& owner_id = m_slot_owner[slot];
    if (owner_id.empty()) return "";
    auto it = m_nodes.find(owner_id);
    if (it == m_nodes.end()) return "";
    const auto& node = it->second;
    return node->ip + ":" + std::to_string(node->port);
}

std::string ClusterManager::getImportingSource(int slot) const {
    if (slot < 0 || slot >= kClusterSlotCount) return "";
    RWMutex::ReadLock lock(m_mutex);
    if (m_slot_states[slot].type != SlotState::Importing) return "";
    const std::string& src_id = m_slot_states[slot].target_node_id;
    if (src_id.empty()) return "";
    auto it = m_nodes.find(src_id);
    if (it == m_nodes.end()) return "";
    return it->second->ip + ":" + std::to_string(it->second->port);
}

void ClusterManager::rebuildSlotOwner() const {
    RWMutex::WriteLock lock(m_mutex);
    for (auto& id : m_slot_owner) {
        id.clear();
    }
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Slave)) continue;
        for (int i = 0; i < kClusterSlotCount; ++i) {
            if (node->slots.test(i)) {
                m_slot_owner[i] = node->node_id;
            }
        }
    }
    m_slot_owner_dirty = false;
}

std::string ClusterManager::buildNodesString() const {
    RWMutex::ReadLock lock(m_mutex);
    std::ostringstream ss;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        // <id> <ip:port@cport> <flags> <master> <ping-sent> <pong-recv> <config-epoch> <link-state> <slot> ...
        ss << node->node_id << " ";
        ss << node->ip << ":" << node->port << "@" << node->cluster_port << " ";

        // flags
        std::vector<std::string> flags;
        if (node->hasFlag(ClusterNodeFlag::Myself)) flags.push_back("myself");
        if (node->hasFlag(ClusterNodeFlag::Master)) flags.push_back("master");
        if (node->hasFlag(ClusterNodeFlag::Slave)) flags.push_back("slave");
        if (node->hasFlag(ClusterNodeFlag::Fail)) flags.push_back("fail");
        if (node->hasFlag(ClusterNodeFlag::PFail)) flags.push_back("fail?");
        if (flags.empty()) flags.push_back("noflags");
        for (size_t i = 0; i < flags.size(); ++i) {
            if (i > 0) ss << ",";
            ss << flags[i];
        }
        ss << " ";

        // master
        if (node->hasFlag(ClusterNodeFlag::Slave) && !node->master_id.empty()) {
            ss << node->master_id;
        } else {
            ss << "-";
        }
        ss << " ";

        // ping-sent pong-recv config-epoch link-state
        ss << "0 0 0 connected ";  // 简化：固定值

        // slots
        if (node->hasFlag(ClusterNodeFlag::Master)) {
            std::vector<int> slots;
            for (int i = 0; i < kClusterSlotCount; ++i) {
                if (node->slots.test(i)) slots.push_back(i);
            }
            // 合并连续槽位
            if (!slots.empty()) {
                int start = slots[0];
                int prev = start;
                for (size_t i = 1; i <= slots.size(); ++i) {
                    if (i == slots.size() || slots[i] != prev + 1) {
                        if (start == prev) {
                            ss << start << " ";
                        } else {
                            ss << start << "-" << prev << " ";
                        }
                        if (i < slots.size()) {
                            start = slots[i];
                            prev = start;
                        }
                    } else {
                        prev = slots[i];
                    }
                }
            }
        }
        ss << "\n";
    }
    return ss.str();
}

RespValue ClusterManager::buildSlotsArray() const {
    RWMutex::ReadLock lock(m_mutex);
    RespValue arr;
    arr.type = RespType::Array;

    // 按节点分组，输出每个主节点的槽位范围
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Slave)) continue;

        std::vector<int> slots;
        for (int i = 0; i < kClusterSlotCount; ++i) {
            if (node->slots.test(i)) slots.push_back(i);
        }
        if (slots.empty()) continue;

        // 合并连续槽位为范围
        int start = slots[0];
        int prev = start;
        for (size_t i = 1; i <= slots.size(); ++i) {
            if (i == slots.size() || slots[i] != prev + 1) {
                RespValue range;
                range.type = RespType::Array;
                range.array.push_back(RespEncoder::integer(start));
                range.array.push_back(RespEncoder::integer(prev));

                // 主节点信息
                RespValue masterInfo;
                masterInfo.type = RespType::Array;
                masterInfo.array.push_back(RespEncoder::bulk(node->ip));
                masterInfo.array.push_back(RespEncoder::integer(node->port));
                masterInfo.array.push_back(RespEncoder::bulk(node->node_id));
                range.array.push_back(masterInfo);

                // 副本信息（如果有）
                RespValue replicas;
                replicas.type = RespType::Array;
                for (const auto& rp : m_nodes) {
                    const auto& replica = rp.second;
                    if (replica->hasFlag(ClusterNodeFlag::Slave) &&
                        replica->master_id == node->node_id) {
                        RespValue repInfo;
                        repInfo.type = RespType::Array;
                        repInfo.array.push_back(RespEncoder::bulk(replica->ip));
                        repInfo.array.push_back(RespEncoder::integer(replica->port));
                        repInfo.array.push_back(RespEncoder::bulk(replica->node_id));
                        replicas.array.push_back(repInfo);
                    }
                }
                range.array.push_back(replicas);

                arr.array.push_back(range);

                if (i < slots.size()) {
                    start = slots[i];
                    prev = start;
                }
            } else {
                prev = slots[i];
            }
        }
    }
    return arr;
}

RespValue ClusterManager::buildInfo() const {
    RWMutex::ReadLock lock(m_mutex);
    RespValue arr;
    arr.type = RespType::Array;

    int master_count = 0;
    int slave_count = 0;
    int assigned_slots = 0;
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Master)) ++master_count;
        if (node->hasFlag(ClusterNodeFlag::Slave)) ++slave_count;
    }
    if (m_myself) {
        for (int i = 0; i < kClusterSlotCount; ++i) {
            if (m_myself->slots.test(i)) ++assigned_slots;
        }
    }

    auto addPair = [&](const std::string& k, const std::string& v) {
        arr.array.push_back(RespEncoder::bulk(k));
        arr.array.push_back(RespEncoder::bulk(v));
    };

    addPair("cluster_state", assigned_slots == kClusterSlotCount ? "ok" : "fail");
    addPair("cluster_slots_assigned", std::to_string(assigned_slots));
    addPair("cluster_slots_ok", std::to_string(assigned_slots));
    addPair("cluster_slots_pfail", "0");
    addPair("cluster_slots_fail", "0");
    addPair("cluster_known_nodes", std::to_string(m_nodes.size()));
    addPair("cluster_size", std::to_string(master_count));
    addPair("cluster_current_epoch", "0");
    addPair("cluster_my_epoch", "0");
    addPair("cluster_stats_messages_sent", "0");
    addPair("cluster_stats_messages_received", "0");

    return arr;
}

bool ClusterManager::setSlotMigrating(int slot, const std::string& target_node_id) {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::WriteLock lock(m_mutex);
    if (!m_myself || !m_myself->slots.test(slot)) return false;
    m_slot_states[slot].type = SlotState::Migrating;
    m_slot_states[slot].target_node_id = target_node_id;
    return true;
}

bool ClusterManager::setSlotImporting(int slot, const std::string& source_node_id) {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::WriteLock lock(m_mutex);
    if (!m_myself || m_myself->slots.test(slot)) return false;
    m_slot_states[slot].type = SlotState::Importing;
    m_slot_states[slot].target_node_id = source_node_id;
    return true;
}

bool ClusterManager::setSlotStable(int slot) {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::WriteLock lock(m_mutex);
    m_slot_states[slot].type = SlotState::Normal;
    m_slot_states[slot].target_node_id.clear();
    return true;
}

bool ClusterManager::finishMigration(int slot) {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::WriteLock lock(m_mutex);
    if (m_slot_states[slot].type != SlotState::Migrating) return false;
    const std::string& target_id = m_slot_states[slot].target_node_id;
    if (target_id.empty()) return false;
    // 将槽位从本节点移除，标记为目标节点
    if (m_myself) {
        m_myself->slots.reset(slot);
    }
    auto it = m_nodes.find(target_id);
    if (it != m_nodes.end()) {
        it->second->slots.set(slot);
    }
    m_slot_states[slot].type = SlotState::Normal;
    m_slot_states[slot].target_node_id.clear();
    m_slot_owner_dirty = true;
    return true;
}

bool ClusterManager::setSlotOwner(int slot, const std::string& node_id, uint64_t epoch) {
    if (slot < 0 || slot >= kClusterSlotCount) return false;
    RWMutex::WriteLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return false;
    auto& target = it->second;
    if (target->hasFlag(ClusterNodeFlag::Slave)) return false;

    // 从所有其他主节点中移除该槽位
    for (auto& p : m_nodes) {
        auto& node = p.second;
        if (node->hasFlag(ClusterNodeFlag::Slave)) continue;
        if (node->node_id == node_id) continue;
        node->slots.reset(slot);
    }
    target->slots.set(slot);
    m_slot_states[slot].type = SlotState::Normal;
    m_slot_states[slot].target_node_id.clear();
    m_slot_owner_dirty = true;
    if (epoch > m_config_epoch) {
        m_config_epoch = epoch;
    } else {
        ++m_config_epoch;
    }
    return true;
}

std::vector<int> ClusterManager::getNodeSlots(const std::string& node_id) const {
    RWMutex::ReadLock lock(m_mutex);
    auto it = m_nodes.find(node_id);
    if (it == m_nodes.end()) return {};
    std::vector<int> slots;
    for (int i = 0; i < kClusterSlotCount; ++i) {
        if (it->second->slots.test(i)) slots.push_back(i);
    }
    return slots;
}

std::string ClusterManager::myNodeId() const {
    RWMutex::ReadLock lock(m_mutex);
    return m_my_node_id;
}

std::vector<std::shared_ptr<ClusterNode>> ClusterManager::getAllNodes() const {
    RWMutex::ReadLock lock(m_mutex);
    std::vector<std::shared_ptr<ClusterNode>> out;
    out.reserve(m_nodes.size());
    for (const auto& p : m_nodes) {
        out.push_back(p.second);
    }
    return out;
}

std::shared_ptr<ClusterNode> ClusterManager::findNodeByAddr(const std::string& ip, int port) const {
    for (const auto& p : m_nodes) {
        const auto& node = p.second;
        if (node->ip == ip && node->port == port) {
            return node;
        }
    }
    return nullptr;
}

std::vector<std::string> ClusterManager::getKeysInSlot(int slot, int count, KvStore_ptr store) const {
    std::vector<std::string> out;
    if(slot < 0 || slot >= kClusterSlotCount || !store || count <= 0) return out;
    // 扫描所有 DB（Redis Cluster 中每个节点仍支持多个 DB，但命令默认 DB 0）
    for(int db = 0; db < store->getMaxDb(); ++db) {
        std::vector<std::string> all = store->keys(db, "*");
        for(const auto& key : all) {
            if(keyToSlot(key) == slot) {
                out.push_back(key);
                if((int)out.size() >= count) return out;
            }
        }
        if((int)out.size() >= count) return out;
    }
    return out;
}

} // namespace kv
} // namespace zero

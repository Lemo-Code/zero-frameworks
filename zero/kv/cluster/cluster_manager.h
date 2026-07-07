/**
 * @file cluster_manager.h
 * @brief Redis Cluster 状态管理（节点、槽位、拓扑）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_CLUSTER_CLUSTER_MANAGER_H__
#define __ZERO_KV_CLUSTER_CLUSTER_MANAGER_H__

#include "cluster_slot.h"
#include "zero/kv/resp.h"
#include "zero/core/concurrency/mutex.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zero {
namespace kv {

class KvStore;

typedef std::shared_ptr<KvStore> KvStore_ptr;

// 节点标志位
enum class ClusterNodeFlag : uint16_t {
    Master = 1,      // 主节点
    Slave = 2,       // 副本
    Myself = 4,      // 本节点
    PFail = 8,       // 可能失效
    Fail = 16,       // 已失效
    NoAddr = 32,     // 无地址
};

struct ClusterNode {
    std::string node_id;           // 40 字符 hex
    std::string ip;
    int port = 0;                  // 客户端端口
    int cluster_port = 0;          // 集群总线端口
    uint16_t flags = 0;            // ClusterNodeFlag 位掩码
    std::string master_id;         // 主节点 ID（如果是副本）
    std::bitset<kClusterSlotCount> slots;  // 负责的槽位
    uint64_t config_epoch = 0;     // 配置 epoch
    int64_t ping_sent = 0;         // 上次发送 PING 时间 (ms)
    int64_t pong_received = 0;     // 上次收到 PONG 时间 (ms)

    bool hasFlag(ClusterNodeFlag f) const {
        return (flags & static_cast<uint16_t>(f)) != 0;
    }
    void setFlag(ClusterNodeFlag f) {
        flags |= static_cast<uint16_t>(f);
    }
    void clearFlag(ClusterNodeFlag f) {
        flags &= ~static_cast<uint16_t>(f);
    }
};

class ClusterManager {
public:
    typedef std::shared_ptr<ClusterManager> ptr;

    ClusterManager();
    ~ClusterManager();

    // 初始化本节点
    void init(const std::string& my_node_id,
              const std::string& bind_ip,
              int port,
              int cluster_port,
              const std::string& announce_ip = std::string(),
              int announce_port = 0);

    // 从 nodes.conf 加载后重置 myself 引用
    void setMyselfFromConfig(const std::shared_ptr<ClusterNode>& myself_node);

    // 是否已启用集群模式
    bool enabled() const { return m_enabled; }

    // 槽位查询
    int keyToSlot(const std::string& key) const;
    bool isMySlot(int slot) const;
    bool isImportingSlot(int slot) const;
    bool isMigratingSlot(int slot) const;

    // 节点管理
    std::shared_ptr<ClusterNode> getNode(const std::string& node_id) const;
    std::shared_ptr<ClusterNode> getMyself() const;
    void addNode(const std::shared_ptr<ClusterNode>& node);
    bool removeNode(const std::string& node_id);

    // CLUSTER MEET
    bool meetNode(const std::string& ip, int port);

    // 槽位分配
    void addSlots(const std::vector<int>& slots);
    void delSlots(const std::vector<int>& slots);

    // 副本设置 - 同时启动 PSYNC 同步到主节点
    void setReplication(const std::string& master_id);
    bool isSlave() const;
    std::string getMasterId() const;
    
    // 获取主节点地址 (用于 PSYNC)
    std::string getMasterAddr() const;  // 返回 "ip:port"
    
    // 故障检测
    void updateNodePingSent(const std::string& node_id, int64_t now_ms);
    void updateNodePongReceived(const std::string& node_id, int64_t now_ms);
    void markNodePFail(const std::string& node_id);
    void markNodeFail(const std::string& node_id);
    void clearNodeFail(const std::string& node_id);
    bool isNodeFailed(const std::string& node_id) const;
    bool isNodePFail(const std::string& node_id) const;
    
    // Failure report（quorum 投票）
    void addFailureReport(const std::string& failed_node_id, const std::string& reporting_node_id);
    void clearFailureReport(const std::string& failed_node_id, const std::string& reporting_node_id);
    bool hasQuorumFailureReports(const std::string& failed_node_id) const;
    int getMasterCount() const;
    
    // 获取主节点的副本列表
    std::vector<std::shared_ptr<ClusterNode>> getReplicas(const std::string& master_id) const;
    
    // 自动故障转移：将副本提升为主节点
    bool failoverReplica(const std::string& replica_node_id);
    
    // 获取主节点地址（用于 CLUSTER REPLICATE 后连接）
    std::shared_ptr<ClusterNode> getMasterNode() const;

    // 重定向
    std::string getSlotOwner(int slot) const;               // "ip:port"
    std::string getImportingSource(int slot) const;          // "ip:port" for ASK

    // 拓扑查询输出
    std::string buildNodesString() const;
    RespValue buildSlotsArray() const;
    RespValue buildInfo() const;

    // 迁移状态管理
    bool setSlotMigrating(int slot, const std::string& target_node_id);
    bool setSlotImporting(int slot, const std::string& source_node_id);
    bool setSlotStable(int slot);
    bool finishMigration(int slot);

    // 将槽位分配给指定节点（CLUSTER SETSLOT NODE），返回是否成功
    bool setSlotOwner(int slot, const std::string& node_id, uint64_t epoch = 0);

    // 获取节点负责的槽位列表
    std::vector<int> getNodeSlots(const std::string& node_id) const;

    // 获取指定槽位中的 key 列表（从 store 扫描，最多 count 个）
    std::vector<std::string> getKeysInSlot(int slot, int count, KvStore_ptr store) const;

    // 获取本节点 ID
    std::string myNodeId() const;

    // 获取本节点地址
    std::string myAddr() const;

    // 获取所有节点
    std::vector<std::shared_ptr<ClusterNode>> getAllNodes() const;

    // 获取存活的主节点列表（用于故障转移选择）
    std::vector<std::shared_ptr<ClusterNode>> getAliveMasters() const;

    // 定时故障检测（由外部定时器调用）
    void checkFailureDetection(int64_t now_ms, int64_t timeout_ms = 15000);

    // 获取本节点配置 epoch（用于故障转移投票）
    uint64_t getConfigEpoch() const;
    void bumpConfigEpoch();

    // 带 epoch 仲裁的槽位更新（用于总线消息合并）
    void updateNodeSlots(const std::string& node_id, const std::vector<int>& slots, uint64_t epoch);

    // cluster-require-full-coverage
    void setRequireFullCoverage(bool v);
    bool requireFullCoverage() const;

    // cluster-slave-validity-factor：副本故障转移资格检查
    void setSlaveValidityFactor(int v);
    int getSlaveValidityFactor() const;
    bool slaveIsValidForFailover(const std::string& replica_node_id, int64_t now_ms, int64_t timeout_ms) const;

    // cluster-migration-barrier：主节点至少拥有多少个副本时才允许迁出副本
    void setMigrationBarrier(int v);
    int getMigrationBarrier() const;
    bool canDonateReplica(const std::string& master_id) const;

    // CLUSTER RESET [SOFT|HARD]
    void reset(bool hard);

private:
    mutable RWMutex m_mutex;
    bool m_enabled = false;
    std::string m_my_node_id;
    std::string m_bind_ip;
    int m_port = 0;
    int m_cluster_port = 0;
    std::string m_announce_ip;
    int m_announce_port = 0;
    uint64_t m_config_epoch = 0;
    bool m_require_full_coverage = false;
    int m_slave_validity_factor = 10;   // Redis 默认 10
    int m_migration_barrier = 1;        // Redis 默认 1

    // failure_reports[node_id] = 报告该节点失效的节点 ID 集合
    std::unordered_map<std::string, std::unordered_set<std::string>> m_failure_reports;

    std::unordered_map<std::string, std::shared_ptr<ClusterNode>> m_nodes;
    std::shared_ptr<ClusterNode> m_myself;

    struct SlotState {
        enum Type { Normal, Migrating, Importing } type = Normal;
        std::string target_node_id;   // 迁出目标 / 导入源节点 ID
    };
    std::array<SlotState, kClusterSlotCount> m_slot_states;

    // 槽位到节点 ID 的反向映射（只缓存，不持久化）
    mutable std::array<std::string, kClusterSlotCount> m_slot_owner;
    mutable bool m_slot_owner_dirty = true;

    void rebuildSlotOwner() const;
    std::shared_ptr<ClusterNode> findNodeByAddr(const std::string& ip, int port) const;
    std::shared_ptr<ClusterNode> findNodeByClientAddr(const std::string& addr) const;  // ip:port
};

// 全局访问（类似 bindReplicationForConfig）
void bindClusterManager(ClusterManager::ptr mgr);
ClusterManager::ptr getClusterManager();

} // namespace kv
} // namespace zero

#endif

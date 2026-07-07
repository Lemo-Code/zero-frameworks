/**
 * @file cluster_commands.cc
 * @brief CLUSTER 子命令实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cluster_manager.h"
#include "cluster_bus.h"
#include "cluster_config.h"
#include "cluster_migrate.h"
#include "cluster_slot.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/command_dispatch.h"
#include "zero/kv/resp.h"
#include "zero/kv/kv_context.h"
#include "zero/kv/store/kv_store.h"
#include "zero/core/log/log.h"
#include <cstdlib>
#include <sstream>

namespace zero {
namespace kv {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static bool parseSlot(const RespValue& v, int& slot_out) {
    if(v.is_null || v.type != RespType::BulkString || v.str.empty()) {
        return false;
    }
    char* end = nullptr;
    long n = std::strtol(v.str.c_str(), &end, 10);
    if(end == v.str.c_str() || *end != '\0' || n < 0 || n >= kClusterSlotCount) {
        return false;
    }
    slot_out = (int)n;
    return true;
}

static std::string upperArg(const std::string& s) {
    std::string out = s;
    for(char& c : out) {
        c = (char)std::toupper((unsigned char)c);
    }
    return out;
}

void registerClusterCommands(CommandDispatch::ptr dispatch) {
    // CLUSTER <subcommand> [args ...]
    dispatch->addCommand("CLUSTER", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'cluster' command");
        }
        std::string sub = upperArg(req.array[1].str);
        ClusterManager::ptr cluster = getClusterManager();
        if(!cluster || !cluster->enabled()) {
            return RespEncoder::err("ERR This instance has cluster support disabled");
        }

        if(sub == "INFO") {
            return cluster->buildInfo();
        }

        if(sub == "NODES") {
            return RespEncoder::bulk(cluster->buildNodesString());
        }

        if(sub == "SLOTS") {
            return cluster->buildSlotsArray();
        }

        if(sub == "MYID") {
            return RespEncoder::bulk(cluster->myNodeId());
        }

        if(sub == "REPLICAS") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|replicas' command");
            }
            auto replicas = cluster->getReplicas(req.array[2].str);
            RespValue arr;
            arr.type = RespType::Array;
            for(const auto& r : replicas) {
                RespValue info;
                info.type = RespType::Array;
                info.array.push_back(RespEncoder::bulk("id=" + r->node_id));
                info.array.push_back(RespEncoder::bulk(r->ip + ":" + std::to_string(r->port)));
                info.array.push_back(RespEncoder::bulk("flags=" + std::string(r->hasFlag(ClusterNodeFlag::Slave) ? "slave" : "master")));
                arr.array.push_back(info);
            }
            return arr;
        }

        if(sub == "BUMPEPOCH") {
            cluster->bumpConfigEpoch();
            return RespEncoder::integer((int64_t)cluster->getConfigEpoch());
        }

        if(sub == "RESET") {
            bool hard = false;
            if(req.array.size() >= 3 && !req.array[2].is_null) {
                std::string mode = upperArg(req.array[2].str);
                hard = (mode == "HARD");
            }
            cluster->reset(hard);
            return RespEncoder::ok();
        }

        if(sub == "FAILOVER") {
            // 简化：立即提升自己为主节点（强制故障转移）
            if(cluster->isSlave()) {
                std::string master_id = cluster->getMasterId();
                cluster->markNodeFail(master_id);
                cluster->failoverReplica(cluster->myNodeId());
                ReplicationManager::ptr repl = getReplicationForConfig();
                if(repl) repl->promoteToMaster();
                ClusterBus* bus = getClusterBus();
                if(bus) bus->broadcastSlotUpdate(0, cluster->myNodeId());
                return RespEncoder::ok();
            }
            return RespEncoder::err("ERR CLUSTER FAILOVER called on a master");
        }

        if(sub == "SAVECONFIG") {
            ClusterConfig::ptr config;
            // 通过 server 获取 config 比较麻烦，这里简化：
            // KvServer 启动时设置的 config 路径固定为 nodes.conf
            config.reset(new ClusterConfig("nodes.conf"));
            if(config->save(cluster.get())) {
                return RespEncoder::ok();
            }
            return RespEncoder::err("ERR Unable to save cluster config");
        }

        if(sub == "KEYSLOT") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|keyslot' command");
            }
            int slot = keyToSlot(req.array[2].str);
            return RespEncoder::integer(slot);
        }

        if(sub == "COUNTKEYSINSLOT") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|countkeysinslot' command");
            }
            int slot = std::stoi(req.array[2].str);
            if(slot < 0 || slot >= kClusterSlotCount) {
                return RespEncoder::err("ERR Invalid slot");
            }
            auto keys = cluster->getKeysInSlot(slot, 1000000, store);
            return RespEncoder::integer((int64_t)keys.size());
        }

        if(sub == "GETKEYSINSLOT") {
            if(req.array.size() != 4 || req.array[2].is_null || req.array[3].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|getkeysinslot' command");
            }
            int slot = std::stoi(req.array[2].str);
            int count = std::stoi(req.array[3].str);
            if(slot < 0 || slot >= kClusterSlotCount || count < 0) {
                return RespEncoder::err("ERR Invalid slot or count");
            }
            auto keys = cluster->getKeysInSlot(slot, count, store);
            RespValue arr;
            arr.type = RespType::Array;
            for(const auto& k : keys) {
                arr.array.push_back(RespEncoder::bulk(k));
            }
            return arr;
        }

        if(sub == "MEET") {
            if(req.array.size() < 4 || req.array[2].is_null || req.array[3].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|meet' command");
            }
            std::string ip = req.array[2].str;
            int port = (int)std::strtol(req.array[3].str.c_str(), nullptr, 10);
            if(port <= 0 || port > 65535) {
                return RespEncoder::err("ERR Invalid node address specified: " + ip + ":" + req.array[3].str);
            }
            int cluster_port = port + 10000;
            if(req.array.size() >= 5 && !req.array[4].is_null) {
                cluster_port = (int)std::strtol(req.array[4].str.c_str(), nullptr, 10);
            }
            // 先创建本地记录
            if(!cluster->meetNode(ip, port)) {
                return RespEncoder::err("ERR CLUSTER MEET failed");
            }
            // 通过 cluster bus 发送真实 MEET
            ClusterBus* bus = getClusterBus();
            if(bus) {
                bus->meetNode(ip, port, cluster_port);
            }
            return RespEncoder::ok();
        }

        if(sub == "ADDSLOTS") {
            if(req.array.size() < 3) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|addslots' command");
            }
            std::vector<int> slots;
            for(size_t i = 2; i < req.array.size(); ++i) {
                int slot = 0;
                if(!parseSlot(req.array[i], slot)) {
                    return RespEncoder::err("ERR Invalid slot: " + (req.array[i].is_null ? "" : req.array[i].str));
                }
                slots.push_back(slot);
            }
            cluster->addSlots(slots);
            return RespEncoder::ok();
        }

        if(sub == "DELSLOTS") {
            if(req.array.size() < 3) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|delslots' command");
            }
            std::vector<int> slots;
            for(size_t i = 2; i < req.array.size(); ++i) {
                int slot = 0;
                if(!parseSlot(req.array[i], slot)) {
                    return RespEncoder::err("ERR Invalid slot: " + (req.array[i].is_null ? "" : req.array[i].str));
                }
                slots.push_back(slot);
            }
            cluster->delSlots(slots);
            return RespEncoder::ok();
        }

        if(sub == "REPLICATE") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|replicate' command");
            }
            std::string master_id = req.array[2].str;
            cluster->setReplication(master_id);
            
            // 获取主节点地址并启动 PSYNC 同步
            auto master_node = cluster->getMasterNode();
            if(master_node) {
                ReplicationManager::ptr repl = getReplicationForConfig();
                if(repl) {
                    repl->setSlaveOf(master_node->ip, master_node->port);
                    ZERO_LOG_INFO(g_logger) << "Cluster replica started PSYNC to " 
                                             << master_node->ip << ":" << master_node->port;
                }
            }
            return RespEncoder::ok();
        }

        if(sub == "FORGET") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|forget' command");
            }
            if(!cluster->removeNode(req.array[2].str)) {
                return RespEncoder::err("ERR Unknown node " + req.array[2].str);
            }
            return RespEncoder::ok();
        }

        if(sub == "SETSLOT") {
            if(req.array.size() < 4 || req.array[2].is_null || req.array[3].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'cluster|setslot' command");
            }
            int slot = 0;
            if(!parseSlot(req.array[2], slot)) {
                return RespEncoder::err("ERR Invalid slot");
            }
            std::string action = upperArg(req.array[3].str);
            if(action == "MIGRATING" && req.array.size() >= 5 && !req.array[4].is_null) {
                if(!cluster->setSlotMigrating(slot, req.array[4].str)) {
                    return RespEncoder::err("ERR Can't set slot migrating");
                }
                return RespEncoder::ok();
            }
            if(action == "IMPORTING" && req.array.size() >= 5 && !req.array[4].is_null) {
                if(!cluster->setSlotImporting(slot, req.array[4].str)) {
                    return RespEncoder::err("ERR Can't set slot importing");
                }
                return RespEncoder::ok();
            }
            if(action == "STABLE") {
                if(!cluster->setSlotStable(slot)) {
                    return RespEncoder::err("ERR Can't set slot stable");
                }
                return RespEncoder::ok();
            }
            if(action == "NODE" && req.array.size() >= 5 && !req.array[4].is_null) {
                std::string node_id = req.array[4].str;
                if(!cluster->setSlotOwner(slot, node_id)) {
                    return RespEncoder::err("ERR Can't set slot owner");
                }
                // 广播 UPDATE 让其他节点知道槽位变更
                ClusterBus* bus = getClusterBus();
                if(bus) {
                    bus->broadcastSlotUpdate(slot, node_id);
                }
                return RespEncoder::ok();
            }
            return RespEncoder::err("ERR Unknown CLUSTER SETSLOT action: " + action);
        }

        return RespEncoder::err("ERR Unknown subcommand or wrong number of arguments for 'cluster'");
    });

    // READONLY - 允许从副本读取
    dispatch->addCommand("READONLY", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        ClusterManager::ptr cluster = getClusterManager();
        if(!cluster || !cluster->enabled()) {
            return RespEncoder::err("ERR This instance has cluster support disabled");
        }
        // 标记会话为 readonly 模式（副本读取）
        ctx.readonly = true;
        return RespEncoder::ok();
    });

    // READWRITE - 取消只读模式
    dispatch->addCommand("READWRITE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        ClusterManager::ptr cluster = getClusterManager();
        if(!cluster || !cluster->enabled()) {
            return RespEncoder::err("ERR This instance has cluster support disabled");
        }
        ctx.readonly = false;
        return RespEncoder::ok();
    });

    // ASKING - 允许下一次命令访问 importing 槽位
    dispatch->addCommand("ASKING", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        ClusterManager::ptr cluster = getClusterManager();
        if(!cluster || !cluster->enabled()) {
            return RespEncoder::err("ERR This instance has cluster support disabled");
        }
        ctx.asking = true;
        return RespEncoder::ok();
    });

    // MIGRATE - 键迁移
    dispatch->addCommand("MIGRATE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return handleMigrate(ctx, req, store);
    });
}

} // namespace kv
} // namespace zero

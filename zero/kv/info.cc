/**
 * @file info.cc
 * @brief INFO 命令实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "info.h"
#include "replication/replication.h"

#include "zero/util/json_util.h"

#include <json/json.h>
#include <sstream>

namespace zero {
namespace kv {

std::string buildInfoText(int db, KvStore::ptr store) {
    std::ostringstream ss;
    const int64_t keys = store->dbsize(db);
    const int64_t expires = store->expireCount(db);
    ss << "# Server\r\n";
    ss << "redis_version:7.0.0\r\n";
    ss << "redis_mode:standalone\r\n";
    ss << "redis_engine:zero-redis\r\n";
    ss << "os:Linux\r\n";
    ss << "arch_bits:64\r\n";
    ss << "# Keyspace\r\n";
    ss << "db" << db << ":keys=" << keys
       << ",expires=" << expires << ",avg_ttl=0\r\n";
    ss << "# Persistence\r\n";
    ss << "rdb_last_save_time:" << store->lastSaveUnixSec() << "\r\n";
    ss << "rdb_path:" << store->getRdbPath() << "\r\n";
    ss << "# Memory\r\n";
    ss << "used_memory:" << store->usedMemoryApprox() << "\r\n";
    ss << "maxmemory:" << store->maxMemory() << "\r\n";
    ss << "maxmemory_policy:" << store->maxMemoryPolicy() << "\r\n";
    ReplicationManager::ptr repl = getReplicationForConfig();
    if(repl) {
        ss << "# Replication\r\n";
        if(repl->role() == ReplicationManager::Role::Master) {
            ss << "role:master\r\n";
            ss << "connected_slaves:" << repl->connectedSlaves() << "\r\n";
            ss << "master_replid:" << repl->replId() << "\r\n";
            ss << "master_repl_offset:" << repl->replOffset() << "\r\n";
        } else {
            ss << "role:slave\r\n";
            ss << "master_host:" << repl->masterHost() << "\r\n";
            ss << "master_port:" << repl->masterPort() << "\r\n";
            ss << "master_link_status:" << repl->masterLinkStatus() << "\r\n";
            ss << "master_replid_saved:" << repl->masterReplId() << "\r\n";
            ss << "master_repl_offset_saved:" << repl->masterReplOffset() << "\r\n";
        }
    }
    return ss.str();
}

std::string buildInfoJson(int db, KvStore::ptr store) {
    Json::Value root;
    root["redis_version"] = "7.0.0";
    root["redis_mode"] = "standalone";
    root["redis_engine"] = "zero-redis";
    root["db"] = db;
    root["keys"] = (Json::Int64)store->dbsize(db);
    root["expires"] = (Json::Int64)store->expireCount(db);
    root["rdb_last_save_time"] = (Json::Int64)store->lastSaveUnixSec();
    root["rdb_path"] = store->getRdbPath();
    root["used_memory"] = (Json::Int64)store->usedMemoryApprox();
    root["maxmemory"] = (Json::Int64)store->maxMemory();
    root["maxmemory_policy"] = store->maxMemoryPolicy();
    ReplicationManager::ptr repl = getReplicationForConfig();
    if(repl) {
        root["role"] = repl->role() == ReplicationManager::Role::Master ? "master" : "slave";
        root["master_replid"] = repl->replId();
        root["master_repl_offset"] = (Json::Int64)repl->replOffset();
        if(repl->role() == ReplicationManager::Role::Slave) {
            root["master_repl_id_saved"] = repl->masterReplId();
            root["master_repl_offset_saved"] = (Json::Int64)repl->masterReplOffset();
        }
    }
    return JsonUtil::ToString(root);
}

} // namespace kv
} // namespace zero

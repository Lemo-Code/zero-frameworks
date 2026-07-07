/**
 * @file replication.h
 * @brief Redis 主从复制（ROLE / SLAVEOF / PSYNC / backlog / 从库同步）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_REPLICATION_REPLICATION_H__
#define __ZERO_KV_REPLICATION_REPLICATION_H__

#include "zero/core/concurrency/mutex.h"
#include "zero/kv/resp.h"
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace zero {
class IOManager;
namespace kv {

class CommandDispatch;
class KvStore;
class KvSession;

class ReplicationManager {
public:
    typedef std::shared_ptr<ReplicationManager> ptr;

    enum class Role {
        Master,
        Slave
    };

    struct PsyncResult {
        enum class Kind { Full, Partial, Error };
        Kind kind = Kind::Error;
        RespValue line;
        std::string payload;
    };

    ReplicationManager();

    Role role() const;
    const std::string& replId() const;
    int64_t replOffset() const;
    int connectedSlaves() const;

    const std::string& masterHost() const;
    int masterPort() const;
    int64_t masterLinkStatus() const;
    const std::string& masterReplId() const;
    int64_t masterReplOffset() const;

    void setSlaveOf(const std::string& host, int port);
    void promoteToMaster();

    void configureSlaveSync(std::shared_ptr<KvStore> store, std::shared_ptr<CommandDispatch> dispatch,
                            zero::IOManager* io_worker);
    void scheduleSlaveSync();
    void scheduleSlaveReconnect();

    void registerReplica(const std::shared_ptr<KvSession>& session);
    void removeReplica(const std::shared_ptr<KvSession>& session);

    void propagateCommand(const RespValue& command);

    PsyncResult handlePsync(const std::string& replid, int64_t offset, std::shared_ptr<KvStore> store) const;

private:
    struct BacklogChunk {
        int64_t start_offset = 0;
        std::string payload;
    };

    void appendBacklog(const std::string& payload);
    bool tryCollectPartial(const std::string& replid, int64_t offset, std::string& out) const;
    void fanoutToReplicas(const std::string& encoded);
    void runSlaveSyncOnce();

    static const size_t kMaxBacklogBytes = 1024 * 1024;

    mutable zero::Mutex m_mutex;
    Role m_role = Role::Master;
    std::string m_repl_id;
    int64_t m_offset = 0;
    std::deque<BacklogChunk> m_backlog;
    size_t m_backlog_bytes = 0;

    std::vector<std::weak_ptr<KvSession>> m_replicas;

    std::string m_master_host;
    int m_master_port = 0;
    int64_t m_master_link_status = 0;
    std::string m_master_repl_id;
    int64_t m_master_offset = -1;

    std::shared_ptr<KvStore> m_sync_store;
    std::shared_ptr<CommandDispatch> m_sync_dispatch;
    zero::IOManager* m_sync_io = nullptr;
    std::weak_ptr<KvSession> m_master_sync;
};

void bindReplicationForConfig(ReplicationManager::ptr repl);
ReplicationManager::ptr getReplicationForConfig();

} // namespace kv
} // namespace zero

#endif

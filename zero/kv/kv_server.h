/**
 * @file kv_server.h
 * @brief Redis 服务器（类比 http::HttpServer，继承 TcpServer）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_KV_SERVER_H__
#define __ZERO_KV_KV_SERVER_H__

#include "command_dispatch.h"
#include "persistence/aof.h"
#include "pubsub/pubsub_hub.h"
#include "blocking/blocking_list_hub.h"
#include "replication/replication.h"
#include "store/kv_store.h"
#include "zero/net/tcp/tcp_server.h"
#include "zero/core/concurrency/timer.h"
#include "zero/core/concurrency/mutex.h"
#include <atomic>
#include <unordered_map>

namespace zero {
namespace kv {

class KvSession;

class KvServer : public TcpServer {
public:
    typedef std::shared_ptr<KvServer> ptr;

    KvServer(KvStore::ptr store,
                CommandDispatch::ptr dispatch = nullptr,
                zero::IOManager* worker = zero::IOManager::GetThis(),
                zero::IOManager* io_worker = zero::IOManager::GetThis(),
                zero::IOManager* accept_worker = zero::IOManager::GetThis());

    CommandDispatch::ptr getDispatch() const { return m_dispatch; }
    KvStore::ptr getStore() const { return m_store; }
    PubSubHub::ptr getPubSub() const { return m_pubsub; }
    AofLog::ptr getAof() const { return m_aof; }
    ReplicationManager::ptr getReplication() const { return m_replication; }

    void setName(const std::string& v) override;
    bool start() override;
    void stop() override;

    void setAutoSaveSec(int sec);
    int getAutoSaveSec() const;

protected:
    void handleClient(Socket::ptr client) override;

private:
    KvStore::ptr m_store;
    CommandDispatch::ptr m_dispatch;
    PubSubHub::ptr m_pubsub;
    BlockingListHub::ptr m_blocking;
    AofLog::ptr m_aof;
    ReplicationManager::ptr m_replication;
    zero::Timer::ptr m_expireTimer;
    zero::Timer::ptr m_saveTimer;
    zero::Timer::ptr m_aofSyncTimer;
    std::atomic<bool> m_bgsaveRunning;
    std::atomic<bool> m_bgrewriteRunning;
    std::atomic<int64_t> m_nextClientId;
    zero::Mutex m_clientsMutex;
    std::unordered_map<int64_t, std::weak_ptr<KvSession>> m_clientSessions;
    int m_autoSaveSec = 60;
};

} // namespace kv
} // namespace zero

#endif

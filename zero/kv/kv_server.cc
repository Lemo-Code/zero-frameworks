/**
 * @file kv_server.cc
 * @brief KV 服务器主入口
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_server.h"
#include "kv_session.h"
#include "command_dispatch.h"
#include "kv_config.h"
#include "zero/core/log/log.h"
#include "zero/core/concurrency/timer.h"
#include "zero/core/concurrency/fiber.h"
#include <atomic>

namespace zero {
namespace kv {

namespace {

const uint64_t kExpireSweepMs = 100;
const int kExpireScanPerDb = 20;

std::string upperCmd(const RespValue& request) {
    if(request.type != RespType::Array || request.array.empty()) {
        return "";
    }
    const RespValue& cmd = request.array[0];
    if(cmd.type != RespType::BulkString || cmd.is_null) {
        return "";
    }
    std::string name = cmd.str;
    for(char& c : name) {
        c = (char)std::toupper((unsigned char)c);
    }
    return name;
}

}

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

KvServer::KvServer(KvStore::ptr store, CommandDispatch::ptr dispatch,
                         zero::IOManager* worker, zero::IOManager* io_worker,
                         zero::IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker)
    ,m_store(std::move(store))
    ,m_pubsub(new PubSubHub)
    ,m_blocking(new BlockingListHub)
    ,m_aof(new AofLog)
    ,m_replication(new ReplicationManager)
    ,m_bgsaveRunning(false)
    ,m_bgrewriteRunning(false)
    ,m_nextClientId(1) {
    if(!m_dispatch) {
        m_dispatch.reset(new CommandDispatch);
        registerBuiltinCommands(m_dispatch);
    } else {
        m_dispatch = std::move(dispatch);
    }
    bindBlockingListHub(m_blocking);
    bindAofForConfig(m_aof);
    bindReplicationForConfig(m_replication);
    bindBgsaveForConfig([this]() {
        if(m_bgsaveRunning.exchange(true)) {
            return false;
        }
        KvStore::ptr store = m_store;
        zero::IOManager* worker = m_worker;
        if(!store || !worker) {
            m_bgsaveRunning = false;
            return false;
        }
        worker->schedule([this, store]() {
            store->saveRdb(nullptr);
            m_bgsaveRunning = false;
        });
        return true;
    });
    bindBgRewriteAofForConfig([this]() {
        if(m_bgrewriteRunning.exchange(true)) {
            return false;
        }
        KvStore::ptr store = m_store;
        AofLog::ptr aof = m_aof;
        zero::IOManager* worker = m_worker;
        if(!store || !aof || !worker) {
            m_bgrewriteRunning = false;
            return false;
        }
        worker->schedule([this, store, aof]() {
            std::string err;
            aof->rewrite(store, &err);
            m_bgrewriteRunning = false;
        });
        return true;
    });
    bindClientListForConfig([this]() {
        std::vector<RedisClientSnapshot> out;
        zero::Mutex::Lock lock(m_clientsMutex);
        for(auto it = m_clientSessions.begin(); it != m_clientSessions.end(); ) {
            KvSession::ptr session = it->second.lock();
            if(!session) {
                it = m_clientSessions.erase(it);
                continue;
            }
            RedisClientSnapshot snap;
            snap.id = session->context().client_id;
            snap.name = session->context().client_name;
            snap.addr = session->context().client_addr;
            out.push_back(std::move(snap));
            ++it;
        }
        return out;
    });
    bindShutdownForConfig([this](bool save) {
        if(save && m_store) {
            std::string err;
            m_store->saveRdb(&err);
        }
        stop();
    });
    if(m_replication) {
        m_replication->configureSlaveSync(m_store, m_dispatch, m_worker);
    }
    m_type = "redis";
    setName("zero-redis");
}

void KvServer::setName(const std::string& v) {
    TcpServer::setName(v);
}

void KvServer::setAutoSaveSec(int sec) {
    m_autoSaveSec = sec;
}

int KvServer::getAutoSaveSec() const {
    return m_autoSaveSec;
}

bool KvServer::start() {
    if(m_store) {
        std::string err;
        if(!m_store->loadRdb(&err)) {
            ZERO_LOG_ERROR(g_logger) << "redis load rdb failed: " << err;
        }
    }
    if(m_aof && m_aof->isEnabled() && m_store && m_dispatch) {
        std::string err;
        if(!m_aof->replay(m_store, m_dispatch, &err)) {
            ZERO_LOG_ERROR(g_logger) << "redis aof replay failed: " << err;
        }
    }
    if(!TcpServer::start()) {
        return false;
    }
    if(!m_expireTimer && m_store && m_worker) {
        KvStore::ptr store = m_store;
        m_expireTimer = m_worker->addTimer(kExpireSweepMs, [store]() {
            store->purgeExpiredActive(kExpireScanPerDb);
        }, true);
    }
    if(!m_saveTimer && m_store && m_worker && m_autoSaveSec > 0) {
        KvStore::ptr store = m_store;
        const uint64_t ms = (uint64_t)m_autoSaveSec * 1000;
        m_saveTimer = m_worker->addTimer(ms, [store]() {
            store->saveRdb(nullptr);
        }, true);
    }
    if(m_aof && m_aof->isEnabled() && m_worker
       && m_aof->fsyncPolicy() == AofFsyncPolicy::EverySec) {
        AofLog::ptr aof = m_aof;
        m_aofSyncTimer = m_worker->addTimer(1000, [aof]() {
            aof->syncToDisk();
        }, true);
    }
    return true;
}

void KvServer::stop() {
    std::vector<KvSession::ptr> sessions;
    {
        zero::Mutex::Lock lock(m_clientsMutex);
        for(auto it = m_clientSessions.begin(); it != m_clientSessions.end(); ++it) {
            KvSession::ptr session = it->second.lock();
            if(session) {
                sessions.push_back(session);
            }
        }
    }
    for(const auto& session : sessions) {
        session->close();
    }
    TcpServer::stop();
}

void KvServer::handleClient(Socket::ptr client) {
    ZERO_LOG_DEBUG(g_logger) << "redis handleClient " << *client;
    KvSession::ptr session(new KvSession(client));
    session->context().pubsub = m_pubsub.get();
    session->context().replication = m_replication.get();
    session->context().session = session;
    session->context().client_id = m_nextClientId.fetch_add(1);
    if(client->getRemoteAddress()) {
        session->context().client_addr = client->getRemoteAddress()->toString();
    }
    {
        zero::Mutex::Lock lock(m_clientsMutex);
        m_clientSessions[session->context().client_id] = session;
    }
    if(!isAuthRequired()) {
        session->context().authenticated = true;
    }

    // Pre-check: are AOF or replication propagation actually needed?
    const bool aof_active = m_aof && m_aof->isEnabled();
    const bool repl_active = m_replication && m_replication->role() == ReplicationManager::Role::Master;
    const bool need_mutation_track = aof_active || repl_active;

    while(true) {
        RespValue req;
        if(!session->recvCommand(req)) {
            break;
        }

        bool quit = false;
        do {
            RespValue rsp = m_dispatch->dispatch(session->context(), req, m_store);
            if(rsp.type != RespType::Null) {
                session->appendResponse(rsp);
            }
            // Only compute upperCmd when mutation tracking is actually needed
            if(need_mutation_track && rsp.type != RespType::Error) {
                const std::string name = upperCmd(req);
                if(isMutatingCommand(name)) {
                    if(aof_active) m_aof->append(req);
                    if(repl_active) m_replication->propagateCommand(req);
                }
            }
            if(session->context().quit) {
                quit = true;
                break;
            }
        } while(session->tryRecvCommand(req));

        if(session->flushResponses() <= 0) {
            break;
        }
        if(session->context().replica_connection) {
            while(client && client->isConnected()) {
                zero::Fiber::YieldToHold();
            }
            break;
        }
        if(quit) {
            break;
        }
    }
    if(m_pubsub) {
        m_pubsub->removeSession(session);
    }
    if(m_blocking) {
        m_blocking->cancelWaiters(session);
    }
    if(m_replication) {
        m_replication->removeReplica(session);
    }
    {
        zero::Mutex::Lock lock(m_clientsMutex);
        m_clientSessions.erase(session->context().client_id);
    }
    session->close();
}

} // namespace kv
} // namespace zero

/**
 * @file replication.cc
 * @brief 主从复制实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "replication.h"

#include "zero/core/io/address.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/timer.h"
#include "zero/kv/command_dispatch.h"
#include "zero/kv/persistence/rdb.h"
#include "zero/kv/kv_session.h"
#include "zero/kv/store/kv_store.h"
#include "zero/kv/resp_reader.h"
#include "zero/core/io/socket.h"

#include <random>
#include <sstream>

namespace zero {
namespace kv {

namespace {

ReplicationManager::ptr g_repl_config;

std::string randomReplId() {
    static const char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id;
    id.reserve(40);
    for(int i = 0; i < 40; ++i) {
        id.push_back(kHex[dist(gen)]);
    }
    return id;
}

RespValue makeSimpleLine(const std::string& text) {
    RespValue rsp;
    rsp.type = RespType::SimpleString;
    rsp.str = text;
    return rsp;
}

bool parseFullResyncLine(const std::string& line, std::string& replid, int64_t& offset) {
    const char* prefix = "FULLRESYNC ";
    const size_t plen = 11;
    if(line.size() <= plen || line.compare(0, plen, prefix) != 0) {
        return false;
    }
    std::istringstream ss(line.substr(plen));
    ss >> replid >> offset;
    return !replid.empty();
}

size_t applyReplicationPayload(const std::string& payload, CommandDispatch::ptr dispatch,
                               KvStore::ptr store) {
    if(payload.empty() || !dispatch || !store) {
        return 0;
    }
    KvContext ctx;
    ctx.repl_apply = true;
    ctx.authenticated = true;
    size_t pos = 0;
    while(pos < payload.size()) {
        RespValue req;
        size_t consumed = 0;
        RespReader reader(payload.data() + pos, payload.size() - pos);
        ParseStatus st = reader.tryParse(req, &consumed);
        if(st == ParseStatus::NeedMore) {
            break;
        }
        if(st == ParseStatus::Error || consumed == 0) {
            break;
        }
        pos += consumed;
        dispatch->dispatch(ctx, req, store);
    }
    return pos;
}

size_t drainSessionReplication(KvSession::ptr session, CommandDispatch::ptr dispatch,
                               KvStore::ptr store) {
    if(!session || !dispatch || !store) {
        return 0;
    }
    size_t applied = 0;
    KvContext ctx;
    ctx.repl_apply = true;
    ctx.authenticated = true;
    RespValue cmd;
    while(session->tryRecvCommand(cmd)) {
        dispatch->dispatch(ctx, cmd, store);
        applied += RespEncoder::encode(cmd).size();
    }
    return applied;
}

} // namespace

void bindReplicationForConfig(ReplicationManager::ptr repl) {
    g_repl_config = repl;
}

ReplicationManager::ptr getReplicationForConfig() {
    return g_repl_config;
}

ReplicationManager::ReplicationManager()
    :m_repl_id(randomReplId()) {
}

ReplicationManager::Role ReplicationManager::role() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_role;
}

const std::string& ReplicationManager::replId() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_repl_id;
}

int64_t ReplicationManager::replOffset() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_offset;
}

int ReplicationManager::connectedSlaves() const {
    zero::Mutex::Lock lock(m_mutex);
    int alive = 0;
    for(const auto& weak : m_replicas) {
        if(weak.lock()) {
            ++alive;
        }
    }
    return alive;
}

const std::string& ReplicationManager::masterHost() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_master_host;
}

int ReplicationManager::masterPort() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_master_port;
}

int64_t ReplicationManager::masterLinkStatus() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_master_link_status;
}

const std::string& ReplicationManager::masterReplId() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_master_repl_id;
}

int64_t ReplicationManager::masterReplOffset() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_master_offset;
}

void ReplicationManager::configureSlaveSync(std::shared_ptr<KvStore> store, std::shared_ptr<CommandDispatch> dispatch,
                                            zero::IOManager* io_worker) {
    zero::Mutex::Lock lock(m_mutex);
    m_sync_store = std::move(store);
    m_sync_dispatch = std::move(dispatch);
    m_sync_io = io_worker;
}

void ReplicationManager::setSlaveOf(const std::string& host, int port) {
    std::shared_ptr<KvSession> sync_session;
    {
        zero::Mutex::Lock lock(m_mutex);
        sync_session = m_master_sync.lock();
        if(host.empty()) {
            m_role = Role::Master;
            m_master_host.clear();
            m_master_port = 0;
            m_master_link_status = 0;
            m_master_repl_id.clear();
            m_master_offset = -1;
            m_master_sync.reset();
        }
    }
    if(sync_session) {
        sync_session->close();
    }
    if(host.empty()) {
        return;
    }
    {
        zero::Mutex::Lock lock(m_mutex);
        m_role = Role::Slave;
        m_master_host = host;
        m_master_port = port;
        m_master_link_status = 0;
    }
    scheduleSlaveSync();
}

void ReplicationManager::promoteToMaster() {
    setSlaveOf("", 0);
}

void ReplicationManager::scheduleSlaveSync() {
    zero::IOManager* io = nullptr;
    {
        zero::Mutex::Lock lock(m_mutex);
        if(m_role != Role::Slave || !m_sync_io || !m_sync_store || !m_sync_dispatch) {
            return;
        }
        io = m_sync_io;
    }
    io->schedule([this]() { runSlaveSyncOnce(); });
}

void ReplicationManager::scheduleSlaveReconnect() {
    zero::IOManager* io = nullptr;
    {
        zero::Mutex::Lock lock(m_mutex);
        if(m_role != Role::Slave || !m_sync_io || m_master_host.empty() || m_master_port <= 0) {
            return;
        }
        io = m_sync_io;
    }
    io->addTimer(1000, [this]() { scheduleSlaveSync(); }, false);
}

void ReplicationManager::registerReplica(const std::shared_ptr<KvSession>& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    m_replicas.push_back(session);
}

void ReplicationManager::removeReplica(const std::shared_ptr<KvSession>& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    for(auto it = m_replicas.begin(); it != m_replicas.end();) {
        KvSession::ptr sp = it->lock();
        if(!sp || sp.get() == session.get()) {
            it = m_replicas.erase(it);
        } else {
            ++it;
        }
    }
}

void ReplicationManager::appendBacklog(const std::string& payload) {
    if(payload.empty()) {
        return;
    }
    BacklogChunk chunk;
    chunk.start_offset = m_offset + 1;
    chunk.payload = payload;
    m_backlog.push_back(chunk);
    m_backlog_bytes += payload.size();
    m_offset += (int64_t)payload.size();

    while(m_backlog_bytes > kMaxBacklogBytes && !m_backlog.empty()) {
        m_backlog_bytes -= m_backlog.front().payload.size();
        m_backlog.pop_front();
    }
}

void ReplicationManager::fanoutToReplicas(const std::string& encoded) {
    std::vector<std::shared_ptr<KvSession>> targets;
    {
        zero::Mutex::Lock lock(m_mutex);
        for(auto& weak : m_replicas) {
            KvSession::ptr sp = weak.lock();
            if(sp) {
                targets.push_back(sp);
            }
        }
    }
    for(const auto& session : targets) {
        session->pushReplicationPayload(encoded);
    }
}

void ReplicationManager::propagateCommand(const RespValue& command) {
    // Fast-path: skip expensive RESP encoding if not master or no replicas
    if(m_role != Role::Master) {
        return;
    }
    const std::string encoded = RespEncoder::encode(command);
    zero::Mutex::Lock lock(m_mutex);
    // Double-check role under lock in case of concurrent SLAVEOF
    if(m_role != Role::Master) {
        return;
    }
    appendBacklog(encoded);
    lock.unlock();
    fanoutToReplicas(encoded);
}

bool ReplicationManager::tryCollectPartial(const std::string& replid, int64_t offset,
                                           std::string& out) const {
    if(replid != "?" && replid != m_repl_id) {
        return false;
    }
    if(offset < 0) {
        return false;
    }
    const int64_t want = offset + 1;
    if(m_backlog.empty()) {
        return false;
    }
    const int64_t oldest = m_backlog.front().start_offset;
    if(want < oldest) {
        return false;
    }
    out.clear();
    for(size_t i = 0; i < m_backlog.size(); ++i) {
        const auto& chunk = m_backlog[i];
        const int64_t chunk_end = chunk.start_offset + (int64_t)chunk.payload.size() - 1;
        if(want > chunk_end) {
            continue;
        }
        if(want < chunk.start_offset) {
            return false;
        }
        const size_t pos = (size_t)(want - chunk.start_offset);
        out.append(chunk.payload.data() + pos, chunk.payload.size() - pos);
        for(size_t j = i + 1; j < m_backlog.size(); ++j) {
            out.append(m_backlog[j].payload);
        }
        return true;
    }
    return false;
}

ReplicationManager::PsyncResult ReplicationManager::handlePsync(const std::string& replid,
                                                                int64_t offset,
                                                                std::shared_ptr<KvStore> store) const {
    PsyncResult result;
    zero::Mutex::Lock lock(m_mutex);
    if(m_role != Role::Master) {
        result.kind = PsyncResult::Kind::Error;
        result.line = RespEncoder::err("ERR PSYNC not allowed on replica");
        return result;
    }

    std::string partial;
    if(tryCollectPartial(replid, offset, partial)) {
        result.kind = PsyncResult::Kind::Partial;
        result.line = makeSimpleLine("CONTINUE");
        result.payload = partial;
        return result;
    }

    if(!store) {
        result.kind = PsyncResult::Kind::Error;
        result.line = RespEncoder::err("ERR store unavailable");
        return result;
    }
    lock.unlock();
    if(!Rdb::dump(*store, result.payload, nullptr)) {
        result.kind = PsyncResult::Kind::Error;
        result.line = RespEncoder::err("ERR failed dumping RDB for replication");
        return result;
    }
    lock.lock();
    std::ostringstream ss;
    ss << "FULLRESYNC " << m_repl_id << " " << m_offset;
    result.kind = PsyncResult::Kind::Full;
    result.line = makeSimpleLine(ss.str());
    return result;
}

void ReplicationManager::runSlaveSyncOnce() {
    std::shared_ptr<KvStore> store;
    std::shared_ptr<CommandDispatch> dispatch;
    std::string host;
    int port = 0;
    std::string psync_replid = "?";
    int64_t psync_offset = -1;
    {
        zero::Mutex::Lock lock(m_mutex);
        if(m_role != Role::Slave || m_master_host.empty() || m_master_port <= 0) {
            return;
        }
        store = m_sync_store;
        dispatch = m_sync_dispatch;
        host = m_master_host;
        port = m_master_port;
        m_master_link_status = 0;
        if(!m_master_repl_id.empty() && m_master_offset >= 0) {
            psync_replid = m_master_repl_id;
            psync_offset = m_master_offset;
        }
    }
    if(!store || !dispatch) {
        scheduleSlaveReconnect();
        return;
    }

    Address::ptr addr = Address::LookupAny(host + ":" + std::to_string(port));
    if(!addr) {
        scheduleSlaveReconnect();
        return;
    }
    Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock || !sock->connect(addr)) {
        scheduleSlaveReconnect();
        return;
    }

    KvSession::ptr session(new KvSession(sock));
    {
        zero::Mutex::Lock lock(m_mutex);
        m_master_sync = session;
    }
    RespValue psync_req;
    psync_req.type = RespType::Array;
    psync_req.array.push_back(RespEncoder::bulk("PSYNC"));
    psync_req.array.push_back(RespEncoder::bulk(psync_replid));
    psync_req.array.push_back(RespEncoder::bulk(std::to_string(psync_offset)));
    session->appendResponse(psync_req);
    if(session->flushResponses() <= 0) {
        scheduleSlaveReconnect();
        return;
    }

    RespValue line_rsp;
    if(!session->recvCommand(line_rsp)) {
        scheduleSlaveReconnect();
        return;
    }
    if(line_rsp.type == RespType::Error) {
        scheduleSlaveReconnect();
        return;
    }
    if(line_rsp.type != RespType::SimpleString) {
        scheduleSlaveReconnect();
        return;
    }

    if(line_rsp.str.find("FULLRESYNC") == 0) {
        std::string master_replid;
        int64_t master_offset = 0;
        if(!parseFullResyncLine(line_rsp.str, master_replid, master_offset)) {
            scheduleSlaveReconnect();
            return;
        }
        RespValue bulk_rsp;
        if(!session->recvCommand(bulk_rsp)) {
            scheduleSlaveReconnect();
            return;
        }
        if(bulk_rsp.type != RespType::BulkString || bulk_rsp.is_null) {
            scheduleSlaveReconnect();
            return;
        }
        if(!Rdb::loadFromString(*store, bulk_rsp.str, nullptr)) {
            scheduleSlaveReconnect();
            return;
        }
        {
            zero::Mutex::Lock lock(m_mutex);
            m_master_repl_id = master_replid;
            m_master_offset = master_offset;
            m_master_link_status = 1;
        }
    } else if(line_rsp.str == "CONTINUE") {
        size_t applied = drainSessionReplication(session, dispatch, store);
        {
            zero::Mutex::Lock lock(m_mutex);
            if(m_master_offset >= 0) {
                m_master_offset += (int64_t)applied;
            }
            m_master_link_status = 1;
        }
    } else {
        scheduleSlaveReconnect();
        return;
    }

    while(true) {
        RespValue cmd;
        if(!session->recvCommand(cmd)) {
            break;
        }
        KvContext ctx;
        ctx.repl_apply = true;
        ctx.authenticated = true;
        dispatch->dispatch(ctx, cmd, store);
        const size_t nbytes = RespEncoder::encode(cmd).size();
        zero::Mutex::Lock lock(m_mutex);
        if(m_master_offset >= 0) {
            m_master_offset += (int64_t)nbytes;
        }
    }

    {
        zero::Mutex::Lock lock(m_mutex);
        m_master_link_status = 0;
    }
    scheduleSlaveReconnect();
}

} // namespace kv
} // namespace zero

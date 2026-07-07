/**
 * @file cluster_bus.cc
 * @brief Redis Cluster 总线通信实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cluster_bus.h"
#include "cluster_manager.h"
#include "cluster_slot.h"
#include "zero/kv/pubsub/pubsub_hub.h"
#include "zero/kv/resp_reader.h"
#include "zero/kv/store/kv_store.h"
#include "zero/core/io/address.h"
#include "zero/core/log/log.h"
#include "zero/core/concurrency/timer.h"
#include "zero/util/hash_util.h"

#include <algorithm>

namespace zero {
namespace kv {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static ClusterBus* g_cluster_bus = nullptr;

void bindClusterBus(ClusterBus* bus) {
    g_cluster_bus = bus;
}

ClusterBus* getClusterBus() {
    return g_cluster_bus;
}

// ===== ClusterMessage =====

RespValue ClusterMessage::toResp() const {
    RespValue arr;
    arr.type = RespType::Array;
    arr.array.push_back(RespEncoder::integer(static_cast<int>(type)));
    arr.array.push_back(RespEncoder::bulk(sender_id));
    arr.array.push_back(RespEncoder::bulk(target_id));
    arr.array.push_back(RespEncoder::bulk(node_id));
    arr.array.push_back(RespEncoder::bulk(ip));
    arr.array.push_back(RespEncoder::integer(port));
    arr.array.push_back(RespEncoder::integer(cluster_port));
    arr.array.push_back(RespEncoder::integer(flags));
    arr.array.push_back(RespEncoder::integer(config_epoch));
    arr.array.push_back(RespEncoder::bulk(master_id));
    arr.array.push_back(RespEncoder::bulk(slots_base64));
    arr.array.push_back(RespEncoder::bulk(data1));
    arr.array.push_back(RespEncoder::bulk(data2));
    return arr;
}

static std::string getRespString(const RespValue& v) {
    if(v.type == RespType::Integer) {
        return std::to_string(v.integer);
    }
    if(v.is_null) {
        return std::string();
    }
    return v.str;
}

bool ClusterMessage::fromResp(const RespValue& v, ClusterMessage& out) {
    if(v.type != RespType::Array || v.array.size() < 13) return false;
    for(size_t i = 0; i < 13; ++i) {
        if(v.array[i].is_null) return false;
    }
    try {
        out.type = static_cast<ClusterMsgType>(std::stoi(getRespString(v.array[0])));
        out.sender_id = getRespString(v.array[1]);
        out.target_id = getRespString(v.array[2]);
        out.node_id = getRespString(v.array[3]);
        out.ip = getRespString(v.array[4]);
        out.port = static_cast<int>(std::stoll(getRespString(v.array[5])));
        out.cluster_port = static_cast<int>(std::stoll(getRespString(v.array[6])));
        out.flags = static_cast<uint16_t>(std::stoll(getRespString(v.array[7])));
        out.config_epoch = static_cast<uint64_t>(std::stoll(getRespString(v.array[8])));
        out.master_id = getRespString(v.array[9]);
        out.slots_base64 = getRespString(v.array[10]);
        out.data1 = getRespString(v.array[11]);
        out.data2 = getRespString(v.array[12]);
    } catch(...) {
        return false;
    }
    return true;
}

// ===== ClusterBus =====

static std::string slotsToBase64(const std::bitset<kClusterSlotCount>& slots) {
    std::string raw(16384 / 8, '\0');
    for(int i = 0; i < kClusterSlotCount; ++i) {
        if(slots.test(i)) {
            raw[i / 8] |= (1 << (i % 8));
        }
    }
    return zero::base64encode(raw);
}

static bool base64ToSlots(const std::string& b64, std::bitset<kClusterSlotCount>& slots) {
    std::string raw = zero::base64decode(b64);
    if(raw.size() != 16384 / 8) return false;
    slots.reset();
    for(int i = 0; i < kClusterSlotCount; ++i) {
        if(raw[i / 8] & (1 << (i % 8))) {
            slots.set(i);
        }
    }
    return true;
}

ClusterBus::ClusterBus(ClusterManager* mgr, PubSubHub* pubsub,
                       zero::IOManager* worker,
                       zero::IOManager* io_worker,
                       zero::IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
    , m_mgr(mgr)
    , m_pubsub(pubsub) {
    setName("zero-cluster-bus");
}

ClusterBus::~ClusterBus() {
    stopBus();
}

bool ClusterBus::startBus(const std::string& bind_ip, int cluster_port) {
    if(!m_mgr) return false;
    std::string addr_str = bind_ip + ":" + std::to_string(cluster_port);
    Address::ptr addr = Address::LookupAny(addr_str);
    if(!addr) {
        ZERO_LOG_ERROR(g_logger) << "Cluster bus invalid address: " << addr_str;
        return false;
    }
    if(!bind(addr)) {
        ZERO_LOG_ERROR(g_logger) << "Cluster bus bind failed: " << addr_str;
        return false;
    }
    if(!start()) {
        ZERO_LOG_ERROR(g_logger) << "Cluster bus start failed: " << addr_str;
        return false;
    }
    m_running = true;
    ZERO_LOG_INFO(g_logger) << "Cluster bus listening on " << addr_str;
    return true;
}

void ClusterBus::stopBus() {
    m_running = false;
    stopGossip();
    {
        zero::Mutex::Lock lock(m_linksMutex);
        for(auto& link : m_links) {
            if(link.sock) link.sock->close();
        }
        m_links.clear();
    }
    TcpServer::stop();
}

void ClusterBus::handleClient(Socket::ptr client) {
    if(!client) return;
    {
        zero::Mutex::Lock lock(m_linksMutex);
        Link link;
        link.sock = client;
        m_links.push_back(link);
    }
    runConnection(client);
}

void ClusterBus::runConnection(Socket::ptr client) {
    if(!client) return;
    std::string read_buf;
    while(m_running && client->isConnected()) {
        char buf[4096];
        int rt = client->recv(buf, sizeof(buf), 0);
        if(rt <= 0) {
            break;
        }
        read_buf.append(buf, rt);
        while(!read_buf.empty()) {
            RespValue v;
            size_t consumed = 0;
            RespReader reader(read_buf.data(), read_buf.size());
            ParseStatus st = reader.tryParse(v, &consumed);
            if(st == ParseStatus::NeedMore) {
                break;
            }
            if(st == ParseStatus::Error || consumed == 0) {
                read_buf.clear();
                break;
            }
            read_buf.erase(0, consumed);
            ClusterMessage msg;
            if(ClusterMessage::fromResp(v, msg)) {
                onMessage(msg, client);
            }
        }
    }
    {
        zero::Mutex::Lock lock(m_linksMutex);
        m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
            [&client](const Link& l) { return l.sock.get() == client.get(); }), m_links.end());
    }
    client->close();
}

void ClusterBus::onMessage(const ClusterMessage& msg, Socket::ptr from) {
    if(!m_mgr) return;
    if(msg.sender_id == m_mgr->myNodeId()) return;  // ignore own messages

    switch(msg.type) {
        case ClusterMsgType::Meet:
            handleMeet(msg);
            break;
        case ClusterMsgType::Ping:
        case ClusterMsgType::Pong:
            handlePingPong(msg);
            break;
        case ClusterMsgType::Fail:
            handleFail(msg);
            break;
        case ClusterMsgType::Update:
            handleUpdate(msg);
            break;
        case ClusterMsgType::Publish:
            handlePublish(msg);
            break;
    }
}

void ClusterBus::buildNodeMessage(ClusterMessage& msg, ClusterMsgType type) const {
    if(!m_mgr) return;
    msg.type = type;
    msg.sender_id = m_mgr->myNodeId();
    msg.target_id.clear();
    auto myself = m_mgr->getMyself();
    if(myself) {
        msg.node_id = myself->node_id;
        msg.ip = myself->ip;
        msg.port = myself->port;
        msg.cluster_port = myself->cluster_port;
        msg.flags = myself->flags;
        msg.config_epoch = m_mgr->getConfigEpoch();
        msg.master_id = myself->master_id;
        msg.slots_base64 = slotsToBase64(myself->slots);
    }
}

void ClusterBus::handleMeet(const ClusterMessage& msg) {
    if(!m_mgr) return;
    // 添加/更新发送者
    auto node = std::make_shared<ClusterNode>();
    node->node_id = msg.sender_id;
    node->ip = msg.ip;
    node->port = msg.port;
    node->cluster_port = msg.cluster_port;
    node->flags = msg.flags;
    node->clearFlag(ClusterNodeFlag::Myself); // 远程节点不应带有 myself 标志
    node->master_id = msg.master_id;
    base64ToSlots(msg.slots_base64, node->slots);
    node->pong_received = KvStore::nowMs();
    m_mgr->addNode(node);

    // 回复 PONG
    ClusterMessage pong;
    buildNodeMessage(pong, ClusterMsgType::Pong);
    sendToAddr(msg.ip, msg.cluster_port, pong);

    ZERO_LOG_INFO(g_logger) << "Cluster MEET from " << msg.sender_id << " at "
                             << msg.ip << ":" << msg.port;
}

void ClusterBus::handlePingPong(const ClusterMessage& msg) {
    if(!m_mgr) return;
    // 更新或添加节点
    auto node = m_mgr->getNode(msg.sender_id);
    if(!node) {
        node = std::make_shared<ClusterNode>();
        node->node_id = msg.sender_id;
    }
    node->ip = msg.ip;
    node->port = msg.port;
    node->cluster_port = msg.cluster_port;
    node->flags = msg.flags;
    node->clearFlag(ClusterNodeFlag::Myself); // 远程节点不应带有 myself 标志
    node->master_id = msg.master_id;
    node->config_epoch = msg.config_epoch;
    std::bitset<kClusterSlotCount> recv_slots;
    base64ToSlots(msg.slots_base64, recv_slots);
    std::vector<int> slots;
    for(int i = 0; i < kClusterSlotCount; ++i) {
        if(recv_slots.test(i)) slots.push_back(i);
    }
    node->slots = recv_slots; // 临时保存，addNode 会合并
    node->pong_received = KvStore::nowMs();
    m_mgr->addNode(node);
    // 使用 epoch 仲裁合并槽位，避免老 epoch 覆盖新 epoch
    m_mgr->updateNodeSlots(msg.sender_id, slots, msg.config_epoch);
    m_mgr->clearNodeFail(msg.sender_id);

    // 处理 gossip 来的 failure report
    if(!msg.data1.empty()) {
        m_mgr->markNodePFail(msg.data1);
        m_mgr->addFailureReport(msg.data1, msg.sender_id);
    }
    if(msg.flags & static_cast<uint16_t>(ClusterNodeFlag::PFail)) {
        m_mgr->markNodePFail(msg.sender_id);
        m_mgr->addFailureReport(msg.sender_id, msg.sender_id);
    }
    if(msg.flags & static_cast<uint16_t>(ClusterNodeFlag::Fail)) {
        m_mgr->markNodeFail(msg.sender_id);
    }

    // 如果是 PING，回复 PONG
    if(msg.type == ClusterMsgType::Ping) {
        ClusterMessage pong;
        buildNodeMessage(pong, ClusterMsgType::Pong);
        sendToAddr(msg.ip, msg.cluster_port, pong);
    }
}

void ClusterBus::handleFail(const ClusterMessage& msg) {
    if(!m_mgr) return;
    if(!msg.data1.empty()) {
        m_mgr->markNodeFail(msg.data1);
        ZERO_LOG_INFO(g_logger) << "Cluster FAIL received for node " << msg.data1
                                 << " from " << msg.sender_id;
    }
}

void ClusterBus::handleUpdate(const ClusterMessage& msg) {
    if(!m_mgr) return;
    // UPDATE 用于显式传播某个节点的信息（通常是槽位变更）
    handlePingPong(msg);
}

void ClusterBus::handlePublish(const ClusterMessage& msg) {
    if(!m_pubsub) return;
    if(!msg.data1.empty()) {
        // data2 是已 RESP 编码的 message payload，直接交给本地 pubsub
        m_pubsub->publish(msg.data1, msg.data2);
    }
}

bool ClusterBus::sendToAddr(const std::string& ip, int cluster_port, const ClusterMessage& msg) {
    std::string addr_str = ip + ":" + std::to_string(cluster_port);
    Address::ptr addr = Address::LookupAny(addr_str);
    if(!addr) return false;
    Socket::ptr sock = Socket::CreateTCP(addr);
    if(!sock) return false;
    if(!sock->connect(addr)) return false;
    std::string payload = RespEncoder::encode(msg.toResp());
    int sent = sock->send(payload.data(), payload.size(), 0);
    sock->close();
    return sent == (int)payload.size();
}

bool ClusterBus::sendToNode(const std::string& node_id, const ClusterMessage& msg) {
    if(!m_mgr) return false;
    auto node = m_mgr->getNode(node_id);
    if(!node) return false;
    return sendToAddr(node->ip, node->cluster_port, msg);
}

void ClusterBus::broadcast(const ClusterMessage& msg, const std::string& exclude_id) {
    if(!m_mgr) return;
    auto nodes = m_mgr->getAllNodes();
    for(const auto& node : nodes) {
        if(node->hasFlag(ClusterNodeFlag::Myself)) continue;
        if(!exclude_id.empty() && node->node_id == exclude_id) continue;
        sendToAddr(node->ip, node->cluster_port, msg);
    }
}

bool ClusterBus::meetNode(const std::string& ip, int port, int cluster_port) {
    if(!m_mgr) return false;
    ClusterMessage msg;
    buildNodeMessage(msg, ClusterMsgType::Meet);
    // MEET 消息包含发送方自身地址（由 buildNodeMessage 填充），
    // 目标地址通过 sendToAddr 参数指定，不覆盖消息中的 ip/port。
    bool ok = sendToAddr(ip, cluster_port, msg);
    if(ok) {
        ZERO_LOG_INFO(g_logger) << "Sent CLUSTER MEET to " << ip << ":" << port
                                 << " (bus " << cluster_port << ")";
    }
    return ok;
}

static std::string pickGossipFailureReport(ClusterManager* mgr, const std::string& exclude_id) {
    if(!mgr) return std::string();
    auto nodes = mgr->getAllNodes();
    for(const auto& n : nodes) {
        if(n->node_id == exclude_id) continue;
        if(n->hasFlag(ClusterNodeFlag::PFail) && !n->hasFlag(ClusterNodeFlag::Fail)) {
            return n->node_id;
        }
    }
    // 也可以 gossip 已经 fail 的节点，帮助新节点收敛
    for(const auto& n : nodes) {
        if(n->node_id == exclude_id) continue;
        if(n->hasFlag(ClusterNodeFlag::Fail)) {
            return n->node_id;
        }
    }
    return std::string();
}

void ClusterBus::sendPingPongTo(const std::string& node_id, ClusterMsgType type) {
    if(!m_mgr) return;
    auto node = m_mgr->getNode(node_id);
    if(!node || node->hasFlag(ClusterNodeFlag::Myself)) return;
    ClusterMessage msg;
    buildNodeMessage(msg, type);
    // gossip 一个本节点已标记为 pfail/fail 的节点
    msg.data1 = pickGossipFailureReport(m_mgr, node_id);
    sendToAddr(node->ip, node->cluster_port, msg);
}

void ClusterBus::sendPingToAll() {
    if(!m_mgr) return;
    auto nodes = m_mgr->getAllNodes();
    for(const auto& node : nodes) {
        if(node->hasFlag(ClusterNodeFlag::Myself)) continue;
        sendPingPongTo(node->node_id, ClusterMsgType::Ping);
    }
}

void ClusterBus::broadcastSlotUpdate(int slot, const std::string& node_id) {
    if(!m_mgr) return;
    auto node = m_mgr->getNode(node_id);
    if(!node) return;
    ClusterMessage msg;
    msg.type = ClusterMsgType::Update;
    msg.sender_id = m_mgr->myNodeId();
    msg.node_id = node_id;
    msg.ip = node->ip;
    msg.port = node->port;
    msg.cluster_port = node->cluster_port;
    msg.flags = node->flags;
    msg.master_id = node->master_id;
    msg.config_epoch = m_mgr->getConfigEpoch();
    msg.slots_base64 = slotsToBase64(node->slots);
    broadcast(msg);
}

void ClusterBus::startGossip(uint64_t interval_ms) {
    if(!m_mgr || !m_worker) return;
    stopGossip();
    ClusterBus* self = this;
    m_gossipTimer = m_worker->addTimer(interval_ms, [self]() {
        self->sendPingToAll();
    }, true);
}

void ClusterBus::stopGossip() {
    if(m_gossipTimer) {
        m_gossipTimer->cancel();
        m_gossipTimer.reset();
    }
}

} // namespace kv
} // namespace zero

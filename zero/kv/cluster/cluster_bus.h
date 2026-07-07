/**
 * @file cluster_bus.h
 * @brief Redis Cluster 总线通信（gossip 协议）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_CLUSTER_CLUSTER_BUS_H__
#define __ZERO_KV_CLUSTER_CLUSTER_BUS_H__

#include "zero/kv/resp.h"
#include "zero/core/concurrency/mutex.h"
#include "zero/core/io/socket.h"
#include "zero/net/tcp/tcp_server.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace zero {
namespace kv {

class ClusterManager;
class PubSubHub;

enum class ClusterMsgType : int {
    Meet = 1,
    Ping = 2,
    Pong = 3,
    Fail = 4,
    Update = 5,
    Publish = 6,
};

struct ClusterMessage {
    ClusterMsgType type = ClusterMsgType::Ping;
    std::string sender_id;
    std::string target_id;        // 目标节点 ID，空表示广播
    std::string node_id;          // 消息中描述的节点 ID
    std::string ip;
    int port = 0;
    int cluster_port = 0;
    uint16_t flags = 0;           // ClusterNodeFlag
    uint64_t config_epoch = 0;
    std::string master_id;        // 空表示主节点
    std::string slots_base64;     // 16384 位 bitmap 的 base64
    std::string data1;            // 额外数据：FAIL=失败节点ID, PUBLISH=channel
    std::string data2;            // 额外数据：PUBLISH=message

    RespValue toResp() const;
    static bool fromResp(const RespValue& v, ClusterMessage& out);
};

class ClusterBus : public TcpServer {
public:
    typedef std::shared_ptr<ClusterBus> ptr;

    ClusterBus(ClusterManager* mgr, PubSubHub* pubsub,
               zero::IOManager* worker = zero::IOManager::GetThis(),
               zero::IOManager* io_worker = zero::IOManager::GetThis(),
               zero::IOManager* accept_worker = zero::IOManager::GetThis());
    ~ClusterBus() override;

    // 绑定 cluster_port 并启动
    bool startBus(const std::string& bind_ip, int cluster_port);
    void stopBus();

    // 向指定节点发送消息
    bool sendToNode(const std::string& node_id, const ClusterMessage& msg);
    bool sendToAddr(const std::string& ip, int cluster_port, const ClusterMessage& msg);

    // 广播给所有已知节点（可选排除自己）
    void broadcast(const ClusterMessage& msg, const std::string& exclude_id = "");

    // 发送 MEET 给新节点（实际握手）
    bool meetNode(const std::string& ip, int port, int cluster_port);

    // 启动 gossip 定时器（PING/PONG）
    void startGossip(uint64_t interval_ms = 1000);
    void stopGossip();

    // 手动触发一次 PING 发送（用于测试）
    void sendPingToAll();

    // 广播槽位变更
    void broadcastSlotUpdate(int slot, const std::string& node_id);

protected:
    void handleClient(Socket::ptr client) override;

private:
    void runConnection(Socket::ptr client);
    void onMessage(const ClusterMessage& msg, Socket::ptr from);
    void handleMeet(const ClusterMessage& msg);
    void handlePingPong(const ClusterMessage& msg);
    void handleFail(const ClusterMessage& msg);
    void handleUpdate(const ClusterMessage& msg);
    void handlePublish(const ClusterMessage& msg);

    void buildNodeMessage(ClusterMessage& msg, ClusterMsgType type) const;
    void sendPingPongTo(const std::string& node_id, ClusterMsgType type);

    ClusterManager* m_mgr = nullptr;
    PubSubHub* m_pubsub = nullptr;
    zero::Timer::ptr m_gossipTimer;

    // 记录已知 cluster bus 连接的对端信息（用于去重和定向发送）
    mutable zero::Mutex m_linksMutex;
    struct Link {
        Socket::ptr sock;
        std::string node_id;
    };
    std::vector<Link> m_links;

    std::atomic<bool> m_running{false};
};

// 全局访问（类似 bindClusterManager / getClusterManager）
void bindClusterBus(ClusterBus* bus);
ClusterBus* getClusterBus();

} // namespace kv
} // namespace zero

#endif

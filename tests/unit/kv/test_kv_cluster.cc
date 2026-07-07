/**
 * @file test_kv_cluster.cc
 * @brief Cluster 模式基础测试
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/cluster/cluster_slot.h"
#include "zero/kv/cluster/cluster_manager.h"
#include "zero/kv/cluster/cluster_bus.h"
#include "zero/kv/command_dispatch.h"
#include "zero/kv/store/kv_store.h"
#include "zero/kv/resp.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <string>

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

// ===== CRC16 / keyToSlot 测试 =====
TEST(KvCluster, Crc16) {
    // Redis 标准测试向量
    EXPECT_EQ(keyToSlot("foo"), 12182);
    EXPECT_EQ(keyToSlot("bar"), 5061);
    EXPECT_EQ(keyToSlot("baz"), 4813);

    // hash tag 测试
    EXPECT_EQ(keyToSlot("foo{bar}"), keyToSlot("bar"));
    EXPECT_EQ(keyToSlot("{user:1000}.orders"), keyToSlot("user:1000"));

    // 空 tag 回退到整个 key
    EXPECT_EQ(keyToSlot("foo{}"), (crc16("foo{}") & (kClusterSlotCount - 1)));
}

// ===== ClusterManager 测试 =====
TEST(KvCluster, ClusterManager) {
    ClusterManager mgr;
    mgr.init("", "127.0.0.1", 6379, 16379);

    EXPECT_TRUE(mgr.enabled());
    EXPECT_FALSE(mgr.myNodeId().empty());
    EXPECT_EQ(mgr.myNodeId().length(), 40u);

    // 初始���态：无槽位
    EXPECT_FALSE(mgr.isMySlot(0));
    EXPECT_FALSE(mgr.isMySlot(100));

    // ADDSLOTS
    std::vector<int> slots = {0, 1, 2, 100, 101, 102};
    mgr.addSlots(slots);
    EXPECT_TRUE(mgr.isMySlot(0));
    EXPECT_TRUE(mgr.isMySlot(2));
    EXPECT_TRUE(mgr.isMySlot(100));
    EXPECT_FALSE(mgr.isMySlot(50));

    // DELSLOTS
    mgr.delSlots({100, 101, 102});
    EXPECT_FALSE(mgr.isMySlot(100));
    EXPECT_TRUE(mgr.isMySlot(0));

    // CLUSTER MEET
    EXPECT_TRUE(mgr.meetNode("127.0.0.1", 6380));
    auto nodes = mgr.getAllNodes();
    EXPECT_EQ(nodes.size(), 2u);  // myself + new node

    // CLUSTER NODES 输出
    std::string nodes_str = mgr.buildNodesString();
    EXPECT_FALSE(nodes_str.empty());
    EXPECT_NE(nodes_str.find("myself"), std::string::npos);

    // CLUSTER SLOTS 输出
    RespValue slots_arr = mgr.buildSlotsArray();
    EXPECT_EQ(slots_arr.type, RespType::Array);
    EXPECT_EQ(slots_arr.array.size(), 1u);  // 一个连续范围 [0-2]

    // CLUSTER INFO 输出
    RespValue info = mgr.buildInfo();
    EXPECT_EQ(info.type, RespType::Array);

    // CLUSTER KEYSLOT
    EXPECT_EQ(mgr.keyToSlot("foo"), keyToSlot("foo"));

    // REPLICATE
    mgr.setReplication("abc123");
    EXPECT_TRUE(mgr.isSlave());
    EXPECT_EQ(mgr.getMasterId(), "abc123");

    // 副本不应有槽位
    EXPECT_FALSE(mgr.isMySlot(0));
}

// ===== MOVED / ASK 重定向测试 =====
TEST(KvCluster, MovedAsk) {
    ClusterManager mgr;
    mgr.init("", "127.0.0.1", 6379, 16379);

    // 分配槽位 0-5460
    std::vector<int> slots0;
    for(int i = 0; i <= 5460; ++i) slots0.push_back(i);
    mgr.addSlots(slots0);

    // 添加另一个节点负责 5461-10922
    auto node2 = std::make_shared<ClusterNode>();
    node2->node_id = "node2abc";
    node2->ip = "127.0.0.1";
    node2->port = 6380;
    node2->cluster_port = 16380;
    node2->setFlag(ClusterNodeFlag::Master);
    for(int i = 5461; i <= 10922; ++i) node2->slots.set(i);
    mgr.addNode(node2);

    // 测试 key "foo" -> slot 12182，不在本节点
    int slot = keyToSlot("foo");
    EXPECT_EQ(slot, 12182);
    EXPECT_FALSE(mgr.isMySlot(slot));

    std::string owner = mgr.getSlotOwner(slot);
    // slot 12182 未被分配，应返回空
    EXPECT_TRUE(owner.empty());

    // 分配 slot 12182 给 node2
    node2->slots.set(12182);
    mgr.addNode(node2);  // 更新
    owner = mgr.getSlotOwner(12182);
    EXPECT_EQ(owner, "127.0.0.1:6380");

    // 测试 MOVED 响应格式
    std::string moved = "MOVED " + std::to_string(slot) + " " + owner;
    EXPECT_NE(moved.find("MOVED 12182 127.0.0.1:6380"), std::string::npos);

    // 测试 ASK 重定向（导入状态）：使用属于 node2 的槽位 6000
    mgr.setSlotImporting(6000, "node2abc");
    EXPECT_TRUE(mgr.isImportingSlot(6000));
    std::string src = mgr.getImportingSource(6000);
    EXPECT_EQ(src, "127.0.0.1:6380");

    std::string ask = "ASK " + std::to_string(6000) + " " + src;
    EXPECT_EQ(ask, "ASK 6000 127.0.0.1:6380");
}

// ===== 迁移状态测试 =====
TEST(KvCluster, Migration) {
    ClusterManager mgr;
    mgr.init("", "127.0.0.1", 6379, 16379);

    std::vector<int> slots;
    for(int i = 0; i < 100; ++i) slots.push_back(i);
    mgr.addSlots(slots);

    auto node2 = std::make_shared<ClusterNode>();
    node2->node_id = "target";
    node2->ip = "127.0.0.1";
    node2->port = 6380;
    node2->setFlag(ClusterNodeFlag::Master);
    mgr.addNode(node2);

    // 设置迁移状态
    EXPECT_TRUE(mgr.setSlotMigrating(50, "target"));
    EXPECT_TRUE(mgr.isMigratingSlot(50));

    // 完成迁移
    EXPECT_TRUE(mgr.finishMigration(50));
    EXPECT_FALSE(mgr.isMigratingSlot(50));
    EXPECT_FALSE(mgr.isMySlot(50));  // 本节点不再负责

    // 验证目标节点获得槽位
    auto target = mgr.getNode("target");
    EXPECT_TRUE(target->slots.test(50));
}

// ===== 故障检测与自动故障转移测试 =====
TEST(KvCluster, FailureDetectionAndFailover) {
    ClusterManager mgr;
    mgr.init("replica_node", "127.0.0.1", 6381, 16381);

    // 添加主节点
    auto master = std::make_shared<ClusterNode>();
    master->node_id = "master_node";
    master->ip = "127.0.0.1";
    master->port = 6379;
    master->cluster_port = 16379;
    master->setFlag(ClusterNodeFlag::Master);
    for(int i = 0; i < 100; ++i) master->slots.set(i);
    mgr.addNode(master);

    // 本节点作为副本
    mgr.setReplication("master_node");
    EXPECT_TRUE(mgr.isSlave());
    EXPECT_EQ(mgr.getMasterId(), "master_node");
    EXPECT_EQ(mgr.getMasterAddr(), "127.0.0.1:6379");

    // 模拟收到 PONG
    mgr.updateNodePongReceived("master_node", 1000);
    EXPECT_FALSE(mgr.isNodePFail("master_node"));

    // 模拟超时未收到 PONG
    mgr.checkFailureDetection(1000 + 20000, 15000);
    EXPECT_TRUE(mgr.isNodePFail("master_node"));

    // 更长时间超时，标记为 Fail
    mgr.checkFailureDetection(1000 + 50000, 15000);
    EXPECT_TRUE(mgr.isNodeFailed("master_node"));

    // 副本自动故障转移
    EXPECT_TRUE(mgr.failoverReplica("replica_node"));
    EXPECT_FALSE(mgr.isSlave());
    EXPECT_TRUE(mgr.getMasterId().empty());
    EXPECT_EQ(mgr.getConfigEpoch(), 1u);
}

// ===== CROSSSLOT 检查测试 =====
TEST(KvCluster, Crossslot) {
    ClusterManager::ptr cluster(new ClusterManager);
    cluster->init("node1", "127.0.0.1", 6379, 16379);
    // 分配所有槽位给本节点，避免 MOVED 干扰，专注 CROSSSLOT 检查
    std::vector<int> all_slots;
    for(int i = 0; i < kClusterSlotCount; ++i) all_slots.push_back(i);
    cluster->addSlots(all_slots);
    bindClusterManager(cluster);

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    registerClusterCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    // 同一槽位的 key（使用 hash tag 强制相同）应该成功
    RespValue ok_mset = dispatch->dispatch(ctx, arrayCmd({"MSET", "a{same}", "1", "b{same}", "2"}), store);
    EXPECT_NE(ok_mset.type, RespType::Error);

    // 不同槽位的 key 应该返回 CROSSSLOT
    RespValue err_mget = dispatch->dispatch(ctx, arrayCmd({"MGET", "foo", "bar"}), store);
    EXPECT_EQ(err_mget.type, RespType::Error);
    EXPECT_NE(err_mget.str.find("CROSSSLOT"), std::string::npos);

    RespValue err_del = dispatch->dispatch(ctx, arrayCmd({"DEL", "key1", "key2"}), store);
    EXPECT_EQ(err_del.type, RespType::Error);
    EXPECT_NE(err_del.str.find("CROSSSLOT"), std::string::npos);

    // EVAL 多 key 跨槽
    RespValue err_eval = dispatch->dispatch(ctx, arrayCmd({"EVAL", "return 1", "2", "foo", "bar"}), store);
    EXPECT_EQ(err_eval.type, RespType::Error);
    EXPECT_NE(err_eval.str.find("CROSSSLOT"), std::string::npos);
}

// ===== READONLY 读路由测试 =====
TEST(KvCluster, ReadonlyRouting) {
    ClusterManager::ptr cluster(new ClusterManager);
    cluster->init("replica", "127.0.0.1", 6381, 16381);

    // 添��主节点并分配所有槽位
    auto master = std::make_shared<ClusterNode>();
    master->node_id = "master";
    master->ip = "127.0.0.1";
    master->port = 6379;
    master->cluster_port = 16379;
    master->setFlag(ClusterNodeFlag::Master);
    for(int i = 0; i < kClusterSlotCount; ++i) master->slots.set(i);
    cluster->addNode(master);

    // 本节点作为副本
    cluster->setReplication("master");
    bindClusterManager(cluster);

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    registerClusterCommands(dispatch);
    KvStore::ptr store(new KvStore);

    // 直接写入数据到 store（模拟从主节点同步过来的数据）
    store->set(0, "mykey", "value");

    KvContext ctx;
    // 未开启 READONLY 时，副本应该返回 MOVED
    RespValue moved = dispatch->dispatch(ctx, arrayCmd({"GET", "mykey"}), store);
    EXPECT_EQ(moved.type, RespType::Error);
    EXPECT_NE(moved.str.find("MOVED"), std::string::npos);

    // 开启 READONLY
    RespValue ro = dispatch->dispatch(ctx, arrayCmd({"READONLY"}), store);
    EXPECT_EQ(ro.type, RespType::SimpleString);
    EXPECT_EQ(ro.str, "OK");

    // 读命令应该成功
    RespValue get_rsp = dispatch->dispatch(ctx, arrayCmd({"GET", "mykey"}), store);
    EXPECT_EQ(get_rsp.type, RespType::BulkString);
    EXPECT_EQ(get_rsp.str, "value");

    // 写命令仍然应该返回 MOVED
    RespValue set_rsp = dispatch->dispatch(ctx, arrayCmd({"SET", "mykey", "newvalue"}), store);
    EXPECT_EQ(set_rsp.type, RespType::Error);
    EXPECT_NE(set_rsp.str.find("MOVED"), std::string::npos);
}

// ===== Cluster Bus 消息编码测试 =====
TEST(KvCluster, ClusterBusMessage) {
    ClusterMessage msg;
    msg.type = ClusterMsgType::Ping;
    msg.sender_id = "sender_id_123";
    msg.target_id = "target_id_456";
    msg.node_id = "node_id_789";
    msg.ip = "127.0.0.1";
    msg.port = 6379;
    msg.cluster_port = 16379;
    msg.flags = static_cast<uint16_t>(ClusterNodeFlag::Master);
    msg.config_epoch = 42;
    msg.master_id = "";
    msg.slots_base64 = "abc123";
    msg.data1 = "fail_node";
    msg.data2 = "payload";

    RespValue encoded = msg.toResp();
    ClusterMessage decoded;
    EXPECT_TRUE(ClusterMessage::fromResp(encoded, decoded));
    EXPECT_EQ(decoded.type, msg.type);
    EXPECT_EQ(decoded.sender_id, msg.sender_id);
    EXPECT_EQ(decoded.port, msg.port);
    EXPECT_EQ(decoded.config_epoch, msg.config_epoch);
    EXPECT_EQ(decoded.data1, msg.data1);
}

// ===== SELECT 拒绝 / ASKING 测试 =====
TEST(KvCluster, SelectAndAsking) {
    ClusterManager::ptr cluster(new ClusterManager);
    cluster->init("node1", "127.0.0.1", 6379, 16379);
    cluster->addSlots({0, 1, 2});
    bindClusterManager(cluster);

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    registerClusterCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    // SELECT 非 0 应该被拒绝
    RespValue sel = dispatch->dispatch(ctx, arrayCmd({"SELECT", "1"}), store);
    EXPECT_EQ(sel.type, RespType::Error);
    EXPECT_NE(sel.str.find("not allowed in cluster mode"), std::string::npos);

    // ASKING 应该返回 OK
    RespValue asking = dispatch->dispatch(ctx, arrayCmd({"ASKING"}), store);
    EXPECT_EQ(asking.type, RespType::SimpleString);
    EXPECT_EQ(asking.str, "OK");
}

// ===== Cluster 运维命令测试 =====
TEST(KvCluster, ClusterAdmin) {
    ClusterManager mgr;
    mgr.init("master1", "127.0.0.1", 6379, 16379);
    mgr.addSlots({0, 1, 2, 3, 4});

    // 添加副本
    auto rep = std::make_shared<ClusterNode>();
    rep->node_id = "replica1";
    rep->ip = "127.0.0.1";
    rep->port = 6380;
    rep->cluster_port = 16380;
    rep->setFlag(ClusterNodeFlag::Slave);
    rep->master_id = "master1";
    mgr.addNode(rep);

    auto replicas = mgr.getReplicas("master1");
    EXPECT_EQ(replicas.size(), 1u);
    EXPECT_EQ(replicas[0]->node_id, "replica1");

    uint64_t old_epoch = mgr.getConfigEpoch();
    (void)old_epoch;
    mgr.bumpConfigEpoch();
    EXPECT_EQ(mgr.getConfigEpoch(), old_epoch + 1);

    // 故障转移后副本继承槽位
    mgr.failoverReplica("replica1");
    EXPECT_FALSE(mgr.isSlave());
    EXPECT_TRUE(mgr.isMySlot(3));
}

// ===== Quorum-based failure voting =====
TEST(KvCluster, QuorumFailover) {
    ClusterManager mgr;
    mgr.init("myself", "127.0.0.1", 6379, 16379);
    mgr.addSlots({0, 1, 2});

    // 添加 4 个其他主节点
    for(int i = 0; i < 4; ++i) {
        auto m = std::make_shared<ClusterNode>();
        m->node_id = std::string("master") + std::to_string(i);
        m->ip = "127.0.0.1";
        m->port = 7000 + i;
        m->cluster_port = 17000 + i;
        m->setFlag(ClusterNodeFlag::Master);
        mgr.addNode(m);
    }

    auto target = std::make_shared<ClusterNode>();
    target->node_id = "target";
    target->ip = "127.0.0.1";
    target->port = 7999;
    target->cluster_port = 17999;
    target->setFlag(ClusterNodeFlag::Master);
    target->slots.set(10);
    mgr.addNode(target);

    // 模拟 target 长时间无响应 -> PFail
    mgr.updateNodePongReceived("target", 1000);
    mgr.checkFailureDetection(1000 + 20000, 15000);
    EXPECT_TRUE(mgr.isNodePFail("target"));
    EXPECT_FALSE(mgr.isNodeFailed("target"));

    // 6 个 master，quorum = 4；myself 已通过 checkFailureDetection 投 1 票
    mgr.addFailureReport("target", "master0");
    mgr.addFailureReport("target", "master1");
    mgr.checkFailureDetection(1000 + 21000, 15000);
    EXPECT_FALSE(mgr.isNodeFailed("target")); // 3/6 < 4

    mgr.addFailureReport("target", "master2");
    mgr.checkFailureDetection(1000 + 22000, 15000);
    EXPECT_TRUE(mgr.isNodeFailed("target")); // 4/6 >= 4
}

// ===== Epoch conflict resolution =====
TEST(KvCluster, EpochConflict) {
    ClusterManager mgr;
    mgr.init("nodeA", "127.0.0.1", 6379, 16379);
    mgr.bumpConfigEpoch(); // nodeA epoch = 1
    mgr.addSlots({0, 1, 2});

    auto nodeB = std::make_shared<ClusterNode>();
    nodeB->node_id = "nodeB";
    nodeB->ip = "127.0.0.1";
    nodeB->port = 6380;
    nodeB->cluster_port = 16380;
    nodeB->setFlag(ClusterNodeFlag::Master);
    nodeB->config_epoch = 2;
    nodeB->slots.set(0);
    mgr.addNode(nodeB);

    // nodeB 以更高 epoch 声明 slot 0，应接管
    mgr.updateNodeSlots("nodeB", {0}, 2);
    EXPECT_FALSE(mgr.getMyself()->slots.test(0));
    EXPECT_TRUE(nodeB->slots.test(0));

    // nodeA 以低 epoch 再次声明 slot 0，应失败
    mgr.updateNodeSlots("nodeA", {0}, 1);
    EXPECT_FALSE(mgr.getMyself()->slots.test(0));

    // 相同 epoch 时，node_id 字典序大者获胜
    auto nodeC = std::make_shared<ClusterNode>();
    nodeC->node_id = "nodeZ";
    nodeC->ip = "127.0.0.1";
    nodeC->port = 6381;
    nodeC->cluster_port = 16381;
    nodeC->setFlag(ClusterNodeFlag::Master);
    nodeC->config_epoch = 2;
    mgr.addNode(nodeC);
    mgr.updateNodeSlots("nodeZ", {0}, 2);
    auto nodeZ = mgr.getNode("nodeZ");
    EXPECT_TRUE(nodeZ != nullptr);
    EXPECT_TRUE(nodeZ->slots.test(0));
    EXPECT_FALSE(nodeB->slots.test(0));
}

// ===== Cluster announce IP/port =====
TEST(KvCluster, ClusterAnnounce) {
    ClusterManager mgr;
    mgr.init("myself", "0.0.0.0", 6379, 16379, "192.168.1.10", 7000);
    auto myself = mgr.getMyself();
    EXPECT_EQ(myself->ip, "192.168.1.10");
    EXPECT_EQ(myself->port, 7000);
    EXPECT_EQ(myself->cluster_port, 16379);
    EXPECT_EQ(mgr.myAddr(), "192.168.1.10:7000");
}

// ===== Multi-DB GETKEYSINSLOT =====
TEST(KvCluster, MultiDbGetkeysinslot) {
    ClusterManager mgr;
    mgr.init("myself", "127.0.0.1", 6379, 16379);
    mgr.addSlots({0, 1, 2});

    KvStore::ptr store(new KvStore);
    int slot = keyToSlot("key_in_db1");
    store->set(1, "key_in_db1", "value");
    store->set(0, "key_in_db0", "value");

    auto keys = mgr.getKeysInSlot(slot, 10, store);
    bool found = false;
    for(const auto& k : keys) {
        if(k == "key_in_db1") found = true;
    }
    EXPECT_TRUE(found);
}

// ===== cluster-slave-validity-factor =====
TEST(KvCluster, SlaveValidityFactor) {
    ClusterManager mgr;
    mgr.init("replica", "127.0.0.1", 6381, 16381);
    mgr.setSlaveValidityFactor(10);

    auto master = std::make_shared<ClusterNode>();
    master->node_id = "master";
    master->ip = "127.0.0.1";
    master->port = 6379;
    master->cluster_port = 16379;
    master->setFlag(ClusterNodeFlag::Master);
    mgr.addNode(master);

    mgr.setReplication("master");

    // pong 时间很近，有效
    master->pong_received = 1000;
    EXPECT_TRUE(mgr.slaveIsValidForFailover("replica", 2000, 15000));

    // 断开时间正好等于限制，仍有效
    EXPECT_TRUE(mgr.slaveIsValidForFailover("replica", 151000, 15000));

    // 断开时间超过限制
    EXPECT_FALSE(mgr.slaveIsValidForFailover("replica", 151001, 15000));

    // factor 0 表示不检查
    mgr.setSlaveValidityFactor(0);
    EXPECT_TRUE(mgr.slaveIsValidForFailover("replica", 999999, 15000));
}

// ===== cluster-migration-barrier =====
TEST(KvCluster, MigrationBarrier) {
    ClusterManager mgr;
    mgr.init("masterA", "127.0.0.1", 6379, 16379);
    mgr.setMigrationBarrier(2);

    // masterA 有 1 个副本，不能迁出
    auto r1 = std::make_shared<ClusterNode>();
    r1->node_id = "r1";
    r1->setFlag(ClusterNodeFlag::Slave);
    r1->master_id = "masterA";
    mgr.addNode(r1);
    EXPECT_FALSE(mgr.canDonateReplica("masterA"));

    // 增加到 2 个副本，可以迁出
    auto r2 = std::make_shared<ClusterNode>();
    r2->node_id = "r2";
    r2->setFlag(ClusterNodeFlag::Slave);
    r2->master_id = "masterA";
    mgr.addNode(r2);
    EXPECT_TRUE(mgr.canDonateReplica("masterA"));

    // barrier 为 0 时总是允许
    mgr.setMigrationBarrier(0);
    EXPECT_TRUE(mgr.canDonateReplica("masterA"));
}

/**
 * @file test_kv_phase10.cc
 * @brief KV 模块 - 单元测试 - test_kv_phase10
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
#include "zero/kv/persistence/rdb.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"

#include <cstdio>
#include <fstream>
#include <sstream>
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

TEST(KvPhase10, NewCommands) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"SETNX", "nxk", "1"}), store).integer, 1);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"SETNX", "nxk", "2"}), store).integer, 0);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"HSETNX", "hk", "f", "v"}), store).integer, 1);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"HSETNX", "hk", "f", "v2"}), store).integer, 0);

    store->setex(ctx.db, "exk", "old", 3600);
    RespValue gex = dispatch->dispatch(ctx, arrayCmd({"GETEX", "exk", "EX", "10"}), store);
    EXPECT_EQ(gex.str, "old");
    EXPECT_GT(store->ttl(ctx.db, "exk"), 0);
    EXPECT_LE(store->ttl(ctx.db, "exk"), 10);

    dispatch->dispatch(ctx, arrayCmd({"SADD", "sk", "a", "b"}), store);
    RespValue sp = dispatch->dispatch(ctx, arrayCmd({"SPOP", "sk"}), store);
    EXPECT_FALSE(sp.is_null);

    dispatch->dispatch(ctx, arrayCmd({"ZADD", "zk", "1", "m1", "2", "m2"}), store);
    RespValue zp = dispatch->dispatch(ctx, arrayCmd({"ZPOPMIN", "zk"}), store);
    EXPECT_EQ(zp.type, RespType::Array);
    EXPECT_EQ(zp.array.size(), 2u);

    ctx.client_id = 42;
    ctx.client_name = "tester";
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"CLIENT", "ID"}), store).integer, 42);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"CLIENT", "SETNAME", "foo"}), store).str, "OK");
    EXPECT_EQ(ctx.client_name, "foo");
}

TEST(KvPhase10, StreamGroups) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue id_rsp = dispatch->dispatch(ctx, arrayCmd({"XADD", "s", "*", "f", "1"}), store);
    const std::string sid = id_rsp.str;
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"XGROUP", "CREATE", "s", "g1", "0"}), store).str, "OK");
    RespValue read = dispatch->dispatch(
        ctx, arrayCmd({"XREADGROUP", "GROUP", "g1", "c1", "STREAMS", "s", ">"}), store);
    EXPECT_EQ(read.type, RespType::Array);
    EXPECT_FALSE(read.array.empty());
    EXPECT_GE(dispatch->dispatch(ctx, arrayCmd({"XACK", "s", "g1", sid}), store).integer, 1);
}

TEST(KvPhase10, ReadonlySlave) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    ReplicationManager::ptr repl(new ReplicationManager);
    bindReplicationForConfig(repl);
    repl->setSlaveOf("127.0.0.1", 6379);

    KvContext client_ctx;
    client_ctx.replication = repl.get();
    RespValue wr = dispatch->dispatch(client_ctx, arrayCmd({"SET", "k", "v"}), store);
    EXPECT_EQ(wr.type, RespType::Error);
    EXPECT_NE(wr.str.find("READONLY"), std::string::npos);

    KvContext repl_ctx;
    repl_ctx.repl_apply = true;
    repl_ctx.authenticated = true;
    EXPECT_EQ(dispatch->dispatch(repl_ctx, arrayCmd({"SET", "k", "v"}), store).str, "OK");
}

TEST(KvPhase10, MaxmemoryPolicies) {
    KvStore::ptr store(new KvStore);
    store->setMaxMemory(200);
    store->setMaxMemoryPolicy("volatile-random");
    for(int i = 0; i < 20; ++i) {
        store->setex(0, "k" + std::to_string(i), "value-with-some-bytes", 60);
    }
    store->set(0, "persist", "x");
    EXPECT_TRUE(store->usedMemoryApprox() <= store->maxMemory() || store->dbsize(0) < 21);
}

TEST(KvPhase10, RdbV2Binary) {
    const std::string path = "/tmp/zero_redis_rdb_v2.rdb";
    std::remove(path.c_str());

    KvStore::ptr store(new KvStore);
    store->set(0, "bin", "v2");
    store->setRdbPath(path);
    ASSERT_TRUE(store->saveRdb(nullptr));

    std::string raw;
    {
        std::ifstream ifs(path, std::ios::binary);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        raw = ss.str();
    }
    EXPECT_EQ(raw.compare(0, 6, "SYRDB2"), 0);

    KvStore::ptr loaded(new KvStore);
    ASSERT_TRUE(Rdb::load(*loaded, path, nullptr));
    std::string val;
    bool found = false;
    EXPECT_TRUE(loaded->get(0, "bin", val, found));
    EXPECT_TRUE(found);
    EXPECT_EQ(val, "v2");

    std::string json = R"({"version":1,"saved_at_ms":0,"databases":[]})";
    KvStore::ptr from_json(new KvStore);
    EXPECT_TRUE(Rdb::loadFromString(*from_json, json, nullptr));

    std::remove(path.c_str());
}

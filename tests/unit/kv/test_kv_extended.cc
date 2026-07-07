/**
 * @file test_kv_extended.cc
 * @brief KV 模块 - 单元测试 - test_kv_extended
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/command_dispatch.h"
#include "zero/kv/persistence/rdb.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/resp.h"
#include "zero/kv/resp_reader.h"
#include "zero/kv/kv_config.h"
#include "zero/kv/store/kv_store.h"

#include <gtest/gtest.h>

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

static void applyPayload(KvStore::ptr store, CommandDispatch::ptr dispatch, const std::string& payload) {
    KvContext ctx;
    size_t pos = 0;
    while(pos < payload.size()) {
        RespValue req;
        size_t consumed = 0;
        RespReader reader(payload.data() + pos, payload.size() - pos);
        if(reader.tryParse(req, &consumed) != ParseStatus::Ok) {
            break;
        }
        pos += consumed;
        dispatch->dispatch(ctx, req, store);
    }
}

TEST(KvExtended, Auth) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    setRequirePass("secret");
    RespValue denied = dispatch->dispatch(ctx, arrayCmd({"GET", "k"}), store);
    EXPECT_EQ(denied.type, RespType::Error);

    RespValue auth = dispatch->dispatch(ctx, arrayCmd({"AUTH", "secret"}), store);
    EXPECT_EQ(auth.str, "OK");
    EXPECT_TRUE(ctx.authenticated);

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);
    RespValue get = dispatch->dispatch(ctx, arrayCmd({"GET", "k"}), store);
    EXPECT_EQ(get.str, "v");
    setRequirePass("");
}

TEST(KvExtended, MaxmemoryEviction) {
    KvStore::ptr store(new KvStore);
    store->setMaxMemory(300);
    store->setMaxMemoryPolicy("allkeys-lru");
    store->set(0, "a", std::string(120, 'a'));
    store->set(0, "b", std::string(120, 'b'));
    store->set(0, "c", std::string(120, 'c'));
    EXPECT_LE(store->dbsize(0), 2u);
}

TEST(KvExtended, StreamCommands) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue add = dispatch->dispatch(
        ctx, arrayCmd({"XADD", "mystream", "*", "f1", "v1", "f2", "v2"}), store);
    EXPECT_EQ(add.type, RespType::BulkString);
    EXPECT_FALSE(add.str.empty());

    RespValue len = dispatch->dispatch(ctx, arrayCmd({"XLEN", "mystream"}), store);
    EXPECT_EQ(len.integer, 1);

    RespValue range = dispatch->dispatch(
        ctx, arrayCmd({"XRANGE", "mystream", "-", "+"}), store);
    EXPECT_GE(range.array.size(), 0u);

    RespValue typ = dispatch->dispatch(ctx, arrayCmd({"TYPE", "mystream"}), store);
    EXPECT_EQ(typ.str, "stream");
}

TEST(KvExtended, Phase8Commands) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "hi"}), store);
    RespValue append = dispatch->dispatch(ctx, arrayCmd({"APPEND", "s", "!!"}), store);
    EXPECT_EQ(append.integer, 4);

    RespValue slen = dispatch->dispatch(ctx, arrayCmd({"STRLEN", "s"}), store);
    EXPECT_EQ(slen.integer, 4);

    dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f", "1"}), store);
    RespValue hincr = dispatch->dispatch(ctx, arrayCmd({"HINCRBY", "h", "f", "2"}), store);
    EXPECT_EQ(hincr.integer, 3);

    dispatch->dispatch(ctx, arrayCmd({"LPUSH", "l", "a", "b", "c"}), store);
    RespValue lindex = dispatch->dispatch(ctx, arrayCmd({"LINDEX", "l", "1"}), store);
    EXPECT_EQ(lindex.str, "b");

    dispatch->dispatch(ctx, arrayCmd({"SADD", "s1", "x", "y"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SADD", "s2", "y", "z"}), store);
    RespValue inter = dispatch->dispatch(ctx, arrayCmd({"SINTER", "s1", "s2"}), store);
    EXPECT_EQ(inter.array.size(), 1u);
    EXPECT_EQ(inter.array[0].str, "y");

    dispatch->dispatch(ctx, arrayCmd({"SET", "old", "1"}), store);
    RespValue rename = dispatch->dispatch(ctx, arrayCmd({"RENAME", "old", "new"}), store);
    EXPECT_EQ(rename.str, "OK");
    RespValue get = dispatch->dispatch(ctx, arrayCmd({"GET", "new"}), store);
    EXPECT_EQ(get.str, "1");
}

TEST(KvExtended, ReplSyncSimulation) {
    CommandDispatch::ptr master_dispatch(new CommandDispatch);
    CommandDispatch::ptr slave_dispatch(new CommandDispatch);
    registerBuiltinCommands(master_dispatch);
    registerBuiltinCommands(slave_dispatch);
    KvStore::ptr master_store(new KvStore);
    KvStore::ptr slave_store(new KvStore);
    ReplicationManager::ptr repl(new ReplicationManager);
    bindReplicationForConfig(repl);
    KvContext ctx;

    master_dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v1"}), master_store);
    repl->propagateCommand(arrayCmd({"SET", "k", "v1"}));

    ReplicationManager::PsyncResult full = repl->handlePsync("?", -1, master_store);
    EXPECT_EQ(full.kind, ReplicationManager::PsyncResult::Kind::Full);
    EXPECT_TRUE(Rdb::loadFromString(*slave_store, full.payload, nullptr));

    master_dispatch->dispatch(ctx, arrayCmd({"SET", "k2", "v2"}), master_store);
    const std::string cmd = RespEncoder::encode(arrayCmd({"SET", "k2", "v2"}));
    repl->propagateCommand(arrayCmd({"SET", "k2", "v2"}));

    ReplicationManager::PsyncResult partial =
        repl->handlePsync(repl->replId(), repl->replOffset() - (int64_t)cmd.size(), master_store);
    EXPECT_EQ(partial.kind, ReplicationManager::PsyncResult::Kind::Partial);
    applyPayload(slave_store, slave_dispatch, partial.payload);

    std::string val;
    bool found = false;
    EXPECT_TRUE(slave_store->get(0, "k", val, found) && found && val == "v1");
    EXPECT_TRUE(slave_store->get(0, "k2", val, found) && found && val == "v2");
}

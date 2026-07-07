/**
 * @file test_kv_repl.cc
 * @brief KV 模块 - 单元测试 - test_kv_repl
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

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

TEST(KvRepl, RoleAndSlaveof) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    ReplicationManager::ptr repl(new ReplicationManager);
    bindReplicationForConfig(repl);
    KvContext ctx;

    RespValue role = dispatch->dispatch(ctx, arrayCmd({"ROLE"}), store);
    EXPECT_EQ(role.type, RespType::Array);
    EXPECT_EQ(role.array.size(), 3u);
    EXPECT_EQ(role.array[0].str, "master");

    RespValue slaveof = dispatch->dispatch(
        ctx, arrayCmd({"SLAVEOF", "127.0.0.1", "6379"}), store);
    EXPECT_EQ(slaveof.str, "OK");
    EXPECT_EQ(repl->role(), ReplicationManager::Role::Slave);
    EXPECT_EQ(repl->masterHost(), "127.0.0.1");
    EXPECT_EQ(repl->masterPort(), 6379);

    RespValue role2 = dispatch->dispatch(ctx, arrayCmd({"ROLE"}), store);
    EXPECT_EQ(role2.array[0].str, "slave");

    RespValue no_one = dispatch->dispatch(ctx, arrayCmd({"SLAVEOF", "NO", "ONE"}), store);
    EXPECT_EQ(no_one.str, "OK");
    EXPECT_EQ(repl->role(), ReplicationManager::Role::Master);
}

TEST(KvRepl, PsyncFullAndPartial) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    ReplicationManager::ptr repl(new ReplicationManager);
    bindReplicationForConfig(repl);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);
    repl->propagateCommand(arrayCmd({"SET", "k", "v"}));

    ReplicationManager::PsyncResult full =
        repl->handlePsync("?", -1, store);
    EXPECT_EQ(full.kind, ReplicationManager::PsyncResult::Kind::Full);
    EXPECT_NE(full.line.str.find("FULLRESYNC"), std::string::npos);
    EXPECT_FALSE(full.payload.empty());

    std::string dumped;
    EXPECT_TRUE(Rdb::dump(*store, dumped, nullptr));
    KvStore::ptr loaded(new KvStore);
    EXPECT_TRUE(Rdb::loadFromString(*loaded, dumped, nullptr));

    repl->propagateCommand(arrayCmd({"SET", "k2", "v2"}));
    ReplicationManager::PsyncResult partial =
        repl->handlePsync(repl->replId(), repl->replOffset() - 1, store);
    EXPECT_EQ(partial.kind, ReplicationManager::PsyncResult::Kind::Partial);
    EXPECT_EQ(partial.line.str, "CONTINUE");
    EXPECT_FALSE(partial.payload.empty());
}

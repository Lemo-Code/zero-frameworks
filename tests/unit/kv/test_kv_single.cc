/**
 * @file test_kv_single.cc
 * @brief KV 模块 - 单元测试 - test_kv_single
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
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

TEST(KvSingle, MultiKeyAndTtl) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "a", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "b", "2"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SETEX", "c", "60", "3"}), store);

    RespValue ex = dispatch->dispatch(ctx, arrayCmd({"EXISTS", "a", "b", "missing"}), store);
    EXPECT_EQ(ex.integer, 2);

    RespValue del = dispatch->dispatch(ctx, arrayCmd({"DEL", "a", "b"}), store);
    EXPECT_EQ(del.integer, 2);

    RespValue persist = dispatch->dispatch(ctx, arrayCmd({"PERSIST", "c"}), store);
    EXPECT_EQ(persist.integer, 1);
    EXPECT_EQ(store->ttl(ctx.db, "c"), -1);
}

TEST(KvSingle, StringExtras) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "old"}), store);
    RespValue gs = dispatch->dispatch(ctx, arrayCmd({"GETSET", "k", "new"}), store);
    EXPECT_EQ(gs.str, "old");
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"GET", "k"}), store).str, "new");

    dispatch->dispatch(ctx, arrayCmd({"SET", "d", "v"}), store);
    RespValue gd = dispatch->dispatch(ctx, arrayCmd({"GETDEL", "d"}), store);
    EXPECT_EQ(gd.str, "v");
    EXPECT_FALSE(store->exists(ctx.db, "d"));
}

TEST(KvSingle, ListWriteOps) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"RPUSH", "l", "a", "b", "a", "c"}), store);
    dispatch->dispatch(ctx, arrayCmd({"LTRIM", "l", "0", "2"}), store);
    EXPECT_GE(dispatch->dispatch(ctx, arrayCmd({"LLEN", "l"}), store).integer, 2);

    dispatch->dispatch(ctx, arrayCmd({"LSET", "l", "1", "x"}), store);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"LINDEX", "l", "1"}), store).str, "x");

    RespValue rem = dispatch->dispatch(ctx, arrayCmd({"LREM", "l", "0", "a"}), store);
    EXPECT_GE(rem.integer, 0);

    dispatch->dispatch(ctx, arrayCmd({"RPUSH", "s", "1"}), store);
    RespValue rpl = dispatch->dispatch(ctx, arrayCmd({"RPOPLPUSH", "s", "l"}), store);
    EXPECT_EQ(rpl.str, "1");
    EXPECT_GE(dispatch->dispatch(ctx, arrayCmd({"LLEN", "l"}), store).integer, 2);
}

TEST(KvSingle, SetAlgebra) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SADD", "s1", "a", "b", "c"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SADD", "s2", "b", "c", "d"}), store);

    RespValue inter = dispatch->dispatch(ctx, arrayCmd({"SINTER", "s1", "s2"}), store);
    EXPECT_EQ(inter.array.size(), 2u);

    RespValue sunion = dispatch->dispatch(ctx, arrayCmd({"SUNION", "s1", "s2"}), store);
    EXPECT_EQ(sunion.array.size(), 4u);

    RespValue sdiff = dispatch->dispatch(ctx, arrayCmd({"SDIFF", "s1", "s2"}), store);
    EXPECT_EQ(sdiff.array.size(), 1u);
    EXPECT_EQ(sdiff.array[0].str, "a");
}

TEST(KvSingle, ZpopBothEnds) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"ZADD", "z", "1", "m1", "2", "m2", "3", "m3"}), store);

    RespValue pmin = dispatch->dispatch(ctx, arrayCmd({"ZPOPMIN", "z"}), store);
    EXPECT_EQ(pmin.array.size(), 2u);
    EXPECT_EQ(pmin.array[0].str, "m1");

    RespValue pmax = dispatch->dispatch(ctx, arrayCmd({"ZPOPMAX", "z"}), store);
    EXPECT_EQ(pmax.array.size(), 2u);
    EXPECT_EQ(pmax.array[0].str, "m3");
}

TEST(KvSingle, HdelMulti) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"HMSET", "h", "f1", "1", "f2", "2", "f3", "3"}), store);
    RespValue hdel = dispatch->dispatch(ctx, arrayCmd({"HDEL", "h", "f1", "f3"}), store);
    EXPECT_EQ(hdel.integer, 2);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"HLEN", "h"}), store).integer, 1);
}

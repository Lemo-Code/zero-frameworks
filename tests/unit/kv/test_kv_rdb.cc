/**
 * @file test_kv_rdb.cc
 * @brief KV 模块 - 单元测试 - test_kv_rdb
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"

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

TEST(KvRdb, RoundTrip) {
    const std::string path = "/tmp/zero_redis_rdb_test.json";
    std::remove(path.c_str());

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    store->setRdbPath(path);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "hello"}), store);
    dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f", "v"}), store);
    dispatch->dispatch(ctx, arrayCmd({"LPUSH", "l", "a", "b"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SADD", "st", "x", "y"}), store);
    dispatch->dispatch(ctx, arrayCmd({"ZADD", "z", "10", "m1", "20", "m2"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SELECT", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "db1", "x"}), store);

    RespValue save = dispatch->dispatch(ctx, arrayCmd({"SAVE"}), store);
    EXPECT_EQ(save.str, "OK");
    EXPECT_GT(store->lastSaveUnixSec(), 0);

    KvStore::ptr loaded(new KvStore);
    loaded->setRdbPath(path);
    EXPECT_TRUE(loaded->loadRdb(nullptr));

    KvContext ctx2;
    ctx2.db = 0;
    RespValue get_s = dispatch->dispatch(ctx2, arrayCmd({"GET", "s"}), loaded);
    EXPECT_EQ(get_s.str, "hello");

    RespValue hget = dispatch->dispatch(ctx2, arrayCmd({"HGET", "h", "f"}), loaded);
    EXPECT_EQ(hget.str, "v");

    RespValue llen = dispatch->dispatch(ctx2, arrayCmd({"LLEN", "l"}), loaded);
    EXPECT_EQ(llen.integer, 2);

    RespValue scard = dispatch->dispatch(ctx2, arrayCmd({"SCARD", "st"}), loaded);
    EXPECT_EQ(scard.integer, 2);

    RespValue zcard = dispatch->dispatch(ctx2, arrayCmd({"ZCARD", "z"}), loaded);
    EXPECT_EQ(zcard.integer, 2);

    dispatch->dispatch(ctx2, arrayCmd({"SELECT", "1"}), loaded);
    RespValue db1 = dispatch->dispatch(ctx2, arrayCmd({"GET", "db1"}), loaded);
    EXPECT_EQ(db1.str, "x");

    RespValue lastsave = dispatch->dispatch(ctx2, arrayCmd({"LASTSAVE"}), loaded);
    EXPECT_EQ(lastsave.integer, 0);

    std::remove(path.c_str());
}

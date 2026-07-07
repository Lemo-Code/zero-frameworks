/**
 * @file test_kv_aof.cc
 * @brief KV 模块 - 单元测试 - test_kv_aof
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
#include "zero/kv/persistence/aof.h"
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

TEST(KvAof, Replay) {
    const std::string path = "/tmp/zero_redis_aof_test.aof";
    std::remove(path.c_str());

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    AofLog::ptr aof(new AofLog);
    aof->setPath(path);
    aof->setEnabled(true);

    RespValue set = dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);
    EXPECT_EQ(set.str, "OK");
    EXPECT_TRUE(aof->append(arrayCmd({"SET", "k", "v"})));

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "overwrite"}), store);
    EXPECT_TRUE(aof->append(arrayCmd({"SET", "k", "overwrite"})));

    KvStore::ptr loaded(new KvStore);
    EXPECT_TRUE(aof->replay(loaded, dispatch, nullptr));

    KvContext ctx2;
    RespValue get = dispatch->dispatch(ctx2, arrayCmd({"GET", "k"}), loaded);
    EXPECT_EQ(get.str, "overwrite");

    std::remove(path.c_str());
}

TEST(KvAof, Rewrite) {
    const std::string path = "/tmp/zero_redis_aof_rewrite_test.aof";
    std::remove(path.c_str());

    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    bindAofForConfig(AofLog::ptr());
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);
    dispatch->dispatch(ctx, arrayCmd({"ZADD", "z", "1.5", "m"}), store);

    AofLog::ptr aof(new AofLog);
    aof->setPath(path);
    aof->setEnabled(true);
    bindAofForConfig(aof);
    EXPECT_TRUE(aof->rewrite(store, nullptr));

    KvStore::ptr loaded(new KvStore);
    EXPECT_TRUE(aof->replay(loaded, dispatch, nullptr));

    KvContext ctx2;
    RespValue get = dispatch->dispatch(ctx2, arrayCmd({"GET", "k"}), loaded);
    EXPECT_EQ(get.str, "v");

    RespValue zcard = dispatch->dispatch(ctx2, arrayCmd({"ZCARD", "z"}), loaded);
    EXPECT_EQ(zcard.integer, 1);

    std::remove(path.c_str());
}

/**
 * @file test_kv_p0p1.cc
 * @brief KV 模块 - 单元测试 - test_kv_p0p1
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
#include "zero/kv/resp.h"
#include "zero/kv/slowlog.h"
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

TEST(KvP0P1, HashAndStringP0) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue hs = dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f1", "v1", "f2", "v2"}), store);
    EXPECT_EQ(hs.integer, 2);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"HGET", "h", "f1"}), store).str, "v1");

    dispatch->dispatch(ctx, arrayCmd({"SET", "a", "1"}), store);
    RespValue mnx = dispatch->dispatch(ctx, arrayCmd({"MSETNX", "b", "2", "c", "3"}), store);
    EXPECT_EQ(mnx.integer, 1);
    RespValue mnx2 = dispatch->dispatch(ctx, arrayCmd({"MSETNX", "a", "x"}), store);
    EXPECT_EQ(mnx2.integer, 0);

    RespValue f = dispatch->dispatch(ctx, arrayCmd({"INCRBYFLOAT", "f", "1.5"}), store);
    EXPECT_EQ(f.str, "1.5");
    f = dispatch->dispatch(ctx, arrayCmd({"INCRBYFLOAT", "f", "2.25"}), store);
    EXPECT_EQ(f.str, "3.75");
}

TEST(KvP0P1, ZsetSetP0) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"ZADD", "z", "1", "a", "2", "b", "3", "c"}), store);
    RespValue zc = dispatch->dispatch(ctx, arrayCmd({"ZCOUNT", "z", "1", "2"}), store);
    EXPECT_EQ(zc.integer, 2);

    RespValue zr = dispatch->dispatch(ctx, arrayCmd({"ZRANGEBYSCORE", "z", "1", "2"}), store);
    EXPECT_EQ(zr.array.size(), 2u);

    dispatch->dispatch(ctx, arrayCmd({"SADD", "s1", "a", "b", "c"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SADD", "s2", "b", "c", "d"}), store);
    RespValue sis = dispatch->dispatch(ctx, arrayCmd({"SINTERSTORE", "out", "s1", "s2"}), store);
    EXPECT_EQ(sis.integer, 2);

    dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f1", "v1", "f2", "v2"}), store);
    RespValue hscan = dispatch->dispatch(ctx, arrayCmd({"HSCAN", "h", "0"}), store);
    EXPECT_EQ(hscan.array.size(), 2u);

    RespValue ch = dispatch->dispatch(ctx, arrayCmd({"ZADD", "z", "CH", "XX", "2.5", "a"}), store);
    EXPECT_EQ(ch.integer, 1);
}

TEST(KvP0P1, StreamDumpP0) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"XADD", "s", "*", "k", "v"}), store);
    dispatch->dispatch(ctx, arrayCmd({"XADD", "s", "*", "k", "v2"}), store);
    dispatch->dispatch(ctx, arrayCmd({"XADD", "s", "*", "k", "v3"}), store);
    RespValue trim = dispatch->dispatch(ctx, arrayCmd({"XTRIM", "s", "MAXLEN", "2"}), store);
    EXPECT_EQ(trim.integer, 1);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"XLEN", "s"}), store).integer, 2);

    dispatch->dispatch(ctx, arrayCmd({"XGROUP", "CREATE", "s", "g", "0"}), store);
    dispatch->dispatch(ctx, arrayCmd({"XREADGROUP", "GROUP", "g", "c1", "COUNT", "1", "STREAMS", "s", ">"}), store);
    RespValue xp = dispatch->dispatch(ctx, arrayCmd({"XPENDING", "s", "g"}), store);
    EXPECT_EQ(xp.type, RespType::Array);
    EXPECT_GE(xp.array[0].integer, 1);

    dispatch->dispatch(ctx, arrayCmd({"SET", "dk", "payload"}), store);
    RespValue dump = dispatch->dispatch(ctx, arrayCmd({"DUMP", "dk"}), store);
    EXPECT_FALSE(dump.is_null);
    dispatch->dispatch(ctx, arrayCmd({"DEL", "dk"}), store);
    dispatch->dispatch(ctx, arrayCmd({"RESTORE", "dk2", "0", dump.str}), store);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"GET", "dk2"}), store).str, "payload");

    RespValue time = dispatch->dispatch(ctx, arrayCmd({"TIME"}), store);
    EXPECT_EQ(time.array.size(), 2u);
}

TEST(KvP0P1, P1Extras) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"RPUSH", "l", "a", "b", "c"}), store);
    RespValue ins = dispatch->dispatch(ctx, arrayCmd({"LINSERT", "l", "BEFORE", "b", "x"}), store);
    EXPECT_EQ(ins.integer, 4);

    dispatch->dispatch(ctx, arrayCmd({"RPUSH", "src", "v"}), store);
    RespValue br = dispatch->dispatch(ctx, arrayCmd({"BRPOPLPUSH", "src", "dst", "0"}), store);
    EXPECT_EQ(br.str, "v");

    getSlowlog().setSlowerThanUs(0);
    dispatch->dispatch(ctx, arrayCmd({"PING"}), store);
    RespValue sl = dispatch->dispatch(ctx, arrayCmd({"SLOWLOG", "LEN"}), store);
    EXPECT_GE(sl.integer, 1);

    RespValue cfg = dispatch->dispatch(ctx, arrayCmd({"CONFIG", "GET", "slowlog-max-len"}), store);
    EXPECT_GE(cfg.array.size(), 2u);

    dispatch->dispatch(ctx, arrayCmd({"CLIENT", "SETNAME", "tester"}), store);
    EXPECT_EQ(dispatch->dispatch(ctx, arrayCmd({"CLIENT", "GETNAME"}), store).str, "tester");
}

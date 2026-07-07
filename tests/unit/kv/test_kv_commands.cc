/**
 * @file test_kv_commands.cc
 * @brief KV 模块 - 单元测试 - test_kv_commands
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/command_dispatch.h"
#include "zero/kv/blocking/blocking_list_hub.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"

#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

TEST(KvCommands, SetGet) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue set_rsp = dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);
    EXPECT_EQ(set_rsp.type, RespType::SimpleString);
    EXPECT_EQ(set_rsp.str, "OK");

    RespValue get_rsp = dispatch->dispatch(ctx, arrayCmd({"GET", "k"}), store);
    EXPECT_EQ(get_rsp.type, RespType::BulkString);
    EXPECT_EQ(get_rsp.str, "v");
}

TEST(KvCommands, ConfigMaxmemory) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue rsp = dispatch->dispatch(
        ctx, arrayCmd({"CONFIG", "GET", "maxmemory"}), store);
    EXPECT_EQ(rsp.type, RespType::Array);
    EXPECT_EQ(rsp.array.size(), 2u);
    EXPECT_EQ(rsp.array[0].str, "maxmemory");
    EXPECT_EQ(rsp.array[1].str, "0");
}

TEST(KvCommands, InfoAndDbsize) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "a", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "b", "2"}), store);

    RespValue info = dispatch->dispatch(ctx, arrayCmd({"INFO"}), store);
    EXPECT_EQ(info.type, RespType::BulkString);
    EXPECT_NE(info.str.find("redis_version:"), std::string::npos);
    EXPECT_NE(info.str.find("keys=2"), std::string::npos);

    RespValue dbsize = dispatch->dispatch(ctx, arrayCmd({"DBSIZE"}), store);
    EXPECT_EQ(dbsize.type, RespType::Integer);
    EXPECT_EQ(dbsize.integer, 2);
}

TEST(KvCommands, SelectFlushdb) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "x", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SELECT", "1"}), store);
    EXPECT_EQ(ctx.db, 1);

    RespValue miss = dispatch->dispatch(ctx, arrayCmd({"GET", "x"}), store);
    EXPECT_TRUE(miss.is_null);

    dispatch->dispatch(ctx, arrayCmd({"SET", "x", "2"}), store);
    dispatch->dispatch(ctx, arrayCmd({"FLUSHDB"}), store);

    RespValue dbsize = dispatch->dispatch(ctx, arrayCmd({"DBSIZE"}), store);
    EXPECT_EQ(dbsize.integer, 0);
}

TEST(KvCommands, ExpireTtl) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "k", "v"}), store);

    RespValue ttl_none = dispatch->dispatch(ctx, arrayCmd({"TTL", "k"}), store);
    EXPECT_EQ(ttl_none.integer, -1);

    RespValue exp = dispatch->dispatch(ctx, arrayCmd({"EXPIRE", "k", "60"}), store);
    EXPECT_EQ(exp.integer, 1);

    RespValue ttl = dispatch->dispatch(ctx, arrayCmd({"TTL", "k"}), store);
    EXPECT_TRUE(ttl.integer >= 59 && ttl.integer <= 60);

    RespValue info = dispatch->dispatch(ctx, arrayCmd({"INFO"}), store);
    EXPECT_NE(info.str.find("expires=1"), std::string::npos);

    RespValue setex = dispatch->dispatch(ctx, arrayCmd({"SETEX", "a", "30", "1"}), store);
    EXPECT_EQ(setex.str, "OK");

    RespValue del_exp = dispatch->dispatch(ctx, arrayCmd({"EXPIRE", "a", "0"}), store);
    EXPECT_EQ(del_exp.integer, 1);

    RespValue miss = dispatch->dispatch(ctx, arrayCmd({"GET", "a"}), store);
    EXPECT_TRUE(miss.is_null);
}

TEST(KvCommands, SetNxXxIncr) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue nx1 = dispatch->dispatch(ctx, arrayCmd({"SET", "k", "1", "NX"}), store);
    EXPECT_EQ(nx1.str, "OK");

    RespValue nx2 = dispatch->dispatch(ctx, arrayCmd({"SET", "k", "2", "NX"}), store);
    EXPECT_TRUE(nx2.is_null);

    RespValue get1 = dispatch->dispatch(ctx, arrayCmd({"GET", "k"}), store);
    EXPECT_EQ(get1.str, "1");

    RespValue xx_miss = dispatch->dispatch(ctx, arrayCmd({"SET", "miss", "x", "XX"}), store);
    EXPECT_TRUE(xx_miss.is_null);

    RespValue xx_ok = dispatch->dispatch(ctx, arrayCmd({"SET", "k", "3", "XX"}), store);
    EXPECT_EQ(xx_ok.str, "OK");

    RespValue incr = dispatch->dispatch(ctx, arrayCmd({"INCR", "counter"}), store);
    EXPECT_EQ(incr.integer, 1);

    RespValue incr2 = dispatch->dispatch(ctx, arrayCmd({"INCRBY", "counter", "5"}), store);
    EXPECT_EQ(incr2.integer, 6);

    RespValue decr = dispatch->dispatch(ctx, arrayCmd({"DECR", "counter"}), store);
    EXPECT_EQ(decr.integer, 5);

    dispatch->dispatch(ctx, arrayCmd({"SET", "bad", "x"}), store);
    RespValue bad_incr = dispatch->dispatch(ctx, arrayCmd({"INCR", "bad"}), store);
    EXPECT_EQ(bad_incr.type, RespType::Error);
}

TEST(KvCommands, ActiveExpire) {
    KvStore::ptr store(new KvStore);
    store->set(0, "k", "v");
    store->pexpire(0, "k", 1);

    bool found = true;
    std::string value;
    store->get(0, "k", value, found);
    EXPECT_TRUE(found);

    usleep(5000);

    EXPECT_GE(store->purgeExpiredActive(100), 1);
    store->get(0, "k", value, found);
    EXPECT_FALSE(found);
}

TEST(KvCommands, KeysScanFlushall) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "user:1", "a"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "user:2", "b"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "other", "c"}), store);

    RespValue keys = dispatch->dispatch(ctx, arrayCmd({"KEYS", "user:*"}), store);
    EXPECT_EQ(keys.type, RespType::Array);
    EXPECT_EQ(keys.array.size(), 2u);

    RespValue scan1 = dispatch->dispatch(ctx, arrayCmd({"SCAN", "0", "MATCH", "user:*", "COUNT", "1"}), store);
    EXPECT_EQ(scan1.type, RespType::Array);
    EXPECT_EQ(scan1.array.size(), 2u);
    EXPECT_EQ(scan1.array[1].type, RespType::Array);
    EXPECT_EQ(scan1.array[1].array.size(), 1u);

    dispatch->dispatch(ctx, arrayCmd({"SELECT", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "x", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"FLUSHALL"}), store);

    RespValue dbsize = dispatch->dispatch(ctx, arrayCmd({"DBSIZE"}), store);
    EXPECT_EQ(dbsize.integer, 0);
}

TEST(KvCommands, Hash) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue hset1 = dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f1", "v1"}), store);
    EXPECT_EQ(hset1.integer, 1);

    RespValue hset2 = dispatch->dispatch(ctx, arrayCmd({"HSET", "h", "f1", "v2"}), store);
    EXPECT_EQ(hset2.integer, 0);

    RespValue hget = dispatch->dispatch(ctx, arrayCmd({"HGET", "h", "f1"}), store);
    EXPECT_EQ(hget.str, "v2");

    RespValue typ = dispatch->dispatch(ctx, arrayCmd({"TYPE", "h"}), store);
    EXPECT_EQ(typ.str, "hash");

    RespValue hlen = dispatch->dispatch(ctx, arrayCmd({"HLEN", "h"}), store);
    EXPECT_EQ(hlen.integer, 1);

    RespValue hex = dispatch->dispatch(ctx, arrayCmd({"HEXISTS", "h", "f1"}), store);
    EXPECT_EQ(hex.integer, 1);

    RespValue hall = dispatch->dispatch(ctx, arrayCmd({"HGETALL", "h"}), store);
    EXPECT_EQ(hall.array.size(), 2u);

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "x"}), store);
    RespValue bad = dispatch->dispatch(ctx, arrayCmd({"HGET", "s", "f"}), store);
    EXPECT_EQ(bad.type, RespType::Error);

    RespValue hdel = dispatch->dispatch(ctx, arrayCmd({"HDEL", "h", "f1"}), store);
    EXPECT_EQ(hdel.integer, 1);

    RespValue miss = dispatch->dispatch(ctx, arrayCmd({"HGET", "h", "f1"}), store);
    EXPECT_TRUE(miss.is_null);

    dispatch->dispatch(ctx, arrayCmd({"HMSET", "h2", "a", "1", "b", "2"}), store);
    RespValue hmget = dispatch->dispatch(ctx, arrayCmd({"HMGET", "h2", "a", "b", "z"}), store);
    EXPECT_EQ(hmget.array.size(), 3u);
    EXPECT_EQ(hmget.array[0].str, "1");
    EXPECT_EQ(hmget.array[1].str, "2");
    EXPECT_TRUE(hmget.array[2].is_null);

    RespValue hkeys = dispatch->dispatch(ctx, arrayCmd({"HKEYS", "h2"}), store);
    EXPECT_EQ(hkeys.array.size(), 2u);

    RespValue hvals = dispatch->dispatch(ctx, arrayCmd({"HVALS", "h2"}), store);
    EXPECT_EQ(hvals.array.size(), 2u);
}

TEST(KvCommands, Multi) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue multi = dispatch->dispatch(ctx, arrayCmd({"MULTI"}), store);
    EXPECT_EQ(multi.str, "OK");
    EXPECT_TRUE(ctx.multi_mode);

    RespValue q1 = dispatch->dispatch(ctx, arrayCmd({"SET", "m", "1"}), store);
    EXPECT_EQ(q1.str, "QUEUED");
    RespValue q2 = dispatch->dispatch(ctx, arrayCmd({"INCR", "m"}), store);
    EXPECT_EQ(q2.str, "QUEUED");

    RespValue exec = dispatch->dispatch(ctx, arrayCmd({"EXEC"}), store);
    EXPECT_EQ(exec.type, RespType::Array);
    EXPECT_EQ(exec.array.size(), 2u);
    EXPECT_FALSE(ctx.multi_mode);

    RespValue get = dispatch->dispatch(ctx, arrayCmd({"GET", "m"}), store);
    EXPECT_EQ(get.str, "2");
}

TEST(KvCommands, List) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue push = dispatch->dispatch(ctx, arrayCmd({"LPUSH", "lst", "a", "b", "c"}), store);
    EXPECT_EQ(push.integer, 3);

    RespValue typ = dispatch->dispatch(ctx, arrayCmd({"TYPE", "lst"}), store);
    EXPECT_EQ(typ.str, "list");

    RespValue llen = dispatch->dispatch(ctx, arrayCmd({"LLEN", "lst"}), store);
    EXPECT_EQ(llen.integer, 3);

    RespValue lpop = dispatch->dispatch(ctx, arrayCmd({"LPOP", "lst"}), store);
    EXPECT_EQ(lpop.str, "c");

    RespValue rpush = dispatch->dispatch(ctx, arrayCmd({"RPUSH", "lst", "d"}), store);
    EXPECT_EQ(rpush.integer, 3);

    RespValue range = dispatch->dispatch(ctx, arrayCmd({"LRANGE", "lst", "0", "-1"}), store);
    EXPECT_EQ(range.array.size(), 3u);
    EXPECT_EQ(range.array[0].str, "b");
    EXPECT_EQ(range.array[2].str, "d");

    RespValue rpop = dispatch->dispatch(ctx, arrayCmd({"RPOP", "lst"}), store);
    EXPECT_EQ(rpop.str, "d");

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "x"}), store);
    RespValue bad = dispatch->dispatch(ctx, arrayCmd({"LPUSH", "s", "y"}), store);
    EXPECT_EQ(bad.type, RespType::Error);
}

TEST(KvCommands, Set) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue add = dispatch->dispatch(ctx, arrayCmd({"SADD", "myset", "a", "b", "a"}), store);
    EXPECT_EQ(add.integer, 2);

    RespValue typ = dispatch->dispatch(ctx, arrayCmd({"TYPE", "myset"}), store);
    EXPECT_EQ(typ.str, "set");

    RespValue card = dispatch->dispatch(ctx, arrayCmd({"SCARD", "myset"}), store);
    EXPECT_EQ(card.integer, 2);

    RespValue is_mem = dispatch->dispatch(ctx, arrayCmd({"SISMEMBER", "myset", "b"}), store);
    EXPECT_EQ(is_mem.integer, 1);

    RespValue not_mem = dispatch->dispatch(ctx, arrayCmd({"SISMEMBER", "myset", "z"}), store);
    EXPECT_EQ(not_mem.integer, 0);

    RespValue members = dispatch->dispatch(ctx, arrayCmd({"SMEMBERS", "myset"}), store);
    EXPECT_EQ(members.array.size(), 2u);

    RespValue rem = dispatch->dispatch(ctx, arrayCmd({"SREM", "myset", "a", "x"}), store);
    EXPECT_EQ(rem.integer, 1);

    RespValue card2 = dispatch->dispatch(ctx, arrayCmd({"SCARD", "myset"}), store);
    EXPECT_EQ(card2.integer, 1);

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "x"}), store);
    RespValue bad = dispatch->dispatch(ctx, arrayCmd({"SADD", "s", "y"}), store);
    EXPECT_EQ(bad.type, RespType::Error);
}

TEST(KvCommands, Zset) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    RespValue add = dispatch->dispatch(
        ctx, arrayCmd({"ZADD", "rank", "1", "a", "2", "b", "2", "c"}), store);
    EXPECT_EQ(add.integer, 3);

    RespValue typ = dispatch->dispatch(ctx, arrayCmd({"TYPE", "rank"}), store);
    EXPECT_EQ(typ.str, "zset");

    RespValue card = dispatch->dispatch(ctx, arrayCmd({"ZCARD", "rank"}), store);
    EXPECT_EQ(card.integer, 3);

    RespValue score = dispatch->dispatch(ctx, arrayCmd({"ZSCORE", "rank", "b"}), store);
    EXPECT_EQ(score.str, "2");

    RespValue range = dispatch->dispatch(
        ctx, arrayCmd({"ZRANGE", "rank", "0", "-1", "WITHSCORES"}), store);
    EXPECT_EQ(range.array.size(), 6u);
    EXPECT_EQ(range.array[0].str, "a");
    EXPECT_EQ(range.array[1].str, "1");

    RespValue rem = dispatch->dispatch(ctx, arrayCmd({"ZREM", "rank", "c", "x"}), store);
    EXPECT_EQ(rem.integer, 1);

    RespValue incr = dispatch->dispatch(ctx, arrayCmd({"ZINCRBY", "rank", "2", "b"}), store);
    EXPECT_EQ(incr.str, "4");

    RespValue rank = dispatch->dispatch(ctx, arrayCmd({"ZRANK", "rank", "b"}), store);
    EXPECT_EQ(rank.integer, 1);

    RespValue revrank = dispatch->dispatch(ctx, arrayCmd({"ZREVRANK", "rank", "b"}), store);
    EXPECT_EQ(revrank.integer, 0);

    RespValue revrange = dispatch->dispatch(
        ctx, arrayCmd({"ZREVRANGE", "rank", "0", "0"}), store);
    EXPECT_EQ(revrange.array.size(), 1u);
    EXPECT_EQ(revrange.array[0].str, "b");

    dispatch->dispatch(ctx, arrayCmd({"SET", "s", "x"}), store);
    RespValue bad = dispatch->dispatch(ctx, arrayCmd({"ZADD", "s", "1", "m"}), store);
    EXPECT_EQ(bad.type, RespType::Error);
}

TEST(KvCommands, Watch) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"SET", "w", "1"}), store);
    dispatch->dispatch(ctx, arrayCmd({"WATCH", "w"}), store);
    dispatch->dispatch(ctx, arrayCmd({"MULTI"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "w", "2"}), store);
    RespValue exec = dispatch->dispatch(ctx, arrayCmd({"EXEC"}), store);
    EXPECT_EQ(exec.type, RespType::Array);
    EXPECT_EQ(exec.array.size(), 1u);
    EXPECT_EQ(exec.array[0].str, "OK");

    RespValue get = dispatch->dispatch(ctx, arrayCmd({"GET", "w"}), store);
    EXPECT_EQ(get.str, "2");

    dispatch->dispatch(ctx, arrayCmd({"WATCH", "w"}), store);
    dispatch->dispatch(ctx, arrayCmd({"MULTI"}), store);
    dispatch->dispatch(ctx, arrayCmd({"INCR", "w"}), store);
    dispatch->dispatch(ctx, arrayCmd({"SET", "w", "x"}), store);
    RespValue abort = dispatch->dispatch(ctx, arrayCmd({"EXEC"}), store);
    EXPECT_FALSE(abort.is_null);

    RespValue after = dispatch->dispatch(ctx, arrayCmd({"GET", "w"}), store);
    EXPECT_EQ(after.str, "x");
}

TEST(KvCommands, BlockingPopImmediate) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    BlockingListHub::ptr hub(new BlockingListHub);
    bindBlockingListHub(hub);
    KvContext ctx;

    dispatch->dispatch(ctx, arrayCmd({"LPUSH", "jobs", "task-a"}), store);
    RespValue pop = dispatch->dispatch(ctx, arrayCmd({"BLPOP", "jobs", "0"}), store);
    EXPECT_EQ(pop.type, RespType::Array);
    EXPECT_EQ(pop.array.size(), 2u);
    EXPECT_EQ(pop.array[0].str, "jobs");
    EXPECT_EQ(pop.array[1].str, "task-a");

    dispatch->dispatch(ctx, arrayCmd({"RPUSH", "q", "tail"}), store);
    RespValue got = dispatch->dispatch(ctx, arrayCmd({"BRPOP", "q", "0"}), store);
    EXPECT_EQ(got.array[1].str, "tail");

    RespValue empty = dispatch->dispatch(ctx, arrayCmd({"BLPOP", "missing", "0"}), store);
    EXPECT_TRUE(empty.is_null);
}

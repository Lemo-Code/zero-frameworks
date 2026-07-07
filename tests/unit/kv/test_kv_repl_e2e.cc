/**
 * @file test_kv_repl_e2e.cc
 * @brief KV 模块 - 单元测试 - test_kv_repl_e2e
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/command_dispatch.h"
#include "zero/kv/persistence/rdb.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <string>

using namespace zero::kv;

static const char* kMasterRdb = "/tmp/zero_repl_e2e_master.rdb";
static const char* kSlaveRdb = "/tmp/zero_repl_e2e_slave.rdb";

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

static void replApply(CommandDispatch::ptr dispatch, const RespValue& cmd, KvStore::ptr store) {
    KvContext ctx;
    ctx.repl_apply = true;
    ctx.authenticated = true;
    dispatch->dispatch(ctx, cmd, store);
}

static bool storeValueEq(CommandDispatch::ptr dispatch, KvStore::ptr store,
                         const RespValue& cmd, const RespValue& expect) {
    KvContext ctx;
    ctx.authenticated = true;
    RespValue got = dispatch->dispatch(ctx, cmd, store);
    if(expect.type == RespType::Integer) {
        return got.type == RespType::Integer && got.integer == expect.integer;
    }
    if(expect.type == RespType::BulkString) {
        return got.type == RespType::BulkString && got.str == expect.str;
    }
    return got.type == expect.type && got.str == expect.str;
}

static bool compareStoreData(CommandDispatch::ptr dispatch, KvStore::ptr master,
                             KvStore::ptr slave) {
    struct Row {
        RespValue cmd;
        RespValue expect;
    };
    const Row rows[] = {
        {arrayCmd({"GET", "repl_e2e"}), RespEncoder::bulk("after_incr")},
        {arrayCmd({"HGET", "h", "f"}), RespEncoder::bulk("v")},
        {arrayCmd({"LLEN", "l"}), RespEncoder::integer(2)},
        {arrayCmd({"SCARD", "st"}), RespEncoder::integer(2)},
    };
    for(const auto& row : rows) {
        if(!storeValueEq(dispatch, master, row.cmd, row.expect)) {
            return false;
        }
        if(!storeValueEq(dispatch, slave, row.cmd, row.expect)) {
            return false;
        }
    }
    return true;
}

static bool saveRdbFiles(KvStore::ptr master, KvStore::ptr slave) {
    master->setRdbPath(kMasterRdb);
    slave->setRdbPath(kSlaveRdb);
    std::remove(kMasterRdb);
    std::remove(kSlaveRdb);
    std::string err;
    if(!master->saveRdb(&err)) {
        return false;
    }
    if(!slave->saveRdb(&err)) {
        return false;
    }
    return true;
}

static bool saveAndCompareRdb(CommandDispatch::ptr dispatch, KvStore::ptr master,
                              KvStore::ptr slave) {
    if(!saveRdbFiles(master, slave)) {
        return false;
    }
    if(!compareStoreData(dispatch, master, slave)) {
        return false;
    }
    return true;
}

TEST(KvReplE2e, ReplDataSync) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);

    KvStore::ptr master(new KvStore);
    KvStore::ptr slave(new KvStore);
    ReplicationManager::ptr repl(new ReplicationManager);
    bindReplicationForConfig(repl);

    KvContext master_ctx;
    master_ctx.authenticated = true;
    master_ctx.replication = repl.get();

    dispatch->dispatch(master_ctx, arrayCmd({"SET", "repl_e2e", "ok"}), master);
    dispatch->dispatch(master_ctx, arrayCmd({"HSET", "h", "f", "v"}), master);
    dispatch->dispatch(master_ctx, arrayCmd({"LPUSH", "l", "a", "b"}), master);

    ReplicationManager::PsyncResult full = repl->handlePsync("?", -1, master);
    ASSERT_EQ(full.kind, ReplicationManager::PsyncResult::Kind::Full);
    ASSERT_FALSE(full.payload.empty());

    std::string err;
    ASSERT_TRUE(Rdb::loadFromString(*slave, full.payload, &err))
        << "load RDB to slave failed: " << err;

    const RespValue incr = arrayCmd({"SET", "repl_e2e", "after_incr"});
    dispatch->dispatch(master_ctx, incr, master);
    repl->propagateCommand(incr);
    replApply(dispatch, incr, slave);

    const RespValue sadd = arrayCmd({"SADD", "st", "x", "y"});
    dispatch->dispatch(master_ctx, sadd, master);
    repl->propagateCommand(sadd);
    replApply(dispatch, sadd, slave);

    ASSERT_TRUE(saveAndCompareRdb(dispatch, master, slave));

    KvContext slave_write_ctx;
    slave_write_ctx.authenticated = true;
    slave_write_ctx.replication = repl.get();
    repl->setSlaveOf("127.0.0.1", 6379);
    RespValue readonly_rsp =
        dispatch->dispatch(slave_write_ctx, arrayCmd({"SET", "x", "y"}), slave);
    EXPECT_EQ(readonly_rsp.type, RespType::Error);
    EXPECT_NE(readonly_rsp.str.find("READONLY"), std::string::npos);
}

/**
 * @file test_kv_blocking.cc
 * @brief KV 模块 - 单元测试 - test_kv_blocking
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/io/iomanager.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/concurrency/mutex.h"
#include "zero/kv/blocking/blocking_list_hub.h"
#include "zero/kv/command_dispatch.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"

#include <gtest/gtest.h>
#include <memory>

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

struct BlockingTestState {
    zero::Mutex mutex;
    bool pop_done = false;
    RespValue pop;
};

static bool waitPopDone(const std::shared_ptr<BlockingTestState>& state, int max_yield) {
    for(int i = 0; i < max_yield; ++i) {
        {
            zero::Mutex::Lock lock(state->mutex);
            if(state->pop_done) {
                return true;
            }
        }
        zero::Fiber::YieldToHold();
    }
    zero::Mutex::Lock lock(state->mutex);
    return state->pop_done;
}

static bool checkPopArray(const BlockingTestState& state, const std::string& key,
                          const std::string& value) {
    if(!state.pop_done) {
        return false;
    }
    if(state.pop.type != RespType::Array || state.pop.array.size() != 2) {
        return false;
    }
    return state.pop.array[0].str == key && state.pop.array[1].str == value;
}

static int runBlockingPopWake() {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    BlockingListHub::ptr hub(new BlockingListHub);
    bindBlockingListHub(hub);

    auto state = std::make_shared<BlockingTestState>();
    auto done = std::make_shared<bool>(false);
    auto rc = std::make_shared<int>(0);
    zero::IOManager* iom = zero::IOManager::GetThis();

    iom->schedule([dispatch, store, state]() {
        KvContext ctx;
        RespValue pop = dispatch->dispatch(ctx, arrayCmd({"BLPOP", "wakeq", "5"}), store);
        zero::Mutex::Lock lock(state->mutex);
        state->pop = pop;
        state->pop_done = true;
    });

    iom->schedule([dispatch, store]() {
        for(int i = 0; i < 30; ++i) {
            zero::Fiber::YieldToHold();
        }
        KvContext ctx;
        dispatch->dispatch(ctx, arrayCmd({"LPUSH", "wakeq", "payload"}), store);
    });

    iom->schedule([state, done, rc]() {
        if(!waitPopDone(state, 10000) || !checkPopArray(*state, "wakeq", "payload")) {
            *rc = 1;
        }
        *done = true;
    });

    while(!*done) {
        zero::Fiber::YieldToHold();
    }
    return *rc;
}

static int runBlockingPopTimeout() {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    BlockingListHub::ptr hub(new BlockingListHub);
    bindBlockingListHub(hub);

    auto state = std::make_shared<BlockingTestState>();
    auto done = std::make_shared<bool>(false);
    auto rc = std::make_shared<int>(0);
    zero::IOManager* iom = zero::IOManager::GetThis();

    iom->schedule([dispatch, store, state]() {
        KvContext ctx;
        RespValue pop = dispatch->dispatch(ctx, arrayCmd({"BLPOP", "emptyq", "1"}), store);
        zero::Mutex::Lock lock(state->mutex);
        state->pop = pop;
        state->pop_done = true;
    });

    iom->schedule([state, done, rc]() {
        if(!waitPopDone(state, 30000)) {
            *rc = 1;
        } else {
            zero::Mutex::Lock lock(state->mutex);
            if(!state->pop.is_null) {
                *rc = 1;
            }
        }
        *done = true;
    });

    while(!*done) {
        zero::Fiber::YieldToHold();
    }
    return *rc;
}

static int runBrpopWake() {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    BlockingListHub::ptr hub(new BlockingListHub);
    bindBlockingListHub(hub);

    auto state = std::make_shared<BlockingTestState>();
    auto done = std::make_shared<bool>(false);
    auto rc = std::make_shared<int>(0);
    zero::IOManager* iom = zero::IOManager::GetThis();

    iom->schedule([dispatch, store, state]() {
        KvContext ctx;
        RespValue pop = dispatch->dispatch(ctx, arrayCmd({"BRPOP", "brq", "5"}), store);
        zero::Mutex::Lock lock(state->mutex);
        state->pop = pop;
        state->pop_done = true;
    });

    iom->schedule([dispatch, store]() {
        for(int i = 0; i < 30; ++i) {
            zero::Fiber::YieldToHold();
        }
        KvContext ctx;
        dispatch->dispatch(ctx, arrayCmd({"RPUSH", "brq", "tail"}), store);
    });

    iom->schedule([state, done, rc]() {
        if(!waitPopDone(state, 10000) || !checkPopArray(*state, "brq", "tail")) {
            *rc = 1;
        }
        *done = true;
    });

    while(!*done) {
        zero::Fiber::YieldToHold();
    }
    return *rc;
}

template<typename Fn>
static int runInIOManager(Fn fn) {
    int rc = 0;
    zero::IOManager iom(2, true, "redis-blocking-test");
    iom.schedule([&]() {
        rc = fn();
        iom.stop();
    });
    return rc;
}

TEST(KvBlocking, BlockingPopWake) {
    EXPECT_EQ(runInIOManager(runBlockingPopWake), 0);
}

TEST(KvBlocking, BlockingPopTimeout) {
    EXPECT_EQ(runInIOManager(runBlockingPopTimeout), 0);
}

TEST(KvBlocking, BrpopWake) {
    EXPECT_EQ(runInIOManager(runBrpopWake), 0);
}

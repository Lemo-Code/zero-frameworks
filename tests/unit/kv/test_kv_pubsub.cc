/**
 * @file test_kv_pubsub.cc
 * @brief KV 模块 - 单元测试 - PubSub
 */

#include <gtest/gtest.h>
#include "zero/kv/command_dispatch.h"
#include "zero/kv/pubsub/pubsub_hub.h"
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

// Test pubsub hub directly (doesn't need session/network)
TEST(KvPubSub, HubDirect) {
    PubSubHub hub;

    // publish to empty hub returns 0
    EXPECT_EQ(hub.publish("news", "hello"), 0);

    // null session operations are safe (no-op)
    hub.subscribe("news", nullptr);
    hub.psubscribe("news.*", nullptr);
    EXPECT_EQ(hub.publish("news", "world"), 0);
}

// Test dispatch error handling without session
TEST(KvPubSub, DispatchWithoutSession) {
    CommandDispatch::ptr dispatch(new CommandDispatch);
    registerBuiltinCommands(dispatch);
    KvStore::ptr store(new KvStore);
    PubSubHub::ptr hub(new PubSubHub);

    KvContext ctx;
    ctx.pubsub = hub.get();
    // No session set — commands requiring session will fail

    RespValue sub = dispatch->dispatch(ctx, arrayCmd({"SUBSCRIBE", "news"}), store);
    EXPECT_EQ(sub.type, RespType::Error);

    RespValue pub = dispatch->dispatch(ctx, arrayCmd({"PUBLISH", "news", "hello"}), store);
    EXPECT_EQ(pub.integer, 0);

    RespValue unsub = dispatch->dispatch(ctx, arrayCmd({"UNSUBSCRIBE"}), store);
    EXPECT_EQ(unsub.type, RespType::Error);
}

// Test publish counts subscribers
TEST(KvPubSub, PublishCounts) {
    PubSubHub hub;
    // Publish without subscribers returns 0
    EXPECT_EQ(hub.publish("chan", "msg"), 0);
    EXPECT_EQ(hub.publish("another", "msg"), 0);
}

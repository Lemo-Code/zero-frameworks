/**
 * @file kv_context.h
 * @brief 单连接 Redis 会话状态
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_KV_CONTEXT_H__
#define __ZERO_KV_KV_CONTEXT_H__

#include "zero/kv/resp.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace zero {
namespace kv {

class PubSubHub;
class ReplicationManager;

class KvSession;

struct WatchEntry {
    int db = 0;
    std::string key;
    uint64_t token = 0;
};

struct KvContext {
    int db = 0;
    bool quit = false;
    bool authenticated = false;
    bool pubsub_mode = false;
    bool multi_mode = false;
    bool repl_apply = false;
    bool replica_connection = false;
    int64_t client_id = 0;
    std::string client_name;
    std::string client_addr;
    std::vector<RespValue> multi_queue;
    std::vector<WatchEntry> watched;

    PubSubHub* pubsub = nullptr;
    ReplicationManager* replication = nullptr;
    std::weak_ptr<KvSession> session;
};

} // namespace kv
} // namespace zero

#endif

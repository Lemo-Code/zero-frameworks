/**
 * @file blocking_list_hub.h
 * @brief BLPOP/BRPOP 阻塞等待 hub
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_BLOCKING_BLOCKING_LIST_HUB_H__
#define __ZERO_KV_BLOCKING_BLOCKING_LIST_HUB_H__

#include "zero/core/concurrency/fiber.h"
#include "zero/core/concurrency/mutex.h"
#include "zero/core/concurrency/scheduler.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace zero {
namespace kv {

class KvStore;
class KvSession;

struct ListWaiter {
    std::weak_ptr<KvSession> session;
    int db = 0;
    std::vector<std::string> keys;
    bool pop_right = false;
    int64_t deadline_ms = -1;
    zero::Scheduler* scheduler = nullptr;
    zero::Fiber::ptr fiber;
    bool done = false;
    std::string result_key;
    std::string result_value;
};

class BlockingListHub {
public:
    typedef std::shared_ptr<BlockingListHub> ptr;

    void addWaiter(ListWaiter* waiter);
    void removeWaiter(ListWaiter* waiter);
    void cancelWaiters(const std::shared_ptr<KvSession>& session);
    void onPush(int db, const std::string& key, std::shared_ptr<KvStore> store);

private:
    bool tryFulfill(ListWaiter* waiter, std::shared_ptr<KvStore> store);

    zero::Mutex m_mutex;
    std::list<ListWaiter*> m_waiters;
};

void bindBlockingListHub(BlockingListHub::ptr hub);
BlockingListHub::ptr getBlockingListHub();

} // namespace kv
} // namespace zero

#endif

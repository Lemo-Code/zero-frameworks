/**
 * @file blocking_list_hub.cc
 * @brief 阻塞列表实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "blocking_list_hub.h"

#include "zero/kv/kv_session.h"
#include "zero/kv/store/kv_store.h"

namespace zero {
namespace kv {

namespace {

BlockingListHub::ptr g_blocking_hub;

} // namespace

void bindBlockingListHub(BlockingListHub::ptr hub) {
    g_blocking_hub = std::move(hub);
}

BlockingListHub::ptr getBlockingListHub() {
    return g_blocking_hub;
}

void BlockingListHub::addWaiter(ListWaiter* waiter) {
    zero::Mutex::Lock lock(m_mutex);
    m_waiters.push_back(waiter);
}

void BlockingListHub::removeWaiter(ListWaiter* waiter) {
    zero::Mutex::Lock lock(m_mutex);
    m_waiters.remove(waiter);
}

void BlockingListHub::cancelWaiters(const std::shared_ptr<KvSession>& session) {
    if(!session) {
        return;
    }
    std::vector<std::pair<zero::Scheduler*, zero::Fiber::ptr>> wake;
    {
        zero::Mutex::Lock lock(m_mutex);
        for(auto it = m_waiters.begin(); it != m_waiters.end();) {
            ListWaiter* w = *it;
            std::shared_ptr<KvSession> sp = w->session.lock();
            if(!sp || sp.get() != session.get()) {
                ++it;
                continue;
            }
            w->done = true;
            if(w->scheduler && w->fiber) {
                wake.emplace_back(w->scheduler, w->fiber);
            }
            it = m_waiters.erase(it);
        }
    }
    for(const auto& item : wake) {
        item.first->schedule(item.second, -1);
    }
}

bool BlockingListHub::tryFulfill(ListWaiter* waiter, std::shared_ptr<KvStore> store) {
    if(!waiter || !store) {
        return false;
    }
    for(const auto& key : waiter->keys) {
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listPop(waiter->db, key, !waiter->pop_right, value, found, &err)) {
            continue;
        }
        if(!found) {
            continue;
        }
        waiter->result_key = key;
        waiter->result_value = value;
        waiter->done = true;
        return true;
    }
    return false;
}

void BlockingListHub::onPush(int db, const std::string& key, std::shared_ptr<KvStore> store) {
    zero::Scheduler* scheduler = nullptr;
    zero::Fiber::ptr fiber;
    {
        zero::Mutex::Lock lock(m_mutex);
        for(auto it = m_waiters.begin(); it != m_waiters.end(); ++it) {
            ListWaiter* w = *it;
            if(w->db != db) {
                continue;
            }
            bool watching = false;
            for(const auto& k : w->keys) {
                if(k == key) {
                    watching = true;
                    break;
                }
            }
            if(!watching) {
                continue;
            }
            if(tryFulfill(w, store)) {
                scheduler = w->scheduler;
                fiber = w->fiber;
                m_waiters.erase(it);
                break;
            }
        }
    }
    if(scheduler && fiber) {
        scheduler->schedule(fiber, -1);
    }
}

} // namespace kv
} // namespace zero

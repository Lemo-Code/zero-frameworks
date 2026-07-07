/**
 * @file pubsub_hub.h
 * @brief Redis Pub/Sub 频道 hub（连接级订阅 + 模式订阅）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_PUBSUB_PUBSUB_HUB_H__
#define __ZERO_KV_PUBSUB_PUBSUB_HUB_H__

#include "zero/core/concurrency/mutex.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zero {
namespace kv {

class KvSession;

class PubSubHub {
public:
    typedef std::shared_ptr<PubSubHub> ptr;

    void subscribe(const std::string& channel, const std::shared_ptr<KvSession>& session);
    void unsubscribe(const std::string& channel, const std::shared_ptr<KvSession>& session);
    void unsubscribeAll(const std::shared_ptr<KvSession>& session);

    void psubscribe(const std::string& pattern, const std::shared_ptr<KvSession>& session);
    void punsubscribe(const std::string& pattern, const std::shared_ptr<KvSession>& session);
    void punsubscribeAll(const std::shared_ptr<KvSession>& session);

    void removeSession(const std::shared_ptr<KvSession>& session);

    /** @return 收到消息的订阅者数量（含频道与模式订阅） */
    int64_t publish(const std::string& channel, const std::string& message);

    int64_t subscriptionCount(const std::shared_ptr<KvSession>& session) const;

private:
    using SessionPtr = std::shared_ptr<KvSession>;
    using SessionWeak = std::weak_ptr<KvSession>;

    static bool matchPattern(const std::string& pattern, const std::string& channel);

    void removeFromChannelMap(const std::string& channel, KvSession* session);
    void removeFromPatternMap(const std::string& pattern, KvSession* session);

    mutable zero::Mutex m_mutex;
    std::unordered_map<std::string, std::vector<SessionWeak>> m_channels;
    std::unordered_map<std::string, std::vector<SessionWeak>> m_patterns;
    std::unordered_map<KvSession*, std::unordered_set<std::string>> m_sessionChannels;
    std::unordered_map<KvSession*, std::unordered_set<std::string>> m_sessionPatterns;
};

} // namespace kv
} // namespace zero

#endif

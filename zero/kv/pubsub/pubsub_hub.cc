/**
 * @file pubsub_hub.cc
 * @brief 发布订阅实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "pubsub_hub.h"
#include "zero/kv/kv_session.h"
#include "zero/kv/resp.h"

namespace zero {
namespace kv {

namespace {

bool matchGlob(const std::string& pattern, const std::string& text) {
    size_t pi = 0;
    size_t ti = 0;
    size_t star_pi = std::string::npos;
    size_t star_ti = 0;

    while(ti < text.size()) {
        if(pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            ++pi;
            ++ti;
            continue;
        }
        if(pi < pattern.size() && pattern[pi] == '*') {
            star_pi = ++pi;
            star_ti = ti;
            continue;
        }
        if(star_pi != std::string::npos) {
            pi = star_pi;
            ti = ++star_ti;
            continue;
        }
        return false;
    }
    while(pi < pattern.size() && pattern[pi] == '*') {
        ++pi;
    }
    return pi == pattern.size();
}

} // namespace

bool PubSubHub::matchPattern(const std::string& pattern, const std::string& channel) {
    return matchGlob(pattern, channel);
}

void PubSubHub::removeFromChannelMap(const std::string& channel, KvSession* session) {
    auto cit = m_channels.find(channel);
    if(cit == m_channels.end()) {
        return;
    }
    auto& subs = cit->second;
    for(auto it = subs.begin(); it != subs.end();) {
        SessionPtr sp = it->lock();
        if(!sp || sp.get() == session) {
            it = subs.erase(it);
        } else {
            ++it;
        }
    }
    if(subs.empty()) {
        m_channels.erase(cit);
    }
}

void PubSubHub::removeFromPatternMap(const std::string& pattern, KvSession* session) {
    auto pit = m_patterns.find(pattern);
    if(pit == m_patterns.end()) {
        return;
    }
    auto& subs = pit->second;
    for(auto it = subs.begin(); it != subs.end();) {
        SessionPtr sp = it->lock();
        if(!sp || sp.get() == session) {
            it = subs.erase(it);
        } else {
            ++it;
        }
    }
    if(subs.empty()) {
        m_patterns.erase(pit);
    }
}

void PubSubHub::subscribe(const std::string& channel, const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    m_channels[channel].push_back(session);
    m_sessionChannels[session.get()].insert(channel);
}

void PubSubHub::unsubscribe(const std::string& channel, const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    auto sit = m_sessionChannels.find(session.get());
    if(sit == m_sessionChannels.end()) {
        return;
    }
    sit->second.erase(channel);
    if(sit->second.empty()) {
        m_sessionChannels.erase(sit);
    }
    removeFromChannelMap(channel, session.get());
}

void PubSubHub::unsubscribeAll(const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    auto sit = m_sessionChannels.find(session.get());
    if(sit != m_sessionChannels.end()) {
        std::vector<std::string> channels(sit->second.begin(), sit->second.end());
        m_sessionChannels.erase(sit);
        for(const auto& ch : channels) {
            removeFromChannelMap(ch, session.get());
        }
    }
}

void PubSubHub::psubscribe(const std::string& pattern, const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    m_patterns[pattern].push_back(session);
    m_sessionPatterns[session.get()].insert(pattern);
}

void PubSubHub::punsubscribe(const std::string& pattern, const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    auto sit = m_sessionPatterns.find(session.get());
    if(sit == m_sessionPatterns.end()) {
        return;
    }
    sit->second.erase(pattern);
    if(sit->second.empty()) {
        m_sessionPatterns.erase(sit);
    }
    removeFromPatternMap(pattern, session.get());
}

void PubSubHub::punsubscribeAll(const SessionPtr& session) {
    if(!session) {
        return;
    }
    zero::Mutex::Lock lock(m_mutex);
    auto sit = m_sessionPatterns.find(session.get());
    if(sit != m_sessionPatterns.end()) {
        std::vector<std::string> patterns(sit->second.begin(), sit->second.end());
        m_sessionPatterns.erase(sit);
        for(const auto& pat : patterns) {
            removeFromPatternMap(pat, session.get());
        }
    }
}

void PubSubHub::removeSession(const SessionPtr& session) {
    unsubscribeAll(session);
    punsubscribeAll(session);
}

int64_t PubSubHub::publish(const std::string& channel, const std::string& message) {
    std::vector<SessionPtr> channel_targets;
    std::vector<std::pair<std::string, SessionPtr>> pattern_targets;
    {
        zero::Mutex::Lock lock(m_mutex);
        auto cit = m_channels.find(channel);
        if(cit != m_channels.end()) {
            for(auto& weak : cit->second) {
                SessionPtr sp = weak.lock();
                if(sp) {
                    channel_targets.push_back(sp);
                }
            }
        }
        for(const auto& pair : m_patterns) {
            if(!matchPattern(pair.first, channel)) {
                continue;
            }
            for(const auto& weak : pair.second) {
                SessionPtr sp = weak.lock();
                if(sp) {
                    pattern_targets.emplace_back(pair.first, sp);
                }
            }
        }
    }

    int64_t delivered = 0;
    for(const auto& session : channel_targets) {
        RespValue payload;
        payload.type = RespType::Array;
        payload.array.push_back(RespEncoder::bulk("message"));
        payload.array.push_back(RespEncoder::bulk(channel));
        payload.array.push_back(RespEncoder::bulk(message));
        if(session->pushPubSubMessage(payload) > 0) {
            ++delivered;
        }
    }
    for(const auto& target : pattern_targets) {
        RespValue payload;
        payload.type = RespType::Array;
        payload.array.push_back(RespEncoder::bulk("pmessage"));
        payload.array.push_back(RespEncoder::bulk(target.first));
        payload.array.push_back(RespEncoder::bulk(channel));
        payload.array.push_back(RespEncoder::bulk(message));
        if(target.second->pushPubSubMessage(payload) > 0) {
            ++delivered;
        }
    }
    return delivered;
}

int64_t PubSubHub::subscriptionCount(const SessionPtr& session) const {
    if(!session) {
        return 0;
    }
    zero::Mutex::Lock lock(m_mutex);
    int64_t count = 0;
    auto cit = m_sessionChannels.find(session.get());
    if(cit != m_sessionChannels.end()) {
        count += (int64_t)cit->second.size();
    }
    auto pit = m_sessionPatterns.find(session.get());
    if(pit != m_sessionPatterns.end()) {
        count += (int64_t)pit->second.size();
    }
    return count;
}

} // namespace kv
} // namespace zero

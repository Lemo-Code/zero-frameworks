/**
 * @file slowlog.cc
 * @brief 慢查询日志实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "slowlog.h"
#include "store/kv_store.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {
namespace kv {

namespace {

Slowlog g_slowlog;

std::vector<std::string> argsFromRequest(const RespValue& request) {
    std::vector<std::string> args;
    if(request.type != RespType::Array) {
        return args;
    }
    for(const auto& item : request.array) {
        if(item.is_null) {
            args.emplace_back("(nil)");
        } else if(item.type == RespType::BulkString) {
            args.push_back(item.str);
        } else if(item.type == RespType::Integer) {
            args.push_back(std::to_string(item.integer));
        } else {
            args.push_back("?");
        }
    }
    return args;
}

} // namespace

void Slowlog::setMaxLen(int64_t n) {
    zero::Mutex::Lock lock(m_mutex);
    if(n < 0) {
        n = 0;
    }
    m_max_len = n;
    if(m_max_len > 0 && (int64_t)m_entries.size() > m_max_len) {
        m_entries.erase(m_entries.begin(),
                        m_entries.begin() + (m_entries.size() - (size_t)m_max_len));
    }
}

void Slowlog::setSlowerThanUs(int64_t us) {
    zero::Mutex::Lock lock(m_mutex);
    m_slower_than_us = us < 0 ? 0 : us;
}

int64_t Slowlog::maxLen() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_max_len;
}

int64_t Slowlog::slowerThanUs() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_slower_than_us;
}

void Slowlog::record(int64_t client_id, const std::string& client_addr,
                     const RespValue& request, int64_t duration_us) {
    zero::Mutex::Lock lock(m_mutex);
    if(duration_us < m_slower_than_us) {
        return;
    }
    SlowlogEntry entry;
    entry.id = m_next_id++;
    entry.timestamp_ms = KvStore::nowMs();
    entry.duration_us = duration_us;
    entry.args = argsFromRequest(request);
    entry.client_id = client_id;
    entry.client_addr = client_addr;
    m_entries.push_back(std::move(entry));
    if(m_max_len > 0 && (int64_t)m_entries.size() > m_max_len) {
        m_entries.erase(m_entries.begin());
    }
}

std::vector<SlowlogEntry> Slowlog::get(int64_t count) const {
    zero::Mutex::Lock lock(m_mutex);
    if(count <= 0 || count > (int64_t)m_entries.size()) {
        count = (int64_t)m_entries.size();
    }
    std::vector<SlowlogEntry> out;
    out.reserve((size_t)count);
    for(int64_t i = (int64_t)m_entries.size() - count; i < (int64_t)m_entries.size(); ++i) {
        if(i >= 0) {
            out.push_back(m_entries[(size_t)i]);
        }
    }
    return out;
}

int64_t Slowlog::len() const {
    zero::Mutex::Lock lock(m_mutex);
    return (int64_t)m_entries.size();
}

void Slowlog::reset() {
    zero::Mutex::Lock lock(m_mutex);
    m_entries.clear();
}

Slowlog& getSlowlog() {
    return g_slowlog;
}

} // namespace kv
} // namespace zero

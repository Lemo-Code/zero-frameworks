/**
 * @file slowlog.h
 * @brief 慢查询日志（SLOWLOG 命令）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_SLOWLOG_H__
#define __ZERO_KV_SLOWLOG_H__

#include "resp.h"
#include "zero/core/concurrency/mutex.h"
#include <cstdint>
#include <string>
#include <vector>

namespace zero {
namespace kv {

struct SlowlogEntry {
    int64_t id = 0;
    int64_t timestamp_ms = 0;
    int64_t duration_us = 0;
    std::vector<std::string> args;
    int64_t client_id = 0;
    std::string client_addr;
};

class Slowlog {
public:
    void setMaxLen(int64_t n);
    void setSlowerThanUs(int64_t us);
    int64_t maxLen() const;
    int64_t slowerThanUs() const;

    void record(int64_t client_id, const std::string& client_addr,
                const RespValue& request, int64_t duration_us);
    std::vector<SlowlogEntry> get(int64_t count) const;
    int64_t len() const;
    void reset();

private:
    mutable zero::Mutex m_mutex;
    int64_t m_max_len = 128;
    int64_t m_slower_than_us = 10000;
    int64_t m_next_id = 1;
    std::vector<SlowlogEntry> m_entries;
};

Slowlog& getSlowlog();

} // namespace kv
} // namespace zero

#endif

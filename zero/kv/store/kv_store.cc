/**
 * @file kv_store.cc
 * @brief KV 存储核心实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_store.h"
#include "zero/kv/persistence/rdb.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <random>

namespace zero {
namespace kv {

const char* KvStore::kWrongType = "WRONGTYPE Operation against a key holding the wrong kind of value";

namespace {

int64_t steadyNowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

int64_t KvStore::nowMs() {
    return steadyNowMs();
}

// ScopedAllShardsLock — lock/unlock all shards for global operations
KvStore::ScopedAllShardsLock::ScopedAllShardsLock(Shard* shards, bool write)
    : m_shards(shards), m_write(write) {
    if(write) {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.wrlock();
        }
    } else {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.rdlock();
        }
    }
}

KvStore::ScopedAllShardsLock::ScopedAllShardsLock(const Shard* shards, bool write)
    : m_shards(const_cast<Shard*>(shards)), m_write(write) {
    if(write) {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.wrlock();
        }
    } else {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.rdlock();
        }
    }
}

KvStore::ScopedAllShardsLock::~ScopedAllShardsLock() {
    for(int i = 0; i < kShardCount; ++i) {
        m_shards[i].m_mutex.unlock();
    }
}

void KvStore::lockAllShards(bool write) {
    if(write) {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.wrlock();
        }
    } else {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].m_mutex.rdlock();
        }
    }
}

void KvStore::unlockAllShards() {
    for(int i = 0; i < kShardCount; ++i) {
        m_shards[i].m_mutex.unlock();
    }
}

void KvStore::bumpWatchKey(int db, const std::string& key) {
    m_shards[shardIdx(key)].bumpWatchKey(db, key);
}

void KvStore::bumpWatchDb(int db) {
    for(int i = 0; i < kShardCount; ++i) {
        m_shards[i].bumpWatchDb(db);
    }
}

uint64_t KvStore::watchToken(int db, const std::string& key) const {
    const Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    shard.m_hasWatches.store(true, std::memory_order_relaxed);
    return shard.watchToken(db, key);
}

bool KvStore::parseIntegerValue(const std::string& s, int64_t& out) {
    if(s.empty()) {
        return false;
    }
    char* end = nullptr;
    long long n = std::strtoll(s.c_str(), &end, 10);
    if(end != s.c_str() + s.size()) {
        return false;
    }
    out = (int64_t)n;
    return true;
}

bool KvStore::matchGlob(const std::string& pattern, const std::string& text) {
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

bool KvStore::isExpired(const Entry& e, int64_t now) const {
    return e.expire_at_ms > 0 && now >= e.expire_at_ms;
}

void KvStore::purgeExpiredDb(DbMap& db, int64_t now) const {
    for(auto it = db.begin(); it != db.end();) {
        if(isExpired(it->second, now)) {
            it = db.erase(it);
        } else {
            ++it;
        }
    }
}

KvStore::Entry* KvStore::findEntry(DbMap& db, const std::string& key, int64_t now) {
    auto it = db.find(key);
    if(it == db.end()) {
        return nullptr;
    }
    if(isExpired(it->second, now)) {
        db.erase(it);
        return nullptr;
    }
    return &it->second;
}

void KvStore::collectKeys(const DbMap& db, int64_t now, const std::string& pattern,
                          std::vector<std::string>& out) const {
    for(const auto& pair : db) {
        if(isExpired(pair.second, now)) {
            continue;
        }
        if(matchGlob(pattern, pair.first)) {
            out.push_back(pair.first);
        }
    }
    std::sort(out.begin(), out.end());
}

bool KvStore::get(int db, const std::string& key, std::string& value_out, bool& found,
                  std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::String) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    value_out = e->value;
    return true;
}

bool KvStore::exists(int db, const std::string& key) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    return findEntry(dbmap, key, now) != nullptr;
}

std::string KvStore::type(int db, const std::string& key) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return "none";
    }
    switch(e->type) {
        case ObjType::Hash:
            return "hash";
        case ObjType::List:
            return "list";
        case ObjType::Set:
            return "set";
        case ObjType::ZSet:
            return "zset";
        case ObjType::Stream:
            return "stream";
        default:
            return "string";
    }
}

void KvStore::set(int db, const std::string& key, const std::string& value) {
    setOpt(db, key, value, false, false, -1, -1);
}

void KvStore::setex(int db, const std::string& key, const std::string& value, int64_t ttl_seconds) {
    setOpt(db, key, value, false, false, ttl_seconds, -1);
}

int KvStore::setOpt(int db, const std::string& key, const std::string& value,
                    bool nx, bool xx, int64_t ex_seconds, int64_t px_milliseconds) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);  // takes all-shard lock internally; release before single-shard lock
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(nx && existing) {
        return 0;
    }
    if(xx && !existing) {
        return 0;
    }

    if(existing) {
        // Reuse findEntry pointer — skip second hash lookup (Phase 3)
        existing->value = value;
        // Only clear collection fields on type change (Phase 4)
        if(existing->type != ObjType::String) {
            existing->hash.clear();
            existing->list.clear();
            existing->set.clear();
            existing->zset.clear();
            existing->stream.clear();
            existing->stream_last_ms = 0;
            existing->stream_last_seq = 0;
            existing->type = ObjType::String;
        }
        if(ex_seconds >= 0) {
            existing->expire_at_ms = now + ex_seconds * 1000;
        } else if(px_milliseconds >= 0) {
            existing->expire_at_ms = now + px_milliseconds;
        } else {
            existing->expire_at_ms = 0;
        }
    } else {
        // New entry — all fields already default-initialized, skip clears.
        Entry& e = dbmap[key];
        e.value = value;
        if(ex_seconds >= 0) {
            e.expire_at_ms = now + ex_seconds * 1000;
        } else if(px_milliseconds >= 0) {
            e.expire_at_ms = now + px_milliseconds;
        }
    }
    shard.bumpWatchKey(db, key);
    return 1;
}

bool KvStore::del(int db, const std::string& key) {
    return delMany(db, std::vector<std::string>{key}) > 0;
}

int64_t KvStore::delMany(int db, const std::vector<std::string>& keys) {
    if(keys.empty()) {
        return 0;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    int64_t removed = 0;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            continue;
        }
        Entry* e = findEntry(dit->second, key, now);
        if(!e) {
            continue;
        }
        dit->second.erase(key);
        shard.bumpWatchKey(db, key);
        ++removed;
    }
    return removed;
}

int64_t KvStore::existsMany(int db, const std::vector<std::string>& keys) {
    if(keys.empty()) {
        return 0;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    int64_t n = 0;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        if(dit != shard.m_dbs.end() && findEntry(dit->second, key, now)) {
            ++n;
        }
    }
    return n;
}

int64_t KvStore::persist(int db, const std::string& key) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    const int64_t now = nowMs();
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    e->expire_at_ms = 0;
    return 1;
}

bool KvStore::getset(int db, const std::string& key, const std::string& value, std::string& old_out,
                     bool& found, std::string* err) {
    if(!get(db, key, old_out, found, err)) {
        return false;
    }
    set(db, key, value);
    return true;
}

bool KvStore::getdel(int db, const std::string& key, std::string& value_out, bool& found,
                     std::string* err) {
    if(!get(db, key, value_out, found, err)) {
        return false;
    }
    if(found) {
        del(db, key);
    }
    return true;
}

int64_t KvStore::dbsize(int db) {
    ScopedAllShardsLock lock(m_shards, false);
    int64_t total = 0;
    const int64_t now = nowMs();
    for(int i = 0; i < kShardCount; ++i) {
        auto dit = m_shards[i].m_dbs.find(db);
        if(dit != m_shards[i].m_dbs.end()) {
            purgeExpiredDb(dit->second, now);
            total += (int64_t)dit->second.size();
        }
    }
    return total;
}

int64_t KvStore::expireCount(int db) {
    ScopedAllShardsLock lock(m_shards, false);
    int64_t n = 0;
    const int64_t now = nowMs();
    for(int i = 0; i < kShardCount; ++i) {
        auto dit = m_shards[i].m_dbs.find(db);
        if(dit == m_shards[i].m_dbs.end()) {
            continue;
        }
        for(auto it = dit->second.begin(); it != dit->second.end();) {
            if(isExpired(it->second, now)) {
                it = dit->second.erase(it);
                continue;
            }
            if(it->second.expire_at_ms > 0) {
                ++n;
            }
            ++it;
        }
    }
    return n;
}

void KvStore::flushdb(int db) {
    ScopedAllShardsLock lock(m_shards, true);
    for(int i = 0; i < kShardCount; ++i) {
        m_shards[i].m_dbs[db].clear();
        m_shards[i].bumpWatchDb(db);
    }
}

void KvStore::flushall() {
    ScopedAllShardsLock lock(m_shards, true);
    for(int i = 0; i < kShardCount; ++i) {
        m_shards[i].m_dbs.clear();
    }
    for(int db = 0; db < kMaxDb; ++db) {
        for(int i = 0; i < kShardCount; ++i) {
            m_shards[i].bumpWatchDb(db);
        }
    }
}

std::vector<std::string> KvStore::keys(int db, const std::string& pattern) {
    ScopedAllShardsLock lock(m_shards, false);
    const int64_t now = nowMs();
    std::vector<std::string> out;
    for(int i = 0; i < kShardCount; ++i) {
        auto dit = m_shards[i].m_dbs.find(db);
        if(dit != m_shards[i].m_dbs.end()) {
            purgeExpiredDb(dit->second, now);
            collectKeys(dit->second, now, pattern, out);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

ScanResult KvStore::scan(int db, uint64_t cursor, const std::string& pattern, int count) {
    ScanResult result;
    if(count <= 0) {
        count = 10;
    }
    ScopedAllShardsLock lock(m_shards, false);
    const int64_t now = nowMs();

    std::vector<std::string> all;
    for(int i = 0; i < kShardCount; ++i) {
        auto dit = m_shards[i].m_dbs.find(db);
        if(dit != m_shards[i].m_dbs.end()) {
            purgeExpiredDb(dit->second, now);
            collectKeys(dit->second, now, pattern, all);
        }
    }
    std::sort(all.begin(), all.end());
    if(cursor >= all.size()) {
        return result;
    }
    const size_t start = (size_t)cursor;
    const size_t end = std::min(start + (size_t)count, all.size());
    result.keys.assign(all.begin() + start, all.begin() + end);
    result.cursor = (end >= all.size()) ? 0 : (uint64_t)end;
    return result;
}

int64_t KvStore::purgeExpiredActive(int max_scan_per_db) {
    if(max_scan_per_db <= 0) {
        return 0;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    int64_t removed = 0;
    for(int i = 0; i < kShardCount; ++i) {
        for(auto& pair : m_shards[i].m_dbs) {
            DbMap& dbmap = pair.second;
            int scanned = 0;
            for(auto it = dbmap.begin(); it != dbmap.end() && scanned < max_scan_per_db;) {
                ++scanned;
                if(isExpired(it->second, now)) {
                    it = dbmap.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
        }
    }
    return removed;
}

int64_t KvStore::expire(int db, const std::string& key, int64_t seconds) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    const int64_t now = nowMs();
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(seconds <= 0) {
        dbmap.erase(key);
        return 1;
    }
    e->expire_at_ms = now + seconds * 1000;
    return 1;
}

int64_t KvStore::pexpire(int db, const std::string& key, int64_t milliseconds) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    const int64_t now = nowMs();
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(milliseconds <= 0) {
        dbmap.erase(key);
        return 1;
    }
    e->expire_at_ms = now + milliseconds;
    return 1;
}

int64_t KvStore::expireAt(int db, const std::string& key, int64_t unix_sec) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    const int64_t now = nowMs();
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(unix_sec <= 0) {
        dbmap.erase(key);
        return 1;
    }
    e->expire_at_ms = unix_sec * 1000;
    return e->expire_at_ms > now ? 1 : 0;
}

int64_t KvStore::pexpireAt(int db, const std::string& key, int64_t unix_ms) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    const int64_t now = nowMs();
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(unix_ms <= 0) {
        dbmap.erase(key);
        return 1;
    }
    e->expire_at_ms = unix_ms;
    return e->expire_at_ms > now ? 1 : 0;
}

int64_t KvStore::ttl(int db, const std::string& key) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    auto dit = shard.m_dbs.find(db);
    if(dit == shard.m_dbs.end()) {
        return -2;
    }
    const int64_t now = nowMs();
    Entry* e = findEntry(dit->second, key, now);
    if(!e) {
        return -2;
    }
    if(e->expire_at_ms == 0) {
        return -1;
    }
    const int64_t remain_ms = e->expire_at_ms - now;
    if(remain_ms <= 0) {
        dit->second.erase(key);
        return -2;
    }
    return (remain_ms + 999) / 1000;
}

int64_t KvStore::pttl(int db, const std::string& key) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    auto dit = shard.m_dbs.find(db);
    if(dit == shard.m_dbs.end()) {
        return -2;
    }
    const int64_t now = nowMs();
    Entry* e = findEntry(dit->second, key, now);
    if(!e) {
        return -2;
    }
    if(e->expire_at_ms == 0) {
        return -1;
    }
    const int64_t remain_ms = e->expire_at_ms - now;
    if(remain_ms <= 0) {
        dit->second.erase(key);
        return -2;
    }
    return remain_ms;
}

bool KvStore::incrBy(int db, const std::string& key, int64_t delta, int64_t& out, std::string& err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        Entry& ne = dbmap[key];
        ne.type = ObjType::String;
        ne.hash.clear();
        ne.list.clear();
        ne.set.clear();
        ne.zset.clear();
        ne.value = std::to_string(delta);
        ne.expire_at_ms = 0;
        out = delta;
        shard.bumpWatchKey(db, key);
        return true;
    }
    if(e->type != ObjType::String) {
        err = kWrongType;
        return false;
    }
    int64_t cur = 0;
    if(!parseIntegerValue(e->value, cur)) {
        err = "ERR value is not an integer or out of range";
        return false;
    }
    // Reuse findEntry pointer — no second hash lookup needed
    const int64_t next = cur + delta;
    e->value = std::to_string(next);
    out = next;
    shard.bumpWatchKey(db, key);
    return true;
}

int64_t KvStore::hset(int db, const std::string& key, const std::string& field,
                      const std::string& value, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(existing && existing->type != ObjType::Hash) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    if(existing) {
        // Reuse pointer — skip second hash lookup
        const bool is_new = existing->hash.find(field) == existing->hash.end();
        existing->hash[field] = value;
        shard.bumpWatchKey(db, key);
        return is_new ? 1 : 0;
    }
    Entry& e = dbmap[key];
    e.type = ObjType::Hash;
    e.value.clear();
    e.list.clear();
    e.set.clear();
    e.zset.clear();
    e.expire_at_ms = 0;
    e.hash[field] = value;
    shard.bumpWatchKey(db, key);
    return 1;
}

bool KvStore::hget(int db, const std::string& key, const std::string& field,
                   std::string& value_out, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::Hash) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    auto it = e->hash.find(field);
    if(it == e->hash.end()) {
        found = false;
        return true;
    }
    found = true;
    value_out = it->second;
    return true;
}

bool KvStore::hdel(int db, const std::string& key, const std::string& field, int64_t& removed,
                   std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        removed = 0;
        return true;
    }
    if(e->type != ObjType::Hash) {
        removed = 0;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    removed = e->hash.erase(field);
    if(e->hash.empty()) {
        dbmap.erase(key);
    }
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::hexists(int db, const std::string& key, const std::string& field, bool& exists_out,
                      std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        exists_out = false;
        return true;
    }
    if(e->type != ObjType::Hash) {
        exists_out = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    exists_out = e->hash.find(field) != e->hash.end();
    return true;
}

int64_t KvStore::hlen(int db, const std::string& key, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return 0;
    }
    if(e->type != ObjType::Hash) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return 0;
    }
    found = true;
    return (int64_t)e->hash.size();
}

bool KvStore::hgetall(int db, const std::string& key,
                      std::vector<std::pair<std::string, std::string>>& out,
                      bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        out.clear();
        return true;
    }
    if(e->type != ObjType::Hash) {
        found = false;
        out.clear();
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    out.clear();
    out.reserve(e->hash.size());
    for(const auto& pair : e->hash) {
        out.push_back(pair);
    }
    std::sort(out.begin(), out.end(),
        [](const std::pair<std::string, std::string>& a,
           const std::pair<std::string, std::string>& b) {
            return a.first < b.first;
        });
    return true;
}

bool KvStore::hmset(int db, const std::string& key,
                    const std::vector<std::pair<std::string, std::string>>& fields,
                    std::string* err) {
    if(fields.empty()) {
        return true;
    }
    for(const auto& field : fields) {
        if(hset(db, key, field.first, field.second, err) < 0) {
            return false;
        }
    }
    return true;
}

bool KvStore::hmget(int db, const std::string& key, const std::vector<std::string>& fields,
                    std::vector<std::pair<bool, std::string>>& values_out, std::string* err) {
    values_out.clear();
    values_out.reserve(fields.size());
    for(const auto& field : fields) {
        std::string value;
        bool found = false;
        if(!hget(db, key, field, value, found, err)) {
            values_out.clear();
            return false;
        }
        values_out.emplace_back(found, value);
    }
    return true;
}

bool KvStore::hkeys(int db, const std::string& key, std::vector<std::string>& out, bool& found,
                    std::string* err) {
    std::vector<std::pair<std::string, std::string>> all;
    if(!hgetall(db, key, all, found, err)) {
        out.clear();
        return false;
    }
    out.clear();
    if(!found) {
        return true;
    }
    out.reserve(all.size());
    for(const auto& pair : all) {
        out.push_back(pair.first);
    }
    return true;
}

bool KvStore::hvals(int db, const std::string& key, std::vector<std::string>& out, bool& found,
                    std::string* err) {
    std::vector<std::pair<std::string, std::string>> all;
    if(!hgetall(db, key, all, found, err)) {
        out.clear();
        return false;
    }
    out.clear();
    if(!found) {
        return true;
    }
    out.reserve(all.size());
    for(const auto& pair : all) {
        out.push_back(pair.second);
    }
    return true;
}

namespace {

void normalizeListRange(int64_t len, int64_t& start, int64_t& stop) {
    if(len <= 0) {
        start = 0;
        stop = -1;
        return;
    }
    if(start < 0) {
        start = len + start;
    }
    if(stop < 0) {
        stop = len + stop;
    }
    if(start < 0) {
        start = 0;
    }
    if(stop >= len) {
        stop = len - 1;
    }
}

} // namespace

int64_t KvStore::listPush(int db, const std::string& key, const std::vector<std::string>& values,
                          bool left, std::string* err) {
    if(values.empty()) {
        return 0;
    }
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(existing && existing->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    if(existing) {
        // Reuse pointer — skip second hash lookup
        if(left) {
            for(const auto& v : values) {
                existing->list.push_front(v);
            }
        } else {
            for(const auto& v : values) {
                existing->list.push_back(v);
            }
        }
        shard.bumpWatchKey(db, key);
        return (int64_t)existing->list.size();
    }
    Entry& e = dbmap[key];
    e.type = ObjType::List;
    e.value.clear();
    e.hash.clear();
    e.set.clear();
    e.zset.clear();
    e.list.clear();
    e.expire_at_ms = 0;
    if(left) {
        for(const auto& v : values) {
            e.list.push_front(v);
        }
    } else {
        for(const auto& v : values) {
            e.list.push_back(v);
        }
    }
    shard.bumpWatchKey(db, key);
    return (int64_t)e.list.size();
}

bool KvStore::listPop(int db, const std::string& key, bool left, std::string& value_out,
                      bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::List) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(e->list.empty()) {
        found = false;
        return true;
    }
    if(left) {
        value_out = e->list.front();
        e->list.pop_front();
    } else {
        value_out = e->list.back();
        e->list.pop_back();
    }
    if(e->list.empty()) {
        dbmap.erase(key);
    }
    shard.bumpWatchKey(db, key);
    found = true;
    return true;
}

int64_t KvStore::listLen(int db, const std::string& key, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return 0;
    }
    if(e->type != ObjType::List) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return 0;
    }
    found = true;
    return (int64_t)e->list.size();
}

bool KvStore::listRange(int db, const std::string& key, int64_t start, int64_t stop,
                        std::vector<std::string>& out, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        out.clear();
        return true;
    }
    if(e->type != ObjType::List) {
        found = false;
        out.clear();
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    out.clear();
    const int64_t len = (int64_t)e->list.size();
    normalizeListRange(len, start, stop);
    if(start > stop || len <= 0) {
        return true;
    }
    for(int64_t i = start; i <= stop; ++i) {
        out.push_back(e->list[(size_t)i]);
    }
    return true;
}

int64_t KvStore::setAdd(int db, const std::string& key, const std::vector<std::string>& members,
                        std::string* err) {
    if(members.empty()) {
        return 0;
    }
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(existing && existing->type != ObjType::Set) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    if(existing) {
        // Reuse pointer — skip second hash lookup
        int64_t added = 0;
        for(const auto& m : members) {
            if(existing->set.insert(m).second) {
                ++added;
            }
        }
        shard.bumpWatchKey(db, key);
        return added;
    }
    Entry& e = dbmap[key];
    e.type = ObjType::Set;
    e.value.clear();
    e.hash.clear();
    e.list.clear();
    e.set.clear();
    e.zset.clear();
    e.expire_at_ms = 0;
    int64_t added = 0;
    for(const auto& m : members) {
        if(e.set.insert(m).second) {
            ++added;
        }
    }
    shard.bumpWatchKey(db, key);
    return added;
}

int64_t KvStore::setRem(int db, const std::string& key, const std::vector<std::string>& members,
                        std::string* err) {
    if(members.empty()) {
        return 0;
    }
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(e->type != ObjType::Set) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    int64_t removed = 0;
    for(const auto& m : members) {
        if(e->set.erase(m)) {
            ++removed;
        }
    }
    if(e->set.empty()) {
        dbmap.erase(key);
    }
    shard.bumpWatchKey(db, key);
    return removed;
}

bool KvStore::setMembers(int db, const std::string& key, std::vector<std::string>& out,
                         bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        out.clear();
        return true;
    }
    if(e->type != ObjType::Set) {
        found = false;
        out.clear();
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    out.assign(e->set.begin(), e->set.end());
    return true;
}

int64_t KvStore::setCard(int db, const std::string& key, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return 0;
    }
    if(e->type != ObjType::Set) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    found = true;
    return (int64_t)e->set.size();
}

bool KvStore::setIsMember(int db, const std::string& key, const std::string& member,
                          bool& is_member, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        is_member = false;
        return true;
    }
    if(e->type != ObjType::Set) {
        found = false;
        is_member = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    is_member = e->set.find(member) != e->set.end();
    return true;
}

int64_t KvStore::zadd(int db, const std::string& key,
                      const std::vector<std::pair<double, std::string>>& members,
                      std::string* err) {
    if(members.empty()) {
        return 0;
    }
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(existing && existing->type != ObjType::ZSet) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    if(existing) {
        // Reuse pointer — skip second hash lookup
        int64_t added = 0;
        for(const auto& pair : members) {
            if(existing->zset.find(pair.second) == existing->zset.end()) {
                ++added;
            }
            existing->zset[pair.second] = pair.first;
        }
        shard.bumpWatchKey(db, key);
        return added;
    }
    Entry& e = dbmap[key];
    e.type = ObjType::ZSet;
    e.value.clear();
    e.hash.clear();
    e.list.clear();
    e.set.clear();
    e.zset.clear();
    e.expire_at_ms = 0;
    int64_t added = 0;
    for(const auto& pair : members) {
        if(e.zset.find(pair.second) == e.zset.end()) {
            ++added;
        }
        e.zset[pair.second] = pair.first;
    }
    shard.bumpWatchKey(db, key);
    return added;
}

int64_t KvStore::zrem(int db, const std::string& key, const std::vector<std::string>& members,
                      std::string* err) {
    if(members.empty()) {
        return 0;
    }
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(e->type != ObjType::ZSet) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    int64_t removed = 0;
    for(const auto& m : members) {
        if(e->zset.erase(m)) {
            ++removed;
        }
    }
    if(e->zset.empty()) {
        dbmap.erase(key);
    }
    shard.bumpWatchKey(db, key);
    return removed;
}

bool KvStore::zscore(int db, const std::string& key, const std::string& member, double& score_out,
                     bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::ZSet) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    auto it = e->zset.find(member);
    if(it == e->zset.end()) {
        found = false;
        return true;
    }
    found = true;
    score_out = it->second;
    return true;
}

int64_t KvStore::zcard(int db, const std::string& key, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return 0;
    }
    if(e->type != ObjType::ZSet) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    found = true;
    return (int64_t)e->zset.size();
}

bool KvStore::zrange(int db, const std::string& key, int64_t start, int64_t stop, bool reverse,
                     std::vector<std::pair<std::string, double>>& out, bool& found,
                     std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        out.clear();
        return true;
    }
    if(e->type != ObjType::ZSet) {
        found = false;
        out.clear();
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    out.clear();
    std::vector<std::pair<double, std::string>> ranked;
    ranked.reserve(e->zset.size());
    for(const auto& kv : e->zset) {
        ranked.emplace_back(kv.second, kv.first);
    }
    if(reverse) {
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if(a.first != b.first) {
                return a.first > b.first;
            }
            return a.second > b.second;
        });
    } else {
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if(a.first != b.first) {
                return a.first < b.first;
            }
            return a.second < b.second;
        });
    }
    const int64_t len = (int64_t)ranked.size();
    normalizeListRange(len, start, stop);
    if(start > stop || len <= 0) {
        return true;
    }
    for(int64_t i = start; i <= stop; ++i) {
        out.emplace_back(ranked[(size_t)i].second, ranked[(size_t)i].first);
    }
    return true;
}

bool KvStore::zincrby(int db, const std::string& key, const std::string& member, double increment,
                      double& score_out, bool& member_existed, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* existing = findEntry(dbmap, key, now);
    if(existing && existing->type != ObjType::ZSet) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(existing) {
        // Reuse pointer — skip second hash lookup
        member_existed = existing->zset.find(member) != existing->zset.end();
        const double next = (member_existed ? existing->zset[member] : 0.0) + increment;
        existing->zset[member] = next;
        score_out = next;
        shard.bumpWatchKey(db, key);
        return true;
    }
    Entry& e = dbmap[key];
    e.type = ObjType::ZSet;
    e.value.clear();
    e.hash.clear();
    e.list.clear();
    e.set.clear();
    e.zset.clear();
    e.expire_at_ms = 0;
    e.zset[member] = increment;
    score_out = increment;
    member_existed = false;
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::zrank(int db, const std::string& key, const std::string& member, bool reverse,
                    int64_t& rank_out, bool& key_found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        key_found = false;
        rank_out = -1;
        return true;
    }
    if(e->type != ObjType::ZSet) {
        key_found = false;
        rank_out = -1;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    key_found = true;
    if(e->zset.find(member) == e->zset.end()) {
        rank_out = -1;
        return true;
    }
    std::vector<std::pair<double, std::string>> ranked;
    ranked.reserve(e->zset.size());
    for(const auto& kv : e->zset) {
        ranked.emplace_back(kv.second, kv.first);
    }
    if(reverse) {
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if(a.first != b.first) {
                return a.first > b.first;
            }
            return a.second > b.second;
        });
    } else {
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if(a.first != b.first) {
                return a.first < b.first;
            }
            return a.second < b.second;
        });
    }
    for(size_t i = 0; i < ranked.size(); ++i) {
        if(ranked[i].second == member) {
            rank_out = (int64_t)i;
            return true;
        }
    }
    rank_out = -1;
    return true;
}

void KvStore::touchEntry(Entry& e, int64_t now) {
    e.lru_ms = now;
}

int64_t KvStore::estimateEntryBytes(const std::string& key, const Entry& e) const {
    int64_t n = (int64_t)key.size() + 64;
    switch(e.type) {
        case ObjType::Hash:
            for(const auto& f : e.hash) {
                n += (int64_t)f.first.size() + (int64_t)f.second.size() + 16;
            }
            break;
        case ObjType::List:
            for(const auto& v : e.list) {
                n += (int64_t)v.size() + 8;
            }
            break;
        case ObjType::Set:
            for(const auto& m : e.set) {
                n += (int64_t)m.size() + 8;
            }
            break;
        case ObjType::ZSet:
            for(const auto& z : e.zset) {
                n += (int64_t)z.first.size() + 16;
            }
            break;
        case ObjType::Stream:
            for(const auto& entry : e.stream) {
                n += (int64_t)entry.id.size() + 16;
                for(const auto& f : entry.fields) {
                    n += (int64_t)f.first.size() + (int64_t)f.second.size() + 16;
                }
            }
            break;
        default:
            n += (int64_t)e.value.size();
            break;
    }
    return n;
}

void KvStore::evictIfNeededLocked(int64_t now) {
    if(m_maxmemory <= 0 || m_maxmemory_policy == "noeviction") {
        return;
    }
    // Acquire all-shard lock — caller must NOT hold any individual shard lock
    ScopedAllShardsLock lock(m_shards, true);
    const bool volatile_only = (m_maxmemory_policy == "volatile-lru"
                                || m_maxmemory_policy == "volatile-random"
                                || m_maxmemory_policy == "volatile-ttl");
    const bool use_random = (m_maxmemory_policy == "allkeys-random"
                             || m_maxmemory_policy == "volatile-random");
    const bool use_ttl = (m_maxmemory_policy == "volatile-ttl");
    const bool use_lru = (m_maxmemory_policy == "allkeys-lru"
                          || m_maxmemory_policy == "volatile-lru");

    while(true) {
        int64_t used = 0;
        for(int i = 0; i < kShardCount; ++i) {
            for(const auto& db_pair : m_shards[i].m_dbs) {
                for(const auto& kv : db_pair.second) {
                    if(!isExpired(kv.second, now)) {
                        used += estimateEntryBytes(kv.first, kv.second);
                    }
                }
            }
        }
        if(used <= m_maxmemory) {
            break;
        }
        struct Candidate {
            int db = 0;
            int shard = 0;
            std::string key;
            int64_t lru = 0;
            int64_t ttl_left = 0;
        };
        std::vector<Candidate> sample;
        sample.reserve(16);
        for(int i = 0; i < kShardCount && sample.size() < 16; ++i) {
            for(const auto& db_pair : m_shards[i].m_dbs) {
                for(const auto& kv : db_pair.second) {
                    if(isExpired(kv.second, now)) {
                        continue;
                    }
                    if(volatile_only && kv.second.expire_at_ms <= 0) {
                        continue;
                    }
                    int64_t ttl_left = kv.second.expire_at_ms > 0
                        ? kv.second.expire_at_ms - now : INT64_MAX;
                    sample.push_back(Candidate{db_pair.first, i, kv.first, kv.second.lru_ms, ttl_left});
                    if(sample.size() >= 16) {
                        break;
                    }
                }
                if(sample.size() >= 16) {
                    break;
                }
            }
        }
        if(sample.empty()) {
            break;
        }
        Candidate victim = sample.front();
        if(use_random) {
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_int_distribution<size_t> dist(0, sample.size() - 1);
            victim = sample[dist(rng)];
        } else if(use_ttl) {
            std::sort(sample.begin(), sample.end(),
                      [](const Candidate& a, const Candidate& b) { return a.ttl_left < b.ttl_left; });
            victim = sample.front();
        } else if(use_lru) {
            std::sort(sample.begin(), sample.end(),
                      [](const Candidate& a, const Candidate& b) { return a.lru < b.lru; });
            victim = sample.front();
        }
        Shard& vshard = m_shards[victim.shard];
        auto dit = vshard.m_dbs.find(victim.db);
        if(dit == vshard.m_dbs.end()) {
            break;
        }
        dit->second.erase(victim.key);
        vshard.bumpWatchKey(victim.db, victim.key);
    }
}

void KvStore::setMaxMemory(int64_t bytes) {
    zero::RWMutex::WriteLock lock(m_shards[0].m_mutex);
    m_maxmemory = bytes > 0 ? bytes : 0;
}

int64_t KvStore::maxMemory() const {
    zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
    return m_maxmemory;
}

void KvStore::setMaxMemoryPolicy(const std::string& policy) {
    zero::RWMutex::WriteLock lock(m_shards[0].m_mutex);
    m_maxmemory_policy = policy.empty() ? "noeviction" : policy;
}

const std::string& KvStore::maxMemoryPolicy() const {
    zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
    return m_maxmemory_policy;
}

int64_t KvStore::usedMemoryApprox() const {
    int64_t used = 0;
    const int64_t now = nowMs();
    for(int i = 0; i < kShardCount; ++i) {
        for(const auto& db_pair : m_shards[i].m_dbs) {
            for(const auto& kv : db_pair.second) {
                if(!isExpired(kv.second, now)) {
                    used += estimateEntryBytes(kv.first, kv.second);
                }
            }
        }
    }
    return used;
}

bool KvStore::append(int db, const std::string& key, const std::string& value, int64_t& len_out,
                     std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        Entry& ne = dbmap[key];
        ne.type = ObjType::String;
        ne.value = value;
        touchEntry(ne, now);
        len_out = (int64_t)ne.value.size();
        shard.bumpWatchKey(db, key);
        return true;
    }
    if(e->type != ObjType::String) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    e->value.append(value);
    touchEntry(*e, now);
    len_out = (int64_t)e->value.size();
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::strlen(int db, const std::string& key, int64_t& len_out, bool& found,
                     std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        len_out = 0;
        return true;
    }
    if(e->type != ObjType::String) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    touchEntry(*e, now);
    found = true;
    len_out = (int64_t)e->value.size();
    return true;
}

int KvStore::renameKey(int db, const std::string& src, const std::string& dst, bool nx,
                       std::string* err) {
    if(src == dst) {
        return 1;
    }
    // src and dst may hash to different shards — lock all shards
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();

    Shard& src_shard = m_shards[shardIdx(src)];
    auto src_dit = src_shard.m_dbs.find(db);
    if(src_dit == src_shard.m_dbs.end()) {
        if(err) *err = "ERR no such key";
        return -2;
    }
    Entry* se = findEntry(src_dit->second, src, now);
    if(!se) {
        if(err) *err = "ERR no such key";
        return -2;
    }

    Shard& dst_shard = m_shards[shardIdx(dst)];
    Entry* de = nullptr;
    auto dst_dit = dst_shard.m_dbs.find(db);
    if(dst_dit != dst_shard.m_dbs.end()) {
        de = findEntry(dst_dit->second, dst, now);
    }
    if(de) {
        if(nx) return 0;
        dst_shard.m_dbs[db].erase(dst);
        dst_shard.bumpWatchKey(db, dst);
    }

    Entry moved = std::move(*se);
    src_dit->second.erase(src);
    dst_shard.m_dbs[db][dst] = std::move(moved);
    touchEntry(dst_shard.m_dbs[db][dst], now);
    src_shard.bumpWatchKey(db, src);
    if(&src_shard != &dst_shard) {
        dst_shard.bumpWatchKey(db, dst);
    }
    return 1;
}

bool KvStore::hincrby(int db, const std::string& key, const std::string& field, int64_t delta,
                       int64_t& out, std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        Entry& ne = dbmap[key];
        ne.type = ObjType::Hash;
        ne.hash[field] = std::to_string(delta);
        touchEntry(ne, now);
        out = delta;
        shard.bumpWatchKey(db, key);
        return true;
    }
    if(e->type != ObjType::Hash) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    int64_t cur = 0;
    auto it = e->hash.find(field);
    if(it != e->hash.end() && !parseIntegerValue(it->second, cur)) {
        if(err) {
            *err = "ERR hash value is not an integer";
        }
        return false;
    }
    const int64_t next = cur + delta;
    e->hash[field] = std::to_string(next);
    touchEntry(*e, now);
    out = next;
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::listIndex(int db, const std::string& key, int64_t index, std::string& value_out,
                        bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    touchEntry(*e, now);
    const int64_t len = (int64_t)e->list.size();
    int64_t idx = index;
    if(idx < 0) {
        idx = len + idx;
    }
    if(idx < 0 || idx >= len) {
        found = false;
        return true;
    }
    found = true;
    value_out = e->list[(size_t)idx];
    return true;
}

int64_t KvStore::listTrim(int db, const std::string& key, int64_t start, int64_t stop,
                          std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(e->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    const int64_t len = (int64_t)e->list.size();
    int64_t s = start;
    int64_t t = stop;
    normalizeListRange(len, s, t);
    if(len <= 0 || s > t) {
        dbmap.erase(key);
        shard.bumpWatchKey(db, key);
        return 1;
    }
    std::deque<std::string> kept;
    for(int64_t i = s; i <= t; ++i) {
        kept.push_back(e->list[(size_t)i]);
    }
    e->list.swap(kept);
    if(e->list.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    return 1;
}

bool KvStore::listSet(int db, const std::string& key, int64_t index, const std::string& value,
                      std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        if(err) {
            *err = "ERR no such key";
        }
        return false;
    }
    if(e->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    const int64_t len = (int64_t)e->list.size();
    int64_t idx = index;
    if(idx < 0) {
        idx = len + idx;
    }
    if(idx < 0 || idx >= len) {
        if(err) {
            *err = "ERR index out of range";
        }
        return false;
    }
    e->list[(size_t)idx] = value;
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

int64_t KvStore::listRem(int db, const std::string& key, int64_t count,
                          const std::string& value, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(e->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    int64_t removed = 0;
    if(count == 0) {
        for(auto it = e->list.begin(); it != e->list.end();) {
            if(*it == value) {
                it = e->list.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
    } else if(count > 0) {
        for(auto it = e->list.begin(); it != e->list.end() && removed < count;) {
            if(*it == value) {
                it = e->list.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
    } else {
        const int64_t need = -count;
        for(int64_t i = (int64_t)e->list.size() - 1; i >= 0 && removed < need; --i) {
            if(e->list[(size_t)i] == value) {
                e->list.erase(e->list.begin() + i);
                ++removed;
            }
        }
    }
    if(e->list.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    return removed;
}

bool KvStore::listRPopLPush(int db, const std::string& src, const std::string& dst,
                            std::string& value_out, bool& found, std::string* err) {
    if(!listPop(db, src, false, value_out, found, err) || !found) {
        return true;
    }
    const int64_t n = listPush(db, dst, {value_out}, true, err);
    if(n < 0) {
        return false;
    }
    return true;
}

bool KvStore::setInter(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                       std::string* err) {
    out.clear();
    if(keys.empty()) {
        return true;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    std::unordered_set<std::string> acc;
    bool first = true;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            out.clear();
            return true;
        }
        Entry* e = findEntry(dit->second, key, now);
        if(!e) {
            out.clear();
            return true;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return false;
        }
        touchEntry(*e, now);
        if(first) {
            acc = e->set;
            first = false;
        } else {
            std::unordered_set<std::string> next;
            for(const auto& m : acc) {
                if(e->set.count(m)) {
                    next.insert(m);
                }
            }
            acc.swap(next);
        }
    }
    out.assign(acc.begin(), acc.end());
    std::sort(out.begin(), out.end());
    return true;
}

bool KvStore::setUnion(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                       std::string* err) {
    out.clear();
    if(keys.empty()) {
        return true;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    std::unordered_set<std::string> acc;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            continue;
        }
        Entry* e = findEntry(dit->second, key, now);
        if(!e) {
            continue;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return false;
        }
        touchEntry(*e, now);
        acc.insert(e->set.begin(), e->set.end());
    }
    out.assign(acc.begin(), acc.end());
    std::sort(out.begin(), out.end());
    return true;
}

bool KvStore::setDiff(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                      std::string* err) {
    out.clear();
    if(keys.empty()) {
        return true;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    Shard& first_shard = m_shards[shardIdx(keys[0])];
    auto first_dit = first_shard.m_dbs.find(db);
    if(first_dit == first_shard.m_dbs.end()) {
        return true;
    }
    Entry* first = findEntry(first_dit->second, keys[0], now);
    if(!first) {
        return true;
    }
    if(first->type != ObjType::Set) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    std::unordered_set<std::string> acc = first->set;
    for(size_t i = 1; i < keys.size(); ++i) {
        Shard& shard = m_shards[shardIdx(keys[i])];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            continue;
        }
        Entry* e = findEntry(dit->second, keys[i], now);
        if(!e) {
            continue;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return false;
        }
        for(const auto& m : e->set) {
            acc.erase(m);
        }
    }
    out.assign(acc.begin(), acc.end());
    std::sort(out.begin(), out.end());
    return true;
}

namespace {

struct StreamIdParts {
    uint64_t ms = 0;
    uint64_t seq = 0;
    bool auto_id = false;
};

bool parseStreamIdToken(const std::string& s, StreamIdParts& out) {
    out = StreamIdParts{};
    if(s == "*") {
        out.auto_id = true;
        return true;
    }
    const size_t dash = s.find('-');
    if(dash == std::string::npos) {
        return false;
    }
    char* end = nullptr;
    unsigned long long ms = std::strtoull(s.substr(0, dash).c_str(), &end, 10);
    if(end != s.c_str() + dash) {
        return false;
    }
    unsigned long long seq = std::strtoull(s.substr(dash + 1).c_str(), &end, 10);
    if(*end != '\0') {
        return false;
    }
    out.ms = ms;
    out.seq = seq;
    return true;
}

int compareStreamId(const StreamIdParts& a, const StreamIdParts& b) {
    if(a.ms != b.ms) {
        return a.ms < b.ms ? -1 : 1;
    }
    if(a.seq != b.seq) {
        return a.seq < b.seq ? -1 : 1;
    }
    return 0;
}

std::string formatStreamId(uint64_t ms, uint64_t seq) {
    return std::to_string(ms) + "-" + std::to_string(seq);
}

} // namespace

bool KvStore::xadd(int db, const std::string& key, const std::string& id_in,
                   const std::vector<std::pair<std::string, std::string>>& fields,
                   std::string& id_out, std::string* err) {
    if(fields.empty()) {
        if(err) {
            *err = "ERR wrong number of arguments for XADD";
        }
        return false;
    }
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        Entry& ne = dbmap[key];
        ne.type = ObjType::Stream;
        e = &ne;
    } else if(e->type != ObjType::Stream) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    StreamIdParts req;
    if(!parseStreamIdToken(id_in, req)) {
        if(err) {
            *err = "ERR invalid stream ID";
        }
        return false;
    }
    if(req.auto_id) {
        const uint64_t ms = (uint64_t)now;
        if(ms > e->stream_last_ms) {
            e->stream_last_ms = ms;
            e->stream_last_seq = 0;
        } else {
            ++e->stream_last_seq;
        }
        id_out = formatStreamId(e->stream_last_ms, e->stream_last_seq);
    } else {
        StreamIdParts tail{};
        if(!e->stream.empty()) {
            parseStreamIdToken(e->stream.back().id, tail);
        }
        if(!e->stream.empty() && compareStreamId(req, tail) <= 0) {
            if(err) {
                *err = "ERR The ID specified in XADD is equal or smaller than the target stream top item";
            }
            return false;
        }
        id_out = formatStreamId(req.ms, req.seq);
        e->stream_last_ms = req.ms;
        e->stream_last_seq = req.seq;
    }
    StreamEntry entry;
    entry.id = id_out;
    for(const auto& f : fields) {
        entry.fields[f.first] = f.second;
    }
    e->stream.push_back(std::move(entry));
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

int64_t KvStore::xlen(int db, const std::string& key, bool& found, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return 0;
    }
    if(e->type != ObjType::Stream) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    touchEntry(*e, now);
    found = true;
    return (int64_t)e->stream.size();
}

bool KvStore::xrange(int db, const std::string& key, const std::string& start,
                     const std::string& end, int64_t count, bool reverse,
                     std::vector<StreamEntry>& out, bool& found, std::string* err) {
    out.clear();
    Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::Stream) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    touchEntry(*e, now);
    found = true;
    StreamIdParts start_id, end_id;
    if(!parseStreamIdToken(start == "-" ? "0-0" : start, start_id)) {
        return true;
    }
    if(!parseStreamIdToken(end == "+" ? "18446744073709551615-18446744073709551615" : end, end_id)) {
        return true;
    }
    if(reverse) {
        for(auto it = e->stream.rbegin(); it != e->stream.rend(); ++it) {
            StreamIdParts cur;
            parseStreamIdToken(it->id, cur);
            if(compareStreamId(cur, start_id) > 0 || compareStreamId(cur, end_id) < 0) {
                continue;
            }
            out.push_back(*it);
            if(count > 0 && (int64_t)out.size() >= count) {
                break;
            }
        }
    } else {
        for(const auto& item : e->stream) {
            StreamIdParts cur;
            parseStreamIdToken(item.id, cur);
            if(compareStreamId(cur, start_id) < 0 || compareStreamId(cur, end_id) > 0) {
                continue;
            }
            out.push_back(item);
            if(count > 0 && (int64_t)out.size() >= count) {
                break;
            }
        }
    }
    return true;
}

int64_t KvStore::xdel(int db, const std::string& key, const std::vector<std::string>& ids,
                      std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return 0;
    }
    if(e->type != ObjType::Stream) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    int64_t removed = 0;
    for(const auto& id : ids) {
        for(auto it = e->stream.begin(); it != e->stream.end(); ++it) {
            if(it->id == id) {
                e->stream.erase(it);
                ++removed;
                break;
            }
        }
    }
    if(e->stream.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    return removed;
}

bool KvStore::xread(int db, const std::vector<std::string>& keys, const std::vector<std::string>& ids,
                    int64_t count, std::vector<std::pair<std::string, std::vector<StreamEntry>>>& out,
                    std::string* err) {
    out.clear();
    if(keys.size() != ids.size() || keys.empty()) {
        if(err) {
            *err = "ERR syntax error";
        }
        return false;
    }
    ScopedAllShardsLock lock(m_shards, false);
    const int64_t now = nowMs();
    for(size_t i = 0; i < keys.size(); ++i) {
        Shard& shard = m_shards[shardIdx(keys[i])];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            continue;
        }
        Entry* e = findEntry(dit->second, keys[i], now);
        if(!e || e->type != ObjType::Stream) {
            continue;
        }
        touchEntry(*e, now);
        std::vector<StreamEntry> picked;
        for(const auto& item : e->stream) {
            if(ids[i] != "0" && item.id <= ids[i]) {
                continue;
            }
            picked.push_back(item);
            if(count > 0 && (int64_t)picked.size() >= count) {
                break;
            }
        }
        if(!picked.empty()) {
            out.emplace_back(keys[i], std::move(picked));
        }
    }
    return true;
}

bool KvStore::getex(int db, const std::string& key, int64_t ex_seconds, int64_t px_milliseconds,
                    bool persist, std::string& value_out, bool& found, std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::String) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    value_out = e->value;
    found = true;
    if(persist) {
        e->expire_at_ms = 0;
    } else if(ex_seconds >= 0) {
        e->expire_at_ms = now + ex_seconds * 1000;
    } else if(px_milliseconds >= 0) {
        e->expire_at_ms = now + px_milliseconds;
    }
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::spop(int db, const std::string& key, std::string& member_out, bool& found,
                   std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::Set) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(e->set.empty()) {
        found = false;
        return true;
    }
    auto it = e->set.begin();
    member_out = *it;
    e->set.erase(it);
    if(e->set.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    found = true;
    return true;
}

bool KvStore::zpopmin(int db, const std::string& key, std::string& member_out, double& score_out,
                      bool& found, std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::ZSet) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(e->zset.empty()) {
        found = false;
        return true;
    }
    auto best = e->zset.begin();
    for(auto it = e->zset.begin(); it != e->zset.end(); ++it) {
        if(it->second < best->second
           || (it->second == best->second && it->first < best->first)) {
            best = it;
        }
    }
    member_out = best->first;
    score_out = best->second;
    e->zset.erase(best);
    if(e->zset.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    found = true;
    return true;
}

bool KvStore::zpopmax(int db, const std::string& key, std::string& member_out, double& score_out,
                      bool& found, std::string* err) {
    const int64_t now = nowMs();
    evictIfNeededLocked(now);
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        found = false;
        return true;
    }
    if(e->type != ObjType::ZSet) {
        found = false;
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(e->zset.empty()) {
        found = false;
        return true;
    }
    auto best = e->zset.begin();
    for(auto it = e->zset.begin(); it != e->zset.end(); ++it) {
        if(it->second > best->second
           || (it->second == best->second && it->first > best->first)) {
            best = it;
        }
    }
    member_out = best->first;
    score_out = best->second;
    e->zset.erase(best);
    if(e->zset.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(*e, now);
    }
    shard.bumpWatchKey(db, key);
    found = true;
    return true;
}

int KvStore::hsetnx(int db, const std::string& key, const std::string& field,
                    const std::string& value, std::string* err) {
    bool exists = false;
    {
        Shard& shard = getShard(key);
        zero::RWMutex::ReadLock lock(shard.m_mutex);
        const int64_t now = nowMs();
        const auto dit = shard.m_dbs.find(db);
        if(dit != shard.m_dbs.end()) {
            const auto it = dit->second.find(key);
            if(it != dit->second.end() && !isExpired(it->second, now)
               && it->second.type == ObjType::Hash) {
                exists = it->second.hash.count(field) > 0;
            }
        }
    }
    if(exists) {
        return 0;
    }
    const int64_t rc = hset(db, key, field, value, err);
    return rc < 0 ? -1 : 1;
}

bool KvStore::xgroupCreate(int db, const std::string& key, const std::string& group,
                           const std::string& id, bool mkstream, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        if(!mkstream) {
            if(err) {
                *err = "ERR no such key";
            }
            return false;
        }
        Entry& ne = dbmap[key];
        ne.type = ObjType::Stream;
        e = &ne;
    } else if(e->type != ObjType::Stream) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    if(e->stream_groups.count(group)) {
        if(err) {
            *err = "BUSYGROUP Consumer Group name already exists";
        }
        return false;
    }
    Entry::StreamGroupState gs;
    if(id == "$") {
        gs.last_id = e->stream.empty() ? "0-0" : e->stream.back().id;
    } else {
        gs.last_id = id;
    }
    e->stream_groups[group] = std::move(gs);
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::xgroupDestroy(int db, const std::string& key, const std::string& group,
                            std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e || e->type != ObjType::Stream) {
        if(err) {
            *err = "ERR no such key";
        }
        return false;
    }
    if(e->stream_groups.erase(group) == 0) {
        if(err) {
            *err = "ERR no such group";
        }
        return false;
    }
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::xreadGroup(int db, const std::string& group, const std::string& consumer,
                         const std::vector<std::string>& keys, const std::vector<std::string>& ids,
                         int64_t count,
                         std::vector<std::pair<std::string, std::vector<StreamEntry>>>& out,
                         std::string* err) {
    out.clear();
    if(keys.size() != ids.size() || keys.empty()) {
        if(err) {
            *err = "ERR syntax error";
        }
        return false;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    for(size_t i = 0; i < keys.size(); ++i) {
        Shard& shard = m_shards[shardIdx(keys[i])];
        auto dit = shard.m_dbs.find(db);
        if(dit == shard.m_dbs.end()) {
            continue;
        }
        Entry* e = findEntry(dit->second, keys[i], now);
        if(!e || e->type != ObjType::Stream) {
            continue;
        }
        auto git = e->stream_groups.find(group);
        if(git == e->stream_groups.end()) {
            if(err) {
                *err = "NOGROUP No such consumer group";
            }
            return false;
        }
        Entry::StreamGroupState& gs = git->second;
        Entry::StreamConsumerState& cs = gs.consumers[consumer];
        std::string start_id = cs.last_id.empty() ? gs.last_id : cs.last_id;
        if(ids[i] == ">") {
            // new messages only
        } else if(!ids[i].empty()) {
            start_id = ids[i];
        }
        std::vector<StreamEntry> picked;
        for(const auto& item : e->stream) {
            if(item.id <= start_id) {
                continue;
            }
            picked.push_back(item);
            cs.last_id = item.id;
            cs.pending.push_back(item.id);
            Entry::StreamPendingMeta meta;
            meta.delivery_ms = now;
            meta.delivery_count = 1;
            cs.pending_meta[item.id] = meta;
            if(count > 0 && (int64_t)picked.size() >= count) {
                break;
            }
        }
        touchEntry(*e, now);
        if(!picked.empty()) {
            out.emplace_back(keys[i], std::move(picked));
        }
    }
    return true;
}

int64_t KvStore::xack(int db, const std::string& key, const std::string& group,
                      const std::vector<std::string>& ids, std::string* err) {
    Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e || e->type != ObjType::Stream) {
        if(err) {
            *err = "ERR no such key";
        }
        return -1;
    }
    auto git = e->stream_groups.find(group);
    if(git == e->stream_groups.end()) {
        if(err) {
            *err = "NOGROUP No such consumer group";
        }
        return -1;
    }
    int64_t acked = 0;
    for(auto& cp : git->second.consumers) {
        for(const auto& id : ids) {
            auto& pending = cp.second.pending;
            for(auto it = pending.begin(); it != pending.end(); ++it) {
                if(*it == id) {
                    pending.erase(it);
                    cp.second.pending_meta.erase(id);
                    ++acked;
                    break;
                }
            }
        }
    }
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return acked;
}

void KvStore::setRdbPath(const std::string& path) {
    zero::RWMutex::WriteLock lock(m_shards[0].m_mutex);
    m_rdbPath = path;
}

const std::string& KvStore::getRdbPath() const {
    zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
    return m_rdbPath;
}

bool KvStore::saveRdb(std::string* err) {
    std::string path;
    {
        zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
        path = m_rdbPath;
    }
    if(!Rdb::save(*this, path, err)) {
        return false;
    }
    zero::RWMutex::WriteLock lock(m_shards[0].m_mutex);
    m_lastSaveSec = nowMs() / 1000;
    return true;
}

bool KvStore::loadRdb(std::string* err) {
    std::string path;
    {
        zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
        path = m_rdbPath;
    }
    std::ifstream probe(path);
    if(!probe.good()) {
        return true;
    }
    return Rdb::load(*this, path, err);
}

int64_t KvStore::lastSaveUnixSec() const {
    zero::RWMutex::ReadLock lock(m_shards[0].m_mutex);
    return m_lastSaveSec;
}

} // namespace kv
} // namespace zero

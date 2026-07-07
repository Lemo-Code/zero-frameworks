/**
 * @file kv_store_extras.cc
 * @brief KV 存储扩展实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/store/kv_store.h"
#include "zero/kv/persistence/rdb.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace zero {
namespace kv {

namespace {

struct ScoreBound {
    double val = 0;
    bool exclusive = false;
    bool neg_inf = false;
    bool pos_inf = false;
};

bool parseScoreBound(const std::string& raw, ScoreBound& out) {
    out = ScoreBound();
    std::string s = raw;
    if(s.empty()) {
        return false;
    }
    if(s == "-inf" || s == "-INF") {
        out.neg_inf = true;
        return true;
    }
    if(s == "+inf" || s == "inf" || s == "+INF" || s == "INF") {
        out.pos_inf = true;
        return true;
    }
    if(s[0] == '(') {
        out.exclusive = true;
        s = s.substr(1);
    }
    char* end = nullptr;
    out.val = std::strtod(s.c_str(), &end);
    return end != s.c_str() && !std::isnan(out.val);
}

bool scoreGeMin(double score, const ScoreBound& min) {
    if(min.neg_inf) {
        return true;
    }
    if(min.exclusive) {
        return score > min.val;
    }
    return score >= min.val;
}

bool scoreLeMax(double score, const ScoreBound& max) {
    if(max.pos_inf) {
        return true;
    }
    if(max.exclusive) {
        return score < max.val;
    }
    return score <= max.val;
}

bool scoreInRange(double score, const ScoreBound& min, const ScoreBound& max) {
    return scoreGeMin(score, min) && scoreLeMax(score, max);
}

std::vector<std::pair<double, std::string>> rankedZset(const std::map<std::string, double>& zset,
                                                        bool reverse) {
    std::vector<std::pair<double, std::string>> ranked;
    ranked.reserve(zset.size());
    for(const auto& kv : zset) {
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
    return ranked;
}

CollectionScanResult scanFlatItems(const std::vector<std::string>& all, uint64_t cursor,
                                   int count) {
    CollectionScanResult result;
    if(count <= 0) {
        count = 10;
    }
    if(cursor >= all.size()) {
        return result;
    }
    const size_t start = (size_t)cursor;
    const size_t end = std::min(start + (size_t)count, all.size());
    result.items.assign(all.begin() + start, all.begin() + end);
    result.cursor = (end >= all.size()) ? 0 : (uint64_t)end;
    return result;
}

const KvStore::StreamEntry* findStreamEntry(const std::vector<KvStore::StreamEntry>& stream,
                                            const std::string& id) {
    for(const auto& item : stream) {
        if(item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

// 格式化 double，去掉尾部多余的零（Redis 兼容行为）
static std::string formatDouble(double v) {
    if (std::isinf(v) || std::isnan(v)) return std::to_string(v);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos &&
        s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

} // namespace

bool KvStore::incrByFloat(int db, const std::string& key, double delta, std::string& out,
                          std::string* err) {
Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        Entry& ne = dbmap[key];
        ne.type = ObjType::String;
        ne.value = formatDouble(delta);
        ne.expire_at_ms = 0;
        out = ne.value;
        shard.bumpWatchKey(db, key);
        return true;
    }
    if(e->type != ObjType::String) {
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    char* end = nullptr;
    double cur = std::strtod(e->value.c_str(), &end);
    if(end == e->value.c_str()) {
        if(err) {
            *err = "ERR value is not a valid float";
        }
        return false;
    }
    const double next = cur + delta;
    e->value = formatDouble(next);
    out = e->value;
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

int KvStore::msetnx(int db, const std::vector<std::pair<std::string, std::string>>& pairs,
                    std::string* err) {
    if(pairs.empty()) {
        return 1;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    // Check none exist
    for(const auto& pair : pairs) {
        Shard& shard = m_shards[shardIdx(pair.first)];
        auto dit = shard.m_dbs.find(db);
        if(dit != shard.m_dbs.end() && findEntry(dit->second, pair.first, now)) {
            return 0;
        }
    }
    for(const auto& pair : pairs) {
        Shard& shard = m_shards[shardIdx(pair.first)];
        DbMap& dbmap = shard.m_dbs[db];
        Entry& e = dbmap[pair.first];
        e.type = ObjType::String;
        e.value = pair.second;
        e.hash.clear();
        e.list.clear();
        e.set.clear();
        e.zset.clear();
        e.stream.clear();
        e.stream_groups.clear();
        e.expire_at_ms = 0;
        touchEntry(e, now);
        shard.bumpWatchKey(db, pair.first);
    }
    (void)err;
    return 1;
}

int64_t KvStore::hsetFields(int db, const std::string& key,
                            const std::vector<std::pair<std::string, std::string>>& fields,
                            std::string* err) {
    if(fields.empty()) {
        return 0;
    }
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
    Entry& e = dbmap[key];
    if(!existing) {
        e.type = ObjType::Hash;
        e.value.clear();
        e.list.clear();
        e.set.clear();
        e.zset.clear();
        e.stream.clear();
        e.stream_groups.clear();
        e.expire_at_ms = 0;
    }
    int64_t added = 0;
    for(const auto& field : fields) {
        if(e.hash.find(field.first) == e.hash.end()) {
            ++added;
        }
        e.hash[field.first] = field.second;
    }
    touchEntry(e, now);
    shard.bumpWatchKey(db, key);
    return added;
}

CollectionScanResult KvStore::hscan(int db, const std::string& key, uint64_t cursor,
                                    const std::string& pattern, int count, std::string* err) {
    CollectionScanResult result;
Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return result;
    }
    if(e->type != ObjType::Hash) {
        if(err) {
            *err = kWrongType;
        }
        return result;
    }
    std::vector<std::string> field_names;
    for(const auto& kv : e->hash) {
        if(matchGlob(pattern, kv.first)) {
            field_names.push_back(kv.first);
        }
    }
    std::sort(field_names.begin(), field_names.end());
    std::vector<std::string> flat;
    flat.reserve(field_names.size() * 2);
    for(const auto& name : field_names) {
        flat.push_back(name);
        flat.push_back(e->hash.at(name));
    }
    return scanFlatItems(flat, cursor, count);
}

CollectionScanResult KvStore::sscan(int db, const std::string& key, uint64_t cursor,
                                    const std::string& pattern, int count, std::string* err) {
    CollectionScanResult result;
Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return result;
    }
    if(e->type != ObjType::Set) {
        if(err) {
            *err = kWrongType;
        }
        return result;
    }
    std::vector<std::string> members;
    for(const auto& m : e->set) {
        if(matchGlob(pattern, m)) {
            members.push_back(m);
        }
    }
    std::sort(members.begin(), members.end());
    return scanFlatItems(members, cursor, count);
}

CollectionScanResult KvStore::zscan(int db, const std::string& key, uint64_t cursor,
                                    const std::string& pattern, int count, std::string* err) {
    CollectionScanResult result;
Shard& shard = getShard(key);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return result;
    }
    if(e->type != ObjType::ZSet) {
        if(err) {
            *err = kWrongType;
        }
        return result;
    }
    std::vector<std::string> flat;
    for(const auto& kv : e->zset) {
        if(matchGlob(pattern, kv.first)) {
            flat.push_back(std::to_string(kv.second));
            flat.push_back(kv.first);
        }
    }
    std::sort(flat.begin(), flat.end());
    return scanFlatItems(flat, cursor, count);
}

int64_t KvStore::listInsert(int db, const std::string& key, bool before, const std::string& pivot,
                            const std::string& value, std::string* err) {
Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    const int64_t now = nowMs();
    DbMap& dbmap = shard.m_dbs[db];
    Entry* e = findEntry(dbmap, key, now);
    if(!e) {
        return -2;
    }
    if(e->type != ObjType::List) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    for(auto it = e->list.begin(); it != e->list.end(); ++it) {
        if(*it == pivot) {
            if(before) {
                e->list.insert(it, value);
            } else {
                e->list.insert(std::next(it), value);
            }
            touchEntry(*e, now);
            shard.bumpWatchKey(db, key);
            return (int64_t)e->list.size();
        }
    }
    return -2;
}

int64_t KvStore::setInterStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                               std::string* err) {
    ScopedAllShardsLock lock(m_shards, true);
    if(keys.empty()) {
        Shard& dshard = m_shards[shardIdx(dest)];
        dshard.m_dbs[db].erase(dest);
        dshard.bumpWatchKey(db, dest);
        return 0;
    }
    const int64_t now = nowMs();
    std::unordered_set<std::string> acc;
    bool first = true;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        Entry* e = (dit != shard.m_dbs.end()) ? findEntry(dit->second, key, now) : nullptr;
        if(!e) {
            Shard& dshard = m_shards[shardIdx(dest)];
            dshard.m_dbs[db].erase(dest);
            dshard.bumpWatchKey(db, dest);
            return 0;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return -1;
        }
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
    Shard& dshard = m_shards[shardIdx(dest)];
    if(acc.empty()) {
        dshard.m_dbs[db].erase(dest);
    } else {
        Entry& e = dshard.m_dbs[db][dest];
        e.type = ObjType::Set;
        e.value.clear();
        e.hash.clear();
        e.list.clear();
        e.zset.clear();
        e.stream.clear();
        e.stream_groups.clear();
        e.set = acc;
        e.expire_at_ms = 0;
        touchEntry(e, now);
    }
    dshard.bumpWatchKey(db, dest);
    return (int64_t)acc.size();
}

int64_t KvStore::setUnionStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                              std::string* err) {
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    std::unordered_set<std::string> acc;
    for(const auto& key : keys) {
        Shard& shard = m_shards[shardIdx(key)];
        auto dit = shard.m_dbs.find(db);
        Entry* e = (dit != shard.m_dbs.end()) ? findEntry(dit->second, key, now) : nullptr;
        if(!e) {
            continue;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return -1;
        }
        acc.insert(e->set.begin(), e->set.end());
    }
    Shard& dshard = m_shards[shardIdx(dest)];
    if(acc.empty()) {
        dshard.m_dbs[db].erase(dest);
    } else {
        Entry& e = dshard.m_dbs[db][dest];
        e.type = ObjType::Set;
        e.value.clear();
        e.hash.clear();
        e.list.clear();
        e.zset.clear();
        e.stream.clear();
        e.stream_groups.clear();
        e.set = acc;
        e.expire_at_ms = 0;
        touchEntry(e, now);
    }
    dshard.bumpWatchKey(db, dest);
    return (int64_t)acc.size();
}

int64_t KvStore::setDiffStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                             std::string* err) {
    if(keys.empty()) {
        return 0;
    }
    ScopedAllShardsLock lock(m_shards, true);
    const int64_t now = nowMs();
    Shard& first_shard = m_shards[shardIdx(keys[0])];
    auto first_dit = first_shard.m_dbs.find(db);
    Entry* first = (first_dit != first_shard.m_dbs.end()) ? findEntry(first_dit->second, keys[0], now) : nullptr;
    if(!first) {
        Shard& dshard = m_shards[shardIdx(dest)];
        dshard.m_dbs[db].erase(dest);
        dshard.bumpWatchKey(db, dest);
        return 0;
    }
    if(first->type != ObjType::Set) {
        if(err) {
            *err = kWrongType;
        }
        return -1;
    }
    std::unordered_set<std::string> acc = first->set;
    for(size_t i = 1; i < keys.size(); ++i) {
        Shard& shard = m_shards[shardIdx(keys[i])];
        auto dit = shard.m_dbs.find(db);
        Entry* e = (dit != shard.m_dbs.end()) ? findEntry(dit->second, keys[i], now) : nullptr;
        if(!e) {
            continue;
        }
        if(e->type != ObjType::Set) {
            if(err) {
                *err = kWrongType;
            }
            return -1;
        }
        for(const auto& m : e->set) {
            acc.erase(m);
        }
    }
    Shard& dshard = m_shards[shardIdx(dest)];
    if(acc.empty()) {
        dshard.m_dbs[db].erase(dest);
    } else {
        Entry& e = dshard.m_dbs[db][dest];
        e.type = ObjType::Set;
        e.value.clear();
        e.hash.clear();
        e.list.clear();
        e.zset.clear();
        e.stream.clear();
        e.stream_groups.clear();
        e.set = acc;
        e.expire_at_ms = 0;
        touchEntry(e, now);
    }
    dshard.bumpWatchKey(db, dest);
    return (int64_t)acc.size();
}

int64_t KvStore::zaddOpt(int db, const std::string& key, const ZaddOptions& opt,
                         const std::vector<std::pair<double, std::string>>& members,
                         std::string* err) {
    if(members.empty()) {
        return 0;
    }
    if(opt.nx && opt.xx) {
        if(err) {
            *err = "ERR NX and XX options at the same time are not compatible";
        }
        return -1;
    }
    if(opt.gt && opt.lt) {
        if(err) {
            *err = "ERR GT and LT options at the same time are not compatible";
        }
        return -1;
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
    if(opt.xx && !existing) {
        return 0;
    }
    Entry& e = dbmap[key];
    if(!existing) {
        e.type = ObjType::ZSet;
        e.value.clear();
        e.hash.clear();
        e.list.clear();
        e.set.clear();
        e.zset.clear();
        e.expire_at_ms = 0;
    }
    int64_t added = 0;
    int64_t updated = 0;
    for(const auto& pair : members) {
        auto it = e.zset.find(pair.second);
        const bool exists = it != e.zset.end();
        if(opt.nx && exists) {
            continue;
        }
        if(opt.xx && !exists) {
            continue;
        }
        if(exists) {
            if(opt.gt && pair.first <= it->second) {
                continue;
            }
            if(opt.lt && pair.first >= it->second) {
                continue;
            }
            if(it->second != pair.first) {
                ++updated;
                e.zset[pair.second] = pair.first;
            }
        } else {
            ++added;
            e.zset[pair.second] = pair.first;
        }
    }
    if(e.zset.empty()) {
        dbmap.erase(key);
    } else {
        touchEntry(e, now);
    }
    shard.bumpWatchKey(db, key);
    return opt.ch ? (added + updated) : added;
}

bool KvStore::zrangeByScore(int db, const std::string& key, const std::string& min,
                            const std::string& max, bool reverse, int64_t offset, int64_t count,
                            bool withscores, std::vector<std::pair<std::string, double>>& out,
                            bool& found, std::string* err) {
    ScoreBound bmin, bmax;
    if(!parseScoreBound(min, bmin) || !parseScoreBound(max, bmax)) {
        if(err) {
            *err = "ERR min or max is not a float";
        }
        found = false;
        return false;
    }
Shard& shard = getShard(key);
    zero::RWMutex::WriteLock lock(shard.m_mutex);
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
        if(err) {
            *err = kWrongType;
        }
        return false;
    }
    found = true;
    out.clear();
    auto ranked = rankedZset(e->zset, reverse);
    int64_t skipped = 0;
    for(const auto& item : ranked) {
        if(!scoreInRange(item.first, bmin, bmax)) {
            continue;
        }
        if(skipped < offset) {
            ++skipped;
            continue;
        }
        out.emplace_back(item.second, item.first);
        if(count >= 0 && (int64_t)out.size() >= count) {
            break;
        }
    }
    (void)withscores;
    return true;
}

int64_t KvStore::zcount(int db, const std::string& key, const std::string& min,
                        const std::string& max, bool& found, std::string* err) {
    ScoreBound bmin, bmax;
    if(!parseScoreBound(min, bmin) || !parseScoreBound(max, bmax)) {
        if(err) {
            *err = "ERR min or max is not a float";
        }
        found = false;
        return -1;
    }
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
    int64_t n = 0;
    for(const auto& kv : e->zset) {
        if(scoreInRange(kv.second, bmin, bmax)) {
            ++n;
        }
    }
    return n;
}

int64_t KvStore::xtrim(int db, const std::string& key, int64_t maxlen, bool approximate,
                       std::string* err) {
    (void)approximate;
    if(maxlen < 0) {
        if(err) {
            *err = "ERR syntax error";
        }
        return -1;
    }
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
    const int64_t before = (int64_t)e->stream.size();
    if(before <= maxlen) {
        return 0;
    }
    const int64_t remove = before - maxlen;
    e->stream.erase(e->stream.begin(), e->stream.begin() + remove);
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return remove;
}

bool KvStore::xpendingSummary(int db, const std::string& key, const std::string& group,
                              XPendingSummary& out, std::string* err) {
    out = XPendingSummary();
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
    auto git = e->stream_groups.find(group);
    if(git == e->stream_groups.end()) {
        if(err) {
            *err = "NOGROUP No such consumer group";
        }
        return false;
    }
    std::unordered_map<std::string, int64_t> consumer_counts;
    for(const auto& cp : git->second.consumers) {
        for(const auto& pid : cp.second.pending) {
            ++out.count;
            if(out.min_id.empty() || pid < out.min_id) {
                out.min_id = pid;
            }
            if(out.max_id.empty() || pid > out.max_id) {
                out.max_id = pid;
            }
            ++consumer_counts[cp.first];
        }
    }
    for(const auto& kv : consumer_counts) {
        out.consumers.emplace_back(kv.first, kv.second);
    }
    std::sort(out.consumers.begin(), out.consumers.end());
    (void)now;
    return true;
}

bool KvStore::xpendingRange(int db, const std::string& key, const std::string& group,
                            const std::string& start, const std::string& end, int64_t count,
                            const std::string& consumer_filter, std::vector<XPendingDetail>& out,
                            std::string* err) {
    out.clear();
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
    auto git = e->stream_groups.find(group);
    if(git == e->stream_groups.end()) {
        if(err) {
            *err = "NOGROUP No such consumer group";
        }
        return false;
    }
    for(const auto& cp : git->second.consumers) {
        if(!consumer_filter.empty() && cp.first != consumer_filter) {
            continue;
        }
        for(const auto& pid : cp.second.pending) {
            if(!start.empty() && pid < start) {
                continue;
            }
            if(!end.empty() && end != "+" && pid > end) {
                continue;
            }
            XPendingDetail detail;
            detail.id = pid;
            detail.consumer = cp.first;
            auto mit = cp.second.pending_meta.find(pid);
            if(mit != cp.second.pending_meta.end()) {
                detail.delivery_count = mit->second.delivery_count;
                detail.idle_ms = now - mit->second.delivery_ms;
            }
            out.push_back(detail);
        }
    }
    std::sort(out.begin(), out.end(), [](const XPendingDetail& a, const XPendingDetail& b) {
        return a.id < b.id;
    });
    if(count >= 0 && (int64_t)out.size() > count) {
        out.resize((size_t)count);
    }
    return true;
}

bool KvStore::xclaim(int db, const std::string& key, const std::string& group,
                     const std::string& consumer, int64_t min_idle_ms,
                     const std::vector<std::string>& ids, bool force,
                     std::vector<StreamEntry>& out, std::string* err) {
    out.clear();
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
    auto git = e->stream_groups.find(group);
    if(git == e->stream_groups.end()) {
        if(err) {
            *err = "NOGROUP No such consumer group";
        }
        return false;
    }
    Entry::StreamConsumerState& target = git->second.consumers[consumer];
    for(const auto& id : ids) {
        bool claimed = false;
        for(auto& cp : git->second.consumers) {
            auto& pending = cp.second.pending;
            for(auto it = pending.begin(); it != pending.end(); ++it) {
                if(*it != id) {
                    continue;
                }
                auto mit = cp.second.pending_meta.find(id);
                const int64_t prev_count = mit != cp.second.pending_meta.end()
                                               ? mit->second.delivery_count
                                               : 0;
                const int64_t idle = mit != cp.second.pending_meta.end()
                                         ? now - mit->second.delivery_ms
                                         : 0;
                if(!force && idle < min_idle_ms) {
                    break;
                }
                pending.erase(it);
                cp.second.pending_meta.erase(id);
                target.pending.push_back(id);
                Entry::StreamPendingMeta meta;
                meta.delivery_ms = now;
                meta.delivery_count = prev_count + 1;
                target.pending_meta[id] = meta;
                const StreamEntry* se = findStreamEntry(e->stream, id);
                if(se) {
                    out.push_back(*se);
                }
                claimed = true;
                break;
            }
            if(claimed) {
                break;
            }
        }
    }
    touchEntry(*e, now);
    shard.bumpWatchKey(db, key);
    return true;
}

bool KvStore::dumpKey(int db, const std::string& key, int64_t& ttl_ms, std::string& payload,
                      bool& found, std::string* err) {
    return Rdb::dumpKey(*this, db, key, ttl_ms, payload, found, err);
}

bool KvStore::restoreKey(int db, const std::string& key, int64_t ttl_ms,
                          const std::string& payload, bool replace, std::string* err) {
    return Rdb::restoreKey(*this, db, key, ttl_ms, payload, replace, err);
}

} // namespace kv
} // namespace zero

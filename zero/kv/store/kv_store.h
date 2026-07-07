/**
 * @file kv_store.h
 * @brief 内存 KV 存储（string / hash / list / set / zset，含 TTL）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_KV_STORE_H__
#define __ZERO_KV_KV_STORE_H__

#include "zero/core/concurrency/mutex.h"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zero {
namespace kv {

class Rdb;
class AofLog;

struct ScanResult {
    uint64_t cursor = 0;
    std::vector<std::string> keys;
};

/** HSCAN/SSCAN/ZSCAN 游标扫描结果 */
struct CollectionScanResult {
    uint64_t cursor = 0;
    std::vector<std::string> items;
};

struct ZaddOptions {
    bool nx = false;
    bool xx = false;
    bool gt = false;
    bool lt = false;
    bool ch = false;
};

class KvStore {
public:
    typedef std::shared_ptr<KvStore> ptr;

    static const char* kWrongType;

    bool get(int db, const std::string& key, std::string& value_out, bool& found,
             std::string* err = nullptr);
    bool exists(int db, const std::string& key);
    std::string type(int db, const std::string& key);
    void set(int db, const std::string& key, const std::string& value);
    void setex(int db, const std::string& key, const std::string& value, int64_t ttl_seconds);
    int setOpt(int db, const std::string& key, const std::string& value,
               bool nx, bool xx, int64_t ex_seconds, int64_t px_milliseconds);
    bool del(int db, const std::string& key);
    int64_t delMany(int db, const std::vector<std::string>& keys);
    int64_t existsMany(int db, const std::vector<std::string>& keys);
    int64_t persist(int db, const std::string& key);
    bool getset(int db, const std::string& key, const std::string& value, std::string& old_out,
                bool& found, std::string* err = nullptr);
    bool getdel(int db, const std::string& key, std::string& value_out, bool& found,
                std::string* err = nullptr);
    int64_t dbsize(int db);
    int64_t expireCount(int db);
    void flushdb(int db);
    void flushall();

    std::vector<std::string> keys(int db, const std::string& pattern);
    ScanResult scan(int db, uint64_t cursor, const std::string& pattern, int count);

    int64_t expire(int db, const std::string& key, int64_t seconds);
    int64_t pexpire(int db, const std::string& key, int64_t milliseconds);
    int64_t expireAt(int db, const std::string& key, int64_t unix_sec);
    int64_t pexpireAt(int db, const std::string& key, int64_t unix_ms);
    int64_t ttl(int db, const std::string& key);
    int64_t pttl(int db, const std::string& key);
    int64_t purgeExpiredActive(int max_scan_per_db = 20);

    bool incrBy(int db, const std::string& key, int64_t delta, int64_t& out, std::string& err);
    bool incrByFloat(int db, const std::string& key, double delta, std::string& out,
                     std::string* err = nullptr);
    /** @return 1 全部写入，0 有 key 已存在 */
    int msetnx(int db, const std::vector<std::pair<std::string, std::string>>& pairs,
               std::string* err = nullptr);
    bool append(int db, const std::string& key, const std::string& value, int64_t& len_out,
                std::string* err = nullptr);
    bool strlen(int db, const std::string& key, int64_t& len_out, bool& found,
                std::string* err = nullptr);

    /** @return 1 成功，0 目标已存在，-1 WRONGTYPE */
    int renameKey(int db, const std::string& src, const std::string& dst, bool nx,
                  std::string* err = nullptr);

    /** @return -1 WRONGTYPE，否则 0/1 表示 field 是否新建 */
    int64_t hset(int db, const std::string& key, const std::string& field, const std::string& value,
                 std::string* err = nullptr);
    /** 多 field HSET，返回新建 field 数量 */
    int64_t hsetFields(int db, const std::string& key,
                       const std::vector<std::pair<std::string, std::string>>& fields,
                       std::string* err = nullptr);
    CollectionScanResult hscan(int db, const std::string& key, uint64_t cursor,
                               const std::string& pattern, int count, std::string* err = nullptr);
    bool hget(int db, const std::string& key, const std::string& field, std::string& value_out,
              bool& found, std::string* err = nullptr);
    bool hdel(int db, const std::string& key, const std::string& field, int64_t& removed,
              std::string* err = nullptr);
    bool hexists(int db, const std::string& key, const std::string& field, bool& exists_out,
                 std::string* err = nullptr);
    int64_t hlen(int db, const std::string& key, bool& found, std::string* err = nullptr);
    bool hgetall(int db, const std::string& key, std::vector<std::pair<std::string, std::string>>& out,
                 bool& found, std::string* err = nullptr);
    bool hmset(int db, const std::string& key,
               const std::vector<std::pair<std::string, std::string>>& fields,
               std::string* err = nullptr);
    bool hmget(int db, const std::string& key, const std::vector<std::string>& fields,
               std::vector<std::pair<bool, std::string>>& values_out, std::string* err = nullptr);
    bool hkeys(int db, const std::string& key, std::vector<std::string>& out, bool& found,
               std::string* err = nullptr);
    bool hvals(int db, const std::string& key, std::vector<std::string>& out, bool& found,
               std::string* err = nullptr);
    bool hincrby(int db, const std::string& key, const std::string& field, int64_t delta,
                 int64_t& out, std::string* err = nullptr);

    /** @return 列表新长度，-1 表示 WRONGTYPE */
    int64_t listPush(int db, const std::string& key, const std::vector<std::string>& values,
                     bool left, std::string* err = nullptr);
    bool listPop(int db, const std::string& key, bool left, std::string& value_out, bool& found,
                 std::string* err = nullptr);
    int64_t listLen(int db, const std::string& key, bool& found, std::string* err = nullptr);
    bool listRange(int db, const std::string& key, int64_t start, int64_t stop,
                   std::vector<std::string>& out, bool& found, std::string* err = nullptr);
    bool listIndex(int db, const std::string& key, int64_t index, std::string& value_out,
                   bool& found, std::string* err = nullptr);
    int64_t listTrim(int db, const std::string& key, int64_t start, int64_t stop,
                     std::string* err = nullptr);
    bool listSet(int db, const std::string& key, int64_t index, const std::string& value,
                 std::string* err = nullptr);
    int64_t listRem(int db, const std::string& key, int64_t count, const std::string& value,
                    std::string* err = nullptr);
    bool listRPopLPush(int db, const std::string& src, const std::string& dst,
                       std::string& value_out, bool& found, std::string* err = nullptr);
    /** @return 新长度；-1 WRONGTYPE；-2 pivot 不存在 */
    int64_t listInsert(int db, const std::string& key, bool before, const std::string& pivot,
                       const std::string& value, std::string* err = nullptr);
    CollectionScanResult sscan(int db, const std::string& key, uint64_t cursor,
                               const std::string& pattern, int count, std::string* err = nullptr);

    /** @return 新加入元素个数，-1 表示 WRONGTYPE */
    int64_t setAdd(int db, const std::string& key, const std::vector<std::string>& members,
                   std::string* err = nullptr);
    /** @return 删除元素个数，-1 表示 WRONGTYPE */
    int64_t setRem(int db, const std::string& key, const std::vector<std::string>& members,
                   std::string* err = nullptr);
    bool setMembers(int db, const std::string& key, std::vector<std::string>& out, bool& found,
                    std::string* err = nullptr);
    int64_t setCard(int db, const std::string& key, bool& found, std::string* err = nullptr);
    bool setIsMember(int db, const std::string& key, const std::string& member, bool& is_member,
                     bool& found, std::string* err = nullptr);
    bool setInter(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                  std::string* err = nullptr);
    bool setUnion(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                  std::string* err = nullptr);
    bool setDiff(int db, const std::vector<std::string>& keys, std::vector<std::string>& out,
                 std::string* err = nullptr);
    int64_t setInterStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                          std::string* err = nullptr);
    int64_t setUnionStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                          std::string* err = nullptr);
    int64_t setDiffStore(int db, const std::string& dest, const std::vector<std::string>& keys,
                         std::string* err = nullptr);

    /** @return 新加入成员数，-1 表示 WRONGTYPE */
    int64_t zadd(int db, const std::string& key,
                 const std::vector<std::pair<double, std::string>>& members,
                 std::string* err = nullptr);
    int64_t zaddOpt(int db, const std::string& key, const ZaddOptions& opt,
                    const std::vector<std::pair<double, std::string>>& members,
                    std::string* err = nullptr);
    /** @return 删除成员数，-1 表示 WRONGTYPE */
    int64_t zrem(int db, const std::string& key, const std::vector<std::string>& members,
                 std::string* err = nullptr);
    bool zscore(int db, const std::string& key, const std::string& member, double& score_out,
                bool& found, std::string* err = nullptr);
    int64_t zcard(int db, const std::string& key, bool& found, std::string* err = nullptr);
    bool zrange(int db, const std::string& key, int64_t start, int64_t stop, bool reverse,
                std::vector<std::pair<std::string, double>>& out, bool& found,
                std::string* err = nullptr);
    bool zrangeByScore(int db, const std::string& key, const std::string& min,
                       const std::string& max, bool reverse, int64_t offset, int64_t count,
                       bool withscores, std::vector<std::pair<std::string, double>>& out,
                       bool& found, std::string* err = nullptr);
    int64_t zcount(int db, const std::string& key, const std::string& min, const std::string& max,
                   bool& found, std::string* err = nullptr);
    CollectionScanResult zscan(int db, const std::string& key, uint64_t cursor,
                               const std::string& pattern, int count, std::string* err = nullptr);
    /** @return 新 score；NaN 表示 WRONGTYPE；found=false 表示 key/member 新建 */
    bool zincrby(int db, const std::string& key, const std::string& member, double increment,
                 double& score_out, bool& member_existed, std::string* err = nullptr);
    /** @return rank>=0；rank=-1 表示 member 不存在；WRONGTYPE 时返回 false */
    bool zrank(int db, const std::string& key, const std::string& member, bool reverse,
               int64_t& rank_out, bool& key_found, std::string* err = nullptr);

    struct StreamEntry {
        std::string id;
        std::unordered_map<std::string, std::string> fields;
    };

    /** @return false 表示 ID 冲突或 WRONGTYPE */
    bool xadd(int db, const std::string& key, const std::string& id_in,
              const std::vector<std::pair<std::string, std::string>>& fields,
              std::string& id_out, std::string* err = nullptr);
    int64_t xlen(int db, const std::string& key, bool& found, std::string* err = nullptr);
    bool xrange(int db, const std::string& key, const std::string& start, const std::string& end,
                int64_t count, bool reverse, std::vector<StreamEntry>& out, bool& found,
                std::string* err = nullptr);
    int64_t xdel(int db, const std::string& key, const std::vector<std::string>& ids,
                 std::string* err = nullptr);
    bool xread(int db, const std::vector<std::string>& keys, const std::vector<std::string>& ids,
               int64_t count, std::vector<std::pair<std::string, std::vector<StreamEntry>>>& out,
               std::string* err = nullptr);

    /** GETEX：读 string 并可选设置 TTL */
    bool getex(int db, const std::string& key, int64_t ex_seconds, int64_t px_milliseconds,
               bool persist, std::string& value_out, bool& found, std::string* err = nullptr);
    bool spop(int db, const std::string& key, std::string& member_out, bool& found,
              std::string* err = nullptr);
    bool zpopmin(int db, const std::string& key, std::string& member_out, double& score_out,
                 bool& found, std::string* err = nullptr);
    bool zpopmax(int db, const std::string& key, std::string& member_out, double& score_out,
                 bool& found, std::string* err = nullptr);
    /** @return 1 新建 field，0 已存在，-1 WRONGTYPE */
    int hsetnx(int db, const std::string& key, const std::string& field, const std::string& value,
               std::string* err = nullptr);

    /** XGROUP CREATE [MKSTREAM] */
    bool xgroupCreate(int db, const std::string& key, const std::string& group,
                      const std::string& id, bool mkstream, std::string* err = nullptr);
    bool xgroupDestroy(int db, const std::string& key, const std::string& group,
                       std::string* err = nullptr);
    bool xreadGroup(int db, const std::string& group, const std::string& consumer,
                    const std::vector<std::string>& keys, const std::vector<std::string>& ids,
                    int64_t count,
                    std::vector<std::pair<std::string, std::vector<StreamEntry>>>& out,
                    std::string* err = nullptr);
    int64_t xack(int db, const std::string& key, const std::string& group,
                 const std::vector<std::string>& ids, std::string* err = nullptr);
    int64_t xtrim(int db, const std::string& key, int64_t maxlen, bool approximate,
                  std::string* err = nullptr);
    struct XPendingSummary {
        int64_t count = 0;
        std::string min_id;
        std::string max_id;
        std::vector<std::pair<std::string, int64_t>> consumers;
    };
    struct XPendingDetail {
        std::string id;
        std::string consumer;
        int64_t idle_ms = 0;
        int64_t delivery_count = 0;
    };
    bool xpendingSummary(int db, const std::string& key, const std::string& group,
                         XPendingSummary& out, std::string* err = nullptr);
    bool xpendingRange(int db, const std::string& key, const std::string& group,
                       const std::string& start, const std::string& end, int64_t count,
                       const std::string& consumer_filter, std::vector<XPendingDetail>& out,
                       std::string* err = nullptr);
    bool xclaim(int db, const std::string& key, const std::string& group,
                const std::string& consumer, int64_t min_idle_ms,
                const std::vector<std::string>& ids, bool force,
                std::vector<StreamEntry>& out, std::string* err = nullptr);

    bool dumpKey(int db, const std::string& key, int64_t& ttl_ms, std::string& payload,
                 bool& found, std::string* err = nullptr);
    bool restoreKey(int db, const std::string& key, int64_t ttl_ms, const std::string& payload,
                    bool replace, std::string* err = nullptr);

    void setMaxMemory(int64_t bytes);
    int64_t maxMemory() const;
    void setMaxMemoryPolicy(const std::string& policy);
    const std::string& maxMemoryPolicy() const;
    int64_t usedMemoryApprox() const;

    void setRdbPath(const std::string& path);
    const std::string& getRdbPath() const;
    bool saveRdb(std::string* err = nullptr);
    bool loadRdb(std::string* err = nullptr);
    int64_t lastSaveUnixSec() const;

    /** WATCH 版本号：key 变更或 flushdb 后 token 变化 */
    uint64_t watchToken(int db, const std::string& key) const;

    static int64_t nowMs();

private:
    friend class Rdb;
    friend class AofLog;
    friend class LuaEngine;

    enum class ObjType { String, Hash, List, Set, ZSet, Stream };

    struct Entry {
        ObjType type = ObjType::String;
        std::string value;
        std::unordered_map<std::string, std::string> hash;
        std::deque<std::string> list;
        std::unordered_set<std::string> set;
        std::map<std::string, double> zset;
        std::vector<StreamEntry> stream;
        uint64_t stream_last_ms = 0;
        uint64_t stream_last_seq = 0;
        struct StreamPendingMeta {
            int64_t delivery_ms = 0;
            int64_t delivery_count = 0;
        };
        struct StreamConsumerState {
            std::string last_id;
            std::deque<std::string> pending;
            std::unordered_map<std::string, StreamPendingMeta> pending_meta;
        };
        struct StreamGroupState {
            std::string last_id;
            std::unordered_map<std::string, StreamConsumerState> consumers;
        };
        std::unordered_map<std::string, StreamGroupState> stream_groups;
        int64_t lru_ms = 0;
        int64_t expire_at_ms = 0;
    };

    using DbMap = std::unordered_map<std::string, Entry>;

    static const int kShardCount = 16;
    static const int kMaxDb = 16;

    // Shard with padding to prevent false sharing between adjacent shards (Phase 5)
    // alignas(64) is avoided because C++14 new doesn't support over-aligned types.
    // Instead, use a static_assert + manual padding to approximate cache-line size.
    struct Shard {
        mutable zero::RWMutex m_mutex;
        std::unordered_map<int, DbMap> m_dbs;
        std::unordered_map<std::string, uint64_t> m_watch_versions[kMaxDb];
        std::unordered_map<int, uint64_t> m_db_watch_epoch;

        mutable std::atomic<bool> m_hasWatches{false};  // fast-path: skip when no watchers

        void bumpWatchKey(int db, const std::string& key) {
            if (m_hasWatches.load(std::memory_order_relaxed)) {
                m_watch_versions[db][key]++;
            }
        }
        void bumpWatchDb(int db) {
            m_db_watch_epoch[db]++;
        }
        uint64_t watchToken(int db, const std::string& key) const {
            uint64_t epoch = 0;
            auto eit = m_db_watch_epoch.find(db);
            if(eit != m_db_watch_epoch.end()) {
                epoch = eit->second;
            }
            uint64_t ver = 0;
            auto vit = m_watch_versions[db].find(key);
            if(vit != m_watch_versions[db].end()) {
                ver = vit->second;
            }
            return (epoch << 32) ^ ver;
        }
    };

    static size_t shardIdx(const std::string& key) {
        return std::hash<std::string>{}(key) % kShardCount;
    }

    // Lock all shards for global operations (keys, scan, flushall, etc.)
    class ScopedAllShardsLock {
    public:
        explicit ScopedAllShardsLock(Shard* shards, bool write);
        // const overload for read-only access (m_mutex is mutable)
        explicit ScopedAllShardsLock(const Shard* shards, bool write);
        ~ScopedAllShardsLock();
    private:
        Shard* m_shards;
        bool m_write;
    };

    // 手动锁定/解锁全部 shard（供 Lua 全局原子性等场景使用）
    // 调用者必须保证 lockAllShards / unlockAllShards 成对调用
    void lockAllShards(bool write);
    void unlockAllShards();

    Shard& getShard(const std::string& key) { return m_shards[shardIdx(key)]; }
    const Shard& getShard(const std::string& key) const { return m_shards[shardIdx(key)]; }
    static Shard& getShardByKey(Shard* shards, const std::string& key) { return shards[shardIdx(key)]; }
    static const Shard& getShardByKey(const Shard* shards, const std::string& key) { return shards[shardIdx(key)]; }
    static size_t getShardIdx(const std::string& key) { return shardIdx(key); }

    static bool parseIntegerValue(const std::string& s, int64_t& out);
    static bool matchGlob(const std::string& pattern, const std::string& text);

    void bumpWatchKey(int db, const std::string& key);
    void bumpWatchDb(int db);
    void touchEntry(Entry& e, int64_t now);
    int64_t estimateEntryBytes(const std::string& key, const Entry& e) const;
    void evictIfNeededLocked(int64_t now);

    bool isExpired(const Entry& e, int64_t now) const;
    void purgeExpiredDb(DbMap& db, int64_t now) const;
    Entry* findEntry(DbMap& db, const std::string& key, int64_t now);
    void collectKeys(const DbMap& db, int64_t now, const std::string& pattern,
                     std::vector<std::string>& out) const;

    Shard m_shards[kShardCount];
    std::string m_rdbPath = "./dump.rdb";
    int64_t m_lastSaveSec = 0;
    int64_t m_maxmemory = 0;
    std::string m_maxmemory_policy = "allkeys-lru";
};

} // namespace kv
} // namespace zero

#endif

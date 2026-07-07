/**
 * @file rdb.cc
 * @brief RDB 持久化实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rdb.h"
#include "zero/util/json_util.h"

#include <json/json.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace zero {
namespace kv {

namespace {

const int kRdbVersionJson = 1;
const char kMagicV2[] = "SYRDB2\0";
const uint32_t kBinaryVersion = 2;

void appendBytes(std::string& out, const void* data, size_t n) {
    out.append((const char*)data, n);
}

void appendU8(std::string& out, uint8_t v) {
    appendBytes(out, &v, 1);
}

void appendU32(std::string& out, uint32_t v) {
    appendBytes(out, &v, 4);
}

void appendU64(std::string& out, uint64_t v) {
    appendBytes(out, &v, 8);
}

void appendI64(std::string& out, int64_t v) {
    appendBytes(out, &v, 8);
}

void appendStr(std::string& out, const std::string& s) {
    appendU32(out, (uint32_t)s.size());
    out.append(s);
}

template<typename T>
bool readPod(const char*& p, const char* end, T& v) {
    if((size_t)(end - p) < sizeof(T)) {
        return false;
    }
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
}

bool readStr(const char*& p, const char* end, std::string& s) {
    uint32_t len = 0;
    if(!readPod(p, end, len)) {
        return false;
    }
    if((size_t)(end - p) < len) {
        return false;
    }
    s.assign(p, len);
    p += len;
    return true;
}

bool isBinaryV2(const std::string& data) {
    return data.size() >= 7 && data.compare(0, 6, "SYRDB2") == 0;
}

bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if(!ofs) {
            if(err) {
                *err = "ERR failed opening RDB temp file";
            }
            return false;
        }
        ofs.write(data.data(), (std::streamsize)data.size());
        if(!ofs.good()) {
            if(err) {
                *err = "ERR failed writing RDB temp file";
            }
            return false;
        }
    }
    if(::rename(tmp.c_str(), path.c_str()) != 0) {
        if(err) {
            *err = "ERR failed renaming RDB file";
        }
        return false;
    }
    return true;
}

bool readFile(const std::string& path, std::string& data, std::string* err) {
    std::ifstream ifs(path, std::ios::binary);
    if(!ifs) {
        if(err) {
            *err = "ERR RDB file not found";
        }
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    data = ss.str();
    if(data.empty()) {
        if(err) {
            *err = "ERR empty RDB file";
        }
        return false;
    }
    return true;
}

} // namespace

void Rdb::writeStreamEntry(const KvStore::Entry& e, std::string& out) {
    appendU64(out, e.stream_last_ms);
    appendU64(out, e.stream_last_seq);
    appendU32(out, (uint32_t)e.stream.size());
    for(const auto& se : e.stream) {
        appendStr(out, se.id);
        appendU32(out, (uint32_t)se.fields.size());
        for(const auto& f : se.fields) {
            appendStr(out, f.first);
            appendStr(out, f.second);
        }
    }
    appendU32(out, (uint32_t)e.stream_groups.size());
    for(const auto& gp : e.stream_groups) {
        appendStr(out, gp.first);
        appendStr(out, gp.second.last_id);
        appendU32(out, (uint32_t)gp.second.consumers.size());
        for(const auto& cp : gp.second.consumers) {
            appendStr(out, cp.first);
            appendStr(out, cp.second.last_id);
            appendU32(out, (uint32_t)cp.second.pending.size());
            for(const auto& pid : cp.second.pending) {
                appendStr(out, pid);
                auto mit = cp.second.pending_meta.find(pid);
                int64_t delivery_ms = mit != cp.second.pending_meta.end() ? mit->second.delivery_ms : 0;
                int64_t delivery_count = mit != cp.second.pending_meta.end() ? mit->second.delivery_count : 1;
                appendI64(out, delivery_ms);
                appendI64(out, delivery_count);
            }
        }
    }
}

bool Rdb::readStreamEntry(const char*& p, const char* end, KvStore::Entry& e) {
    if(!readPod(p, end, e.stream_last_ms) || !readPod(p, end, e.stream_last_seq)) {
        return false;
    }
    uint32_t entry_count = 0;
    if(!readPod(p, end, entry_count)) {
        return false;
    }
    e.stream.clear();
    e.stream.reserve(entry_count);
    for(uint32_t i = 0; i < entry_count; ++i) {
        KvStore::StreamEntry se;
        if(!readStr(p, end, se.id)) {
            return false;
        }
        uint32_t field_count = 0;
        if(!readPod(p, end, field_count)) {
            return false;
        }
        for(uint32_t j = 0; j < field_count; ++j) {
            std::string k, v;
            if(!readStr(p, end, k) || !readStr(p, end, v)) {
                return false;
            }
            se.fields[k] = std::move(v);
        }
        e.stream.push_back(std::move(se));
    }
    uint32_t group_count = 0;
    if(!readPod(p, end, group_count)) {
        return false;
    }
    for(uint32_t i = 0; i < group_count; ++i) {
        std::string gname;
        KvStore::Entry::StreamGroupState gs;
        if(!readStr(p, end, gname) || !readStr(p, end, gs.last_id)) {
            return false;
        }
        uint32_t consumer_count = 0;
        if(!readPod(p, end, consumer_count)) {
            return false;
        }
        for(uint32_t j = 0; j < consumer_count; ++j) {
            std::string cname;
            KvStore::Entry::StreamConsumerState cs;
            if(!readStr(p, end, cname) || !readStr(p, end, cs.last_id)) {
                return false;
            }
            uint32_t pending_count = 0;
            if(!readPod(p, end, pending_count)) {
                return false;
            }
            for(uint32_t k = 0; k < pending_count; ++k) {
                std::string pid;
                if(!readStr(p, end, pid)) {
                    return false;
                }
                cs.pending.push_back(std::move(pid));
                int64_t delivery_ms = 0;
                int64_t delivery_count = 1;
                if(!readPod(p, end, delivery_ms) || !readPod(p, end, delivery_count)) {
                    return false;
                }
                KvStore::Entry::StreamPendingMeta meta;
                meta.delivery_ms = delivery_ms;
                meta.delivery_count = delivery_count;
                cs.pending_meta[cs.pending.back()] = meta;
            }
            gs.consumers[cname] = std::move(cs);
        }
        e.stream_groups[gname] = std::move(gs);
    }
    return true;
}

bool Rdb::dumpBinaryV2(const KvStore& store, std::string& data, std::string* err) {
    data.clear();
    appendBytes(data, kMagicV2, 8);
    appendU32(data, kBinaryVersion);
    appendU64(data, (uint64_t)KvStore::nowMs());

    std::vector<std::pair<int, std::vector<std::pair<std::string, KvStore::Entry>>>> snapshot;
    {
        KvStore::ScopedAllShardsLock lock(store.m_shards, false);
        const int64_t now = KvStore::nowMs();
        for(int i = 0; i < KvStore::kShardCount; ++i) {
            for(const auto& db_pair : store.m_shards[i].m_dbs) {
                int db = db_pair.first;
                // Find or create snapshot slot for this db
                bool found = false;
                for(auto& snap : snapshot) {
                    if(snap.first == db) {
                        for(const auto& kv : db_pair.second) {
                            if(store.isExpired(kv.second, now)) continue;
                            snap.second.emplace_back(kv.first, kv.second);
                        }
                        found = true;
                        break;
                    }
                }
                if(!found) {
                    std::vector<std::pair<std::string, KvStore::Entry>> entries;
                    for(const auto& kv : db_pair.second) {
                        if(store.isExpired(kv.second, now)) continue;
                        entries.emplace_back(kv.first, kv.second);
                    }
                    if(!entries.empty()) {
                        snapshot.emplace_back(db, std::move(entries));
                    }
                }
            }
        }
    }

    appendU32(data, (uint32_t)snapshot.size());
    for(const auto& db_pair : snapshot) {
        appendI64(data, db_pair.first);
        appendU32(data, (uint32_t)db_pair.second.size());
        for(const auto& kv : db_pair.second) {
            const KvStore::Entry& e = kv.second;
            appendStr(data, kv.first);
            appendI64(data, e.expire_at_ms);
            appendU8(data, (uint8_t)e.type);
            switch(e.type) {
                case KvStore::ObjType::Hash:
                    appendU32(data, (uint32_t)e.hash.size());
                    for(const auto& f : e.hash) {
                        appendStr(data, f.first);
                        appendStr(data, f.second);
                    }
                    break;
                case KvStore::ObjType::List:
                    appendU32(data, (uint32_t)e.list.size());
                    for(const auto& v : e.list) {
                        appendStr(data, v);
                    }
                    break;
                case KvStore::ObjType::Set:
                    appendU32(data, (uint32_t)e.set.size());
                    for(const auto& m : e.set) {
                        appendStr(data, m);
                    }
                    break;
                case KvStore::ObjType::ZSet:
                    appendU32(data, (uint32_t)e.zset.size());
                    for(const auto& z : e.zset) {
                        appendStr(data, z.first);
                        appendBytes(data, &z.second, sizeof(double));
                    }
                    break;
                case KvStore::ObjType::Stream:
                    writeStreamEntry(e, data);
                    break;
                default:
                    appendStr(data, e.value);
                    break;
            }
        }
    }
    appendU8(data, 0xFF);
    (void)err;
    return true;
}

bool Rdb::loadBinaryV2(KvStore& store, const std::string& data, std::string* err) {
    const char* p = data.data();
    const char* end = p + data.size();
    if((size_t)(end - p) < 8 || std::memcmp(p, kMagicV2, 8) != 0) {
        if(err) {
            *err = "ERR invalid RDB magic";
        }
        return false;
    }
    p += 8;
    uint32_t version = 0;
    if(!readPod(p, end, version) || version != kBinaryVersion) {
        if(err) {
            *err = "ERR unsupported RDB version";
        }
        return false;
    }
    uint64_t saved_at = 0;
    if(!readPod(p, end, saved_at)) {
        if(err) {
            *err = "ERR malformed RDB header";
        }
        return false;
    }
    (void)saved_at;
    uint32_t db_count = 0;
    if(!readPod(p, end, db_count)) {
        if(err) {
            *err = "ERR malformed RDB databases";
        }
        return false;
    }
    const int64_t now = KvStore::nowMs();
    std::unordered_map<int, KvStore::DbMap> rebuilt;
    for(uint32_t di = 0; di < db_count; ++di) {
        int64_t db_id = 0;
        if(!readPod(p, end, db_id)) {
            if(err) {
                *err = "ERR malformed RDB db id";
            }
            return false;
        }
        uint32_t key_count = 0;
        if(!readPod(p, end, key_count)) {
            if(err) {
                *err = "ERR malformed RDB key count";
            }
            return false;
        }
        KvStore::DbMap& dbmap = rebuilt[db_id];
        for(uint32_t ki = 0; ki < key_count; ++ki) {
            std::string key;
            KvStore::Entry e;
            if(!readStr(p, end, key) || !readPod(p, end, e.expire_at_ms)) {
                if(err) {
                    *err = "ERR malformed RDB entry";
                }
                return false;
            }
            if(store.isExpired(e, now)) {
                uint8_t type = 0;
                if(!readPod(p, end, type)) {
                    return false;
                }
                if(!skipEntryPayload(p, end, (KvStore::ObjType)type)) {
                    if(err) {
                        *err = "ERR malformed RDB entry payload";
                    }
                    return false;
                }
                continue;
            }
            uint8_t type = 0;
            if(!readPod(p, end, type)) {
                if(err) {
                    *err = "ERR malformed RDB type";
                }
                return false;
            }
            e.type = (KvStore::ObjType)type;
            switch(e.type) {
                case KvStore::ObjType::Hash: {
                    uint32_t n = 0;
                    if(!readPod(p, end, n)) {
                        return false;
                    }
                    for(uint32_t i = 0; i < n; ++i) {
                        std::string f, v;
                        if(!readStr(p, end, f) || !readStr(p, end, v)) {
                            return false;
                        }
                        e.hash[f] = std::move(v);
                    }
                    break;
                }
                case KvStore::ObjType::List: {
                    uint32_t n = 0;
                    if(!readPod(p, end, n)) {
                        return false;
                    }
                    for(uint32_t i = 0; i < n; ++i) {
                        std::string v;
                        if(!readStr(p, end, v)) {
                            return false;
                        }
                        e.list.push_back(std::move(v));
                    }
                    break;
                }
                case KvStore::ObjType::Set: {
                    uint32_t n = 0;
                    if(!readPod(p, end, n)) {
                        return false;
                    }
                    for(uint32_t i = 0; i < n; ++i) {
                        std::string m;
                        if(!readStr(p, end, m)) {
                            return false;
                        }
                        e.set.insert(std::move(m));
                    }
                    break;
                }
                case KvStore::ObjType::ZSet: {
                    uint32_t n = 0;
                    if(!readPod(p, end, n)) {
                        return false;
                    }
                    for(uint32_t i = 0; i < n; ++i) {
                        std::string m;
                        double score = 0;
                        if(!readStr(p, end, m) || !readPod(p, end, score)) {
                            return false;
                        }
                        e.zset[m] = score;
                    }
                    break;
                }
                case KvStore::ObjType::Stream:
                    if(!readStreamEntry(p, end, e)) {
                        if(err) {
                            *err = "ERR malformed RDB stream";
                        }
                        return false;
                    }
                    break;
                default:
                    if(!readStr(p, end, e.value)) {
                        return false;
                    }
                    e.type = KvStore::ObjType::String;
                    break;
            }
            if(!store.isExpired(e, now)) {
                dbmap[key] = std::move(e);
            }
        }
    }
    uint8_t footer = 0;
    if(!readPod(p, end, footer) || footer != 0xFF) {
        if(err) {
            *err = "ERR malformed RDB footer";
        }
        return false;
    }
    {
        {
            KvStore::ScopedAllShardsLock lock(store.m_shards, true);
            for(int i = 0; i < KvStore::kShardCount; ++i) {
                store.m_shards[i].m_dbs.clear();
                // Clear per-db watch versions
                for(int db = 0; db < KvStore::kMaxDb; ++db) {
                    store.m_shards[i].m_watch_versions[db].clear();
                }
            }
            for(auto& db_pair : rebuilt) {
                int db = db_pair.first;
                for(auto& kv : db_pair.second) {
                    size_t idx = KvStore::shardIdx(kv.first);
                    store.m_shards[idx].m_dbs[db][kv.first] = std::move(kv.second);
                }
            }
        }
    }
    return true;
}

bool Rdb::loadFromJsonV1(KvStore& store, const std::string& data, std::string* err) {
    Json::Value root;
    if(!JsonUtil::FromString(root, data)) {
        if(err) {
            *err = "ERR invalid RDB JSON";
        }
        return false;
    }
    if(JsonUtil::GetInt32(root, "version", 0) != kRdbVersionJson) {
        if(err) {
            *err = "ERR unsupported RDB version";
        }
        return false;
    }

    const int64_t now = KvStore::nowMs();
    std::unordered_map<int, KvStore::DbMap> rebuilt;
    const Json::Value& dbs = root["databases"];
    if(!dbs.isArray()) {
        if(err) {
            *err = "ERR malformed RDB databases";
        }
        return false;
    }

    for(Json::ArrayIndex i = 0; i < dbs.size(); ++i) {
        const Json::Value& db_node = dbs[i];
        const int db_id = JsonUtil::GetInt32(db_node, "id", 0);
        const Json::Value& entries = db_node["entries"];
        if(!entries.isArray()) {
            continue;
        }
        KvStore::DbMap& dbmap = rebuilt[db_id];
        for(Json::ArrayIndex j = 0; j < entries.size(); ++j) {
            const Json::Value& item = entries[j];
            const std::string key = JsonUtil::GetString(item, "key");
            if(key.empty()) {
                continue;
            }
            KvStore::Entry e;
            e.expire_at_ms = JsonUtil::GetInt64(item, "expire_at_ms", 0);
            if(store.isExpired(e, now)) {
                continue;
            }
            const std::string type = JsonUtil::GetString(item, "type", "string");
            if(type == "hash") {
                e.type = KvStore::ObjType::Hash;
                const Json::Value& fields = item["fields"];
                if(fields.isObject()) {
                    for(const auto& name : fields.getMemberNames()) {
                        e.hash[name] = fields[name].asString();
                    }
                }
            } else if(type == "list") {
                e.type = KvStore::ObjType::List;
                const Json::Value& items = item["items"];
                if(items.isArray()) {
                    for(Json::ArrayIndex k = 0; k < items.size(); ++k) {
                        e.list.push_back(items[k].asString());
                    }
                }
            } else if(type == "set") {
                e.type = KvStore::ObjType::Set;
                const Json::Value& members = item["members"];
                if(members.isArray()) {
                    for(Json::ArrayIndex k = 0; k < members.size(); ++k) {
                        e.set.insert(members[k].asString());
                    }
                }
            } else if(type == "zset") {
                e.type = KvStore::ObjType::ZSet;
                const Json::Value& items = item["zitems"];
                if(items.isArray()) {
                    for(Json::ArrayIndex k = 0; k < items.size(); ++k) {
                        const Json::Value& node = items[k];
                        e.zset[node["member"].asString()] = node["score"].asDouble();
                    }
                }
            } else if(type == "stream") {
                e.type = KvStore::ObjType::Stream;
                const Json::Value& stream_entries = item["stream"];
                if(stream_entries.isArray()) {
                    for(Json::ArrayIndex k = 0; k < stream_entries.size(); ++k) {
                        const Json::Value& node = stream_entries[k];
                        KvStore::StreamEntry se;
                        se.id = JsonUtil::GetString(node, "id");
                        const Json::Value& fields = node["fields"];
                        if(fields.isObject()) {
                            for(const auto& name : fields.getMemberNames()) {
                                se.fields[name] = fields[name].asString();
                            }
                        }
                        e.stream.push_back(std::move(se));
                    }
                }
            } else {
                e.type = KvStore::ObjType::String;
                e.value = JsonUtil::GetString(item, "value");
            }
            dbmap[key] = std::move(e);
        }
    }

    {
        {
            KvStore::ScopedAllShardsLock lock(store.m_shards, true);
            for(int i = 0; i < KvStore::kShardCount; ++i) {
                store.m_shards[i].m_dbs.clear();
                // Clear per-db watch versions
                for(int db = 0; db < KvStore::kMaxDb; ++db) {
                    store.m_shards[i].m_watch_versions[db].clear();
                }
            }
            for(auto& db_pair : rebuilt) {
                int db = db_pair.first;
                for(auto& kv : db_pair.second) {
                    size_t idx = KvStore::shardIdx(kv.first);
                    store.m_shards[idx].m_dbs[db][kv.first] = std::move(kv.second);
                }
            }
        }
    }
    return true;
}

bool Rdb::skipEntryPayload(const char*& p, const char* end, KvStore::ObjType type) {
    KvStore::Entry e;
    e.type = type;
    switch(type) {
        case KvStore::ObjType::Hash: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string f, v;
                if(!readStr(p, end, f) || !readStr(p, end, v)) {
                    return false;
                }
            }
            break;
        }
        case KvStore::ObjType::List: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string v;
                if(!readStr(p, end, v)) {
                    return false;
                }
            }
            break;
        }
        case KvStore::ObjType::Set: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string m;
                if(!readStr(p, end, m)) {
                    return false;
                }
            }
            break;
        }
        case KvStore::ObjType::ZSet: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string m;
                double score = 0;
                if(!readStr(p, end, m) || !readPod(p, end, score)) {
                    return false;
                }
            }
            break;
        }
        case KvStore::ObjType::Stream:
            return readStreamEntry(p, end, e);
        default: {
            std::string v;
            return readStr(p, end, v);
        }
    }
    return true;
}

void Rdb::writeEntryPayload(const KvStore::Entry& e, std::string& out) {
    switch(e.type) {
        case KvStore::ObjType::Hash:
            appendU32(out, (uint32_t)e.hash.size());
            for(const auto& f : e.hash) {
                appendStr(out, f.first);
                appendStr(out, f.second);
            }
            break;
        case KvStore::ObjType::List:
            appendU32(out, (uint32_t)e.list.size());
            for(const auto& v : e.list) {
                appendStr(out, v);
            }
            break;
        case KvStore::ObjType::Set:
            appendU32(out, (uint32_t)e.set.size());
            for(const auto& m : e.set) {
                appendStr(out, m);
            }
            break;
        case KvStore::ObjType::ZSet:
            appendU32(out, (uint32_t)e.zset.size());
            for(const auto& z : e.zset) {
                appendStr(out, z.first);
                appendBytes(out, &z.second, sizeof(double));
            }
            break;
        case KvStore::ObjType::Stream:
            writeStreamEntry(e, out);
            break;
        default:
            appendStr(out, e.value);
            break;
    }
}

bool Rdb::readEntryPayload(const char*& p, const char* end, KvStore::Entry& e,
                           KvStore::ObjType type) {
    e.type = type;
    switch(type) {
        case KvStore::ObjType::Hash: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string k, v;
                if(!readStr(p, end, k) || !readStr(p, end, v)) {
                    return false;
                }
                e.hash[k] = std::move(v);
            }
            break;
        }
        case KvStore::ObjType::List: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string v;
                if(!readStr(p, end, v)) {
                    return false;
                }
                e.list.push_back(std::move(v));
            }
            break;
        }
        case KvStore::ObjType::Set: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string m;
                if(!readStr(p, end, m)) {
                    return false;
                }
                e.set.insert(std::move(m));
            }
            break;
        }
        case KvStore::ObjType::ZSet: {
            uint32_t n = 0;
            if(!readPod(p, end, n)) {
                return false;
            }
            for(uint32_t i = 0; i < n; ++i) {
                std::string m;
                double score = 0;
                if(!readStr(p, end, m) || !readPod(p, end, score)) {
                    return false;
                }
                e.zset[m] = score;
            }
            break;
        }
        case KvStore::ObjType::Stream:
            return readStreamEntry(p, end, e);
        default: {
            return readStr(p, end, e.value);
        }
    }
    return true;
}

const char kDumpMagic[] = "SYRDMP\0";

bool Rdb::dumpKey(const KvStore& store, int db, const std::string& key, int64_t& ttl_ms,
                  std::string& payload, bool& found, std::string* err) {
    found = false;
    payload.clear();
    ttl_ms = 0;
    KvStore::Shard& shard = const_cast<KvStore::Shard&>(store.m_shards[KvStore::shardIdx(key)]);
    zero::RWMutex::ReadLock lock(shard.m_mutex);
    const int64_t now = KvStore::nowMs();
    auto dit = shard.m_dbs.find(db);
    if(dit == shard.m_dbs.end()) {
        return true;
    }
    const auto it = dit->second.find(key);
    if(it == dit->second.end() || store.isExpired(it->second, now)) {
        return true;
    }
    found = true;
    const KvStore::Entry& e = it->second;
    if(e.expire_at_ms > now) {
        ttl_ms = e.expire_at_ms - now;
    }
    appendBytes(payload, kDumpMagic, 8);
    appendI64(payload, e.expire_at_ms);
    appendU8(payload, (uint8_t)e.type);
    writeEntryPayload(e, payload);
    (void)err;
    return true;
}

bool Rdb::restoreKey(KvStore& store, int db, const std::string& key, int64_t ttl_ms,
                     const std::string& payload, bool replace, std::string* err) {
    if(payload.size() < 8 || payload.compare(0, 6, "SYRDMP") != 0) {
        if(err) {
            *err = "ERR DUMP payload version or checksum are wrong";
        }
        return false;
    }
    const char* p = payload.data() + 8;
    const char* end = payload.data() + payload.size();
    int64_t expire_at_ms = 0;
    uint8_t type = 0;
    if(!readPod(p, end, expire_at_ms) || !readPod(p, end, type)) {
        if(err) {
            *err = "ERR DUMP payload version or checksum are wrong";
        }
        return false;
    }
    KvStore::Entry e;
    if(!readEntryPayload(p, end, e, (KvStore::ObjType)type)) {
        if(err) {
            *err = "ERR DUMP payload version or checksum are wrong";
        }
        return false;
    }
    const int64_t now = KvStore::nowMs();
    if(ttl_ms > 0) {
        e.expire_at_ms = now + ttl_ms;
    } else if(ttl_ms == 0) {
        e.expire_at_ms = 0;
    } else {
        e.expire_at_ms = now + ttl_ms;
    }
    KvStore::Shard& shard = store.m_shards[KvStore::shardIdx(key)];
    zero::RWMutex::WriteLock lock(shard.m_mutex);
    KvStore::DbMap& dbmap = shard.m_dbs[db];
    auto existing = dbmap.find(key);
    if(existing != dbmap.end() && !store.isExpired(existing->second, now)) {
        if(!replace) {
            if(err) {
                *err = "BUSYKEY Target key name already exists.";
            }
            return false;
        }
    }
    e.lru_ms = now;
    dbmap[key] = std::move(e);
    shard.bumpWatchKey(db, key);
    return true;
}

bool Rdb::save(const KvStore& store, const std::string& path, std::string* err) {
    std::string data;
    if(!dump(store, data, err)) {
        return false;
    }
    return writeFileAtomic(path, data, err);
}

bool Rdb::dump(const KvStore& store, std::string& data, std::string* err) {
    return dumpBinaryV2(store, data, err);
}

bool Rdb::loadFromString(KvStore& store, const std::string& data, std::string* err) {
    if(data.empty()) {
        if(err) {
            *err = "ERR empty RDB payload";
        }
        return false;
    }
    if(isBinaryV2(data)) {
        return loadBinaryV2(store, data, err);
    }
    return loadFromJsonV1(store, data, err);
}

bool Rdb::load(KvStore& store, const std::string& path, std::string* err) {
    std::string data;
    if(!readFile(path, data, err)) {
        return false;
    }
    return loadFromString(store, data, err);
}

} // namespace kv
} // namespace zero

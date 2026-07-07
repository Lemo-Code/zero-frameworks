/**
 * @file aof.cc
 * @brief AOF 持久化实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "aof.h"
#include "zero/kv/resp_reader.h"

#include <algorithm>
#include <fcntl.h>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <vector>

namespace zero {
namespace kv {

namespace {

AofLog::ptr g_aof_config;

std::string upperCmd(const RespValue& request) {
    if(request.type != RespType::Array || request.array.empty()) {
        return "";
    }
    const RespValue& cmd = request.array[0];
    if(cmd.type != RespType::BulkString || cmd.is_null) {
        return "";
    }
    std::string name = cmd.str;
    for(char& c : name) {
        c = (char)std::toupper((unsigned char)c);
    }
    return name;
}

RespValue makeRewriteArray(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

bool writeFileAtomic(const std::string& path, const std::string& data, std::string* err) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if(!ofs) {
            if(err) {
                *err = "ERR failed opening AOF temp file";
            }
            return false;
        }
        ofs.write(data.data(), (std::streamsize)data.size());
        if(!ofs.good()) {
            if(err) {
                *err = "ERR failed writing AOF temp file";
            }
            return false;
        }
    }
    if(::rename(tmp.c_str(), path.c_str()) != 0) {
        if(err) {
            *err = "ERR failed renaming AOF file";
        }
        return false;
    }
    return true;
}

std::string scoreToString(double score) {
    std::ostringstream ss;
    ss << score;
    return ss.str();
}

} // namespace

void AofLog::buildRewriteCommands(const KvStore& store, std::vector<RespValue>& out) {
    KvStore::ScopedAllShardsLock lock(const_cast<KvStore::Shard*>(store.m_shards), false);
    const int64_t now = KvStore::nowMs();
    // Use a set to track which DBs we've already sent SELECT for
    bool db_done[KvStore::kMaxDb] = {};
    for(int i = 0; i < KvStore::kShardCount; ++i) {
        for(const auto& db_pair : store.m_shards[i].m_dbs) {
            int db = db_pair.first;
            if(!db_done[db]) {
                out.push_back(makeRewriteArray({"SELECT", std::to_string(db)}));
                db_done[db] = true;
            }
            for(const auto& kv : db_pair.second) {
            if(store.isExpired(kv.second, now)) {
                continue;
            }
            const std::string& key = kv.first;
            switch(kv.second.type) {
                case KvStore::ObjType::Hash:
                    for(const auto& field : kv.second.hash) {
                        out.push_back(makeRewriteArray({"HSET", key, field.first, field.second}));
                    }
                    break;
                case KvStore::ObjType::List: {
                    RespValue cmd = makeRewriteArray({"RPUSH", key});
                    for(const auto& item : kv.second.list) {
                        cmd.array.push_back(RespEncoder::bulk(item));
                    }
                    if(cmd.array.size() > 2) {
                        out.push_back(cmd);
                    }
                    break;
                }
                case KvStore::ObjType::Set: {
                    RespValue cmd = makeRewriteArray({"SADD", key});
                    for(const auto& member : kv.second.set) {
                        cmd.array.push_back(RespEncoder::bulk(member));
                    }
                    if(cmd.array.size() > 2) {
                        out.push_back(cmd);
                    }
                    break;
                }
                case KvStore::ObjType::ZSet: {
                    RespValue cmd = makeRewriteArray({"ZADD", key});
                    for(const auto& member : kv.second.zset) {
                        cmd.array.push_back(RespEncoder::bulk(scoreToString(member.second)));
                        cmd.array.push_back(RespEncoder::bulk(member.first));
                    }
                    if(cmd.array.size() > 2) {
                        out.push_back(cmd);
                    }
                    break;
                }
                case KvStore::ObjType::Stream: {
                    for(const auto& entry : kv.second.stream) {
                        RespValue cmd = makeRewriteArray({"XADD", key, entry.id});
                        std::vector<std::string> names;
                        names.reserve(entry.fields.size());
                        for(const auto& f : entry.fields) {
                            names.push_back(f.first);
                        }
                        std::sort(names.begin(), names.end());
                        for(const auto& name : names) {
                            cmd.array.push_back(RespEncoder::bulk(name));
                            cmd.array.push_back(RespEncoder::bulk(entry.fields.at(name)));
                        }
                        out.push_back(cmd);
                    }
                    break;
                }
                default:
                    if(kv.second.expire_at_ms > now) {
                        const int64_t ttl_sec = (kv.second.expire_at_ms - now + 999) / 1000;
                        if(ttl_sec > 0) {
                            out.push_back(makeRewriteArray(
                                {"SETEX", key, std::to_string(ttl_sec), kv.second.value}));
                            break;
                        }
                    }
                    out.push_back(makeRewriteArray({"SET", key, kv.second.value}));
                    break;
            }
            if(kv.second.expire_at_ms > now && kv.second.type != KvStore::ObjType::String) {
                const int64_t ttl_sec = (kv.second.expire_at_ms - now + 999) / 1000;
                if(ttl_sec > 0) {
                    out.push_back(makeRewriteArray({"EXPIRE", key, std::to_string(ttl_sec)}));
                }
            }
        }
    }
    }
}

void bindAofForConfig(AofLog::ptr aof) {
    g_aof_config = std::move(aof);
}

AofLog::ptr getAofForConfig() {
    return g_aof_config;
}

bool isMutatingCommand(const std::string& upper_name) {
    static const char* kMutating[] = {
        "SET", "SETEX", "SETNX", "GETSET", "GETDEL", "GETEX", "DEL", "EXPIRE", "PEXPIRE",
        "EXPIREAT", "PEXPIREAT", "PERSIST", "MSET", "MSETNX",
        "INCR", "DECR", "INCRBY", "DECRBY", "INCRBYFLOAT",
        "HSET", "HDEL", "HMSET", "HSETNX", "HINCRBY",
        "LPUSH", "RPUSH", "LPOP", "RPOP", "LTRIM", "LSET", "LREM", "RPOPLPUSH", "LINSERT",
        "SADD", "SREM", "SPOP", "SINTERSTORE", "SUNIONSTORE", "SDIFFSTORE",
        "ZADD", "ZREM", "ZINCRBY", "ZPOPMIN", "ZPOPMAX",
        "APPEND", "XADD", "XDEL", "XGROUP", "XACK", "XTRIM", "XCLAIM",
        "RESTORE",
        "RENAME", "RENAMENX",
        "FLUSHDB", "FLUSHALL", "SELECT",
        nullptr
    };
    for(const char** p = kMutating; *p; ++p) {
        if(upper_name == *p) {
            return true;
        }
    }
    return false;
}

void AofLog::setPath(const std::string& path) {
    zero::Mutex::Lock lock(m_mutex);
    m_path = path;
}

const std::string& AofLog::getPath() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_path;
}

void AofLog::setEnabled(bool enabled) {
    zero::Mutex::Lock lock(m_mutex);
    m_enabled = enabled;
}

bool AofLog::isEnabled() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_enabled;
}

void AofLog::setFsyncPolicy(AofFsyncPolicy policy) {
    zero::Mutex::Lock lock(m_mutex);
    m_fsync = policy;
}

AofFsyncPolicy AofLog::fsyncPolicy() const {
    zero::Mutex::Lock lock(m_mutex);
    return m_fsync;
}

void AofLog::setFsyncPolicyString(const std::string& policy) {
    std::string p = policy;
    for(char& c : p) {
        c = (char)std::tolower((unsigned char)c);
    }
    if(p == "always") {
        setFsyncPolicy(AofFsyncPolicy::Always);
    } else if(p == "no") {
        setFsyncPolicy(AofFsyncPolicy::No);
    } else {
        setFsyncPolicy(AofFsyncPolicy::EverySec);
    }
}

std::string AofLog::fsyncPolicyString() const {
    switch(fsyncPolicy()) {
        case AofFsyncPolicy::Always:
            return "always";
        case AofFsyncPolicy::No:
            return "no";
        default:
            return "everysec";
    }
}

bool AofLog::syncToDisk() {
    std::string path;
    {
        zero::Mutex::Lock lock(m_mutex);
        if(!m_enabled) {
            return true;
        }
        path = m_path;
    }
    if(path.empty()) {
        return true;
    }
    int fd = ::open(path.c_str(), O_RDONLY);
    if(fd < 0) {
        return false;
    }
    const bool ok = ::fdatasync(fd) == 0;
    ::close(fd);
    return ok;
}

bool AofLog::append(const RespValue& command) {
    AofFsyncPolicy fsync = AofFsyncPolicy::EverySec;
    std::string path;
    {
        zero::Mutex::Lock lock(m_mutex);
        if(!m_enabled) {
            return true;
        }
        fsync = m_fsync;
        path = m_path;
    }
    std::string encoded = RespEncoder::encode(command);
    std::ofstream ofs(path, std::ios::binary | std::ios::app);
    if(!ofs) {
        return false;
    }
    ofs.write(encoded.data(), (std::streamsize)encoded.size());
    if(!ofs.good()) {
        return false;
    }
    if(fsync == AofFsyncPolicy::Always) {
        ofs.flush();
        return syncToDisk();
    }
    return true;
}

bool AofLog::replay(KvStore::ptr store, CommandDispatch::ptr dispatch, std::string* err) {
    std::string path;
    {
        zero::Mutex::Lock lock(m_mutex);
        path = m_path;
    }
    std::ifstream ifs(path, std::ios::binary);
    if(!ifs.good()) {
        return true;
    }
    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if(data.empty()) {
        return true;
    }

    KvContext ctx;
    size_t pos = 0;
    while(pos < data.size()) {
        RespValue req;
        size_t consumed = 0;
        RespReader reader(data.data() + pos, data.size() - pos);
        ParseStatus st = reader.tryParse(req, &consumed);
        if(st == ParseStatus::NeedMore) {
            if(err) {
                *err = "ERR truncated AOF at offset " + std::to_string(pos);
            }
            return false;
        }
        if(st == ParseStatus::Error) {
            if(err) {
                *err = "ERR corrupt AOF at offset " + std::to_string(pos);
            }
            return false;
        }
        pos += consumed;
        dispatch->dispatch(ctx, req, store);
        if(ctx.quit) {
            ctx.quit = false;
        }
    }
    return true;
}

bool AofLog::rewrite(KvStore::ptr store, std::string* err) {
    if(!store) {
        if(err) {
            *err = "ERR store unavailable";
        }
        return false;
    }
    std::string path;
    {
        zero::Mutex::Lock lock(m_mutex);
        path = m_path;
    }
    std::vector<RespValue> commands;
    buildRewriteCommands(*store, commands);
    std::string data;
    for(const auto& cmd : commands) {
        data += RespEncoder::encode(cmd);
    }
    return writeFileAtomic(path, data, err);
}

} // namespace kv
} // namespace zero

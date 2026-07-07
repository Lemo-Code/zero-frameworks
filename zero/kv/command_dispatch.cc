/**
 * @file command_dispatch.cc
 * @brief 命令分发实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "command_dispatch.h"
#include "info.h"
#include "lua_engine.h"
#include "persistence/aof.h"
#include "replication/replication.h"
#include "pubsub/pubsub_hub.h"
#include "blocking/blocking_list_hub.h"
#include "slowlog.h"
#include "kv_config.h"
#include "kv_session.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/concurrency/scheduler.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace zero {
namespace kv {

// Forward-declared here so configGet/configSet in anonymous ns can see it
static std::function<std::shared_ptr<LuaEngine>()> g_get_lua_engine_cfg;
// Cached config for lua-global-atomic, applied when LuaEngine is created
static bool g_lua_global_atomic_cfg = false;

namespace {

std::string cmdNameFromRequest(const RespValue& request) {
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

RespValue configGet(const RespValue& req, KvStore::ptr store) {
    static const std::unordered_map<std::string, std::string> kKnown = {
        {"maxmemory", "0"},
        {"maxmemory-policy", "noeviction"},
        {"save", ""},
        {"appendonly", "no"},
        {"appendfsync", "everysec"},
        {"requirepass", ""},
        {"timeout", "0"},
        {"tcp-keepalive", "300"},
        {"databases", "16"},
        {"slowlog-log-slower-than", "10000"},
        {"slowlog-max-len", "128"},
        {"dir", "."},
        {"dbfilename", "dump.rdb"},
        {"lua-global-atomic", "no"},
    };

    std::unordered_map<std::string, std::string> known = kKnown;
    if(store) {
        known["maxmemory"] = std::to_string(store->maxMemory());
        known["maxmemory-policy"] = store->maxMemoryPolicy();
    }
    AofLog::ptr aof = getAofForConfig();
    if(aof && aof->isEnabled()) {
        known["appendonly"] = "yes";
        known["appendfsync"] = aof->fsyncPolicyString();
    }
    if(isAuthRequired()) {
        known["requirepass"] = "yes";
    }
    known["slowlog-log-slower-than"] = std::to_string(getSlowlog().slowerThanUs());
    known["slowlog-max-len"] = std::to_string(getSlowlog().maxLen());
    if(store) {
        known["dbfilename"] = store->getRdbPath();
    }
    // lua-global-atomic: read from the global Lua engine
    // lua-global-atomic: read from cached config (always available)
    known["lua-global-atomic"] = g_lua_global_atomic_cfg ? "yes" : "no";
    // Sync to LuaEngine if it exists
    if(g_get_lua_engine_cfg) {
        auto lua = g_get_lua_engine_cfg();
        if(lua && lua->isGlobalAtomic() != g_lua_global_atomic_cfg) {
            lua->setGlobalAtomic(g_lua_global_atomic_cfg);
        }
    }

    RespValue arr;
    arr.type = RespType::Array;

    auto appendKey = [&](const std::string& key) {
        auto it = known.find(key);
        if(it != known.end()) {
            arr.array.push_back(RespEncoder::bulk(it->first));
            arr.array.push_back(RespEncoder::bulk(it->second));
        }
    };

    if(req.array.size() == 2) {
        for(const auto& p : known) {
            arr.array.push_back(RespEncoder::bulk(p.first));
            arr.array.push_back(RespEncoder::bulk(p.second));
        }
        return arr;
    }

    for(size_t i = 2; i < req.array.size(); ++i) {
        if(req.array[i].is_null) {
            continue;
        }
        const std::string& key = req.array[i].str;
        if(key == "*") {
            arr.array.clear();
            for(const auto& p : known) {
                arr.array.push_back(RespEncoder::bulk(p.first));
                arr.array.push_back(RespEncoder::bulk(p.second));
            }
            return arr;
        }
        appendKey(key);
    }
    return arr;
}

RespValue configSet(const RespValue& req, KvStore::ptr store) {
    if(req.array.size() != 4 || req.array[2].is_null || req.array[3].is_null) {
        return RespEncoder::err("ERR wrong number of arguments for CONFIG SET");
    }
    std::string key = req.array[2].str;
    for(char& c : key) {
        c = (char)std::tolower((unsigned char)c);
    }
    const std::string& value = req.array[3].str;
    if(key == "maxmemory" && store) {
        char* end = nullptr;
        long long n = std::strtoll(value.c_str(), &end, 10);
        if(end == value.c_str()) {
            return RespEncoder::err("ERR invalid maxmemory");
        }
        store->setMaxMemory((int64_t)n);
        return RespEncoder::ok();
    }
    if(key == "maxmemory-policy" && store) {
        static const char* kPolicies[] = {
            "noeviction", "allkeys-lru", "allkeys-random",
            "volatile-lru", "volatile-random", "volatile-ttl", nullptr
        };
        bool ok = false;
        for(const char** p = kPolicies; *p; ++p) {
            if(value == *p) {
                ok = true;
                break;
            }
        }
        if(!ok) {
            return RespEncoder::err("ERR invalid maxmemory policy");
        }
        store->setMaxMemoryPolicy(value);
        return RespEncoder::ok();
    }
    if(key == "requirepass") {
        setRequirePass(value);
        return RespEncoder::ok();
    }
    if(key == "appendfsync") {
        AofLog::ptr aof = getAofForConfig();
        if(!aof) {
            return RespEncoder::err("ERR AOF unavailable");
        }
        aof->setFsyncPolicyString(value);
        return RespEncoder::ok();
    }
    if(key == "slowlog-log-slower-than") {
        char* end = nullptr;
        long long n = std::strtoll(value.c_str(), &end, 10);
        if(end == value.c_str()) {
            return RespEncoder::err("ERR invalid slowlog-log-slower-than");
        }
        getSlowlog().setSlowerThanUs((int64_t)n);
        return RespEncoder::ok();
    }
    if(key == "slowlog-max-len") {
        char* end = nullptr;
        long long n = std::strtoll(value.c_str(), &end, 10);
        if(end == value.c_str()) {
            return RespEncoder::err("ERR invalid slowlog-max-len");
        }
        getSlowlog().setMaxLen((int64_t)n);
        return RespEncoder::ok();
    }
    if(key == "dbfilename" && store) {
        store->setRdbPath(value);
        return RespEncoder::ok();
    }
    if(key == "lua-global-atomic") {
        if(value != "yes" && value != "no") {
            return RespEncoder::err("ERR lua-global-atomic must be 'yes' or 'no'");
        }
        g_lua_global_atomic_cfg = (value == "yes");
        // Sync to LuaEngine if it already exists
        if(g_get_lua_engine_cfg) {
            auto lua = g_get_lua_engine_cfg();
            if(lua) {
                lua->setGlobalAtomic(g_lua_global_atomic_cfg);
            }
        }
        return RespEncoder::ok();
    }
    if(key == "dir") {
        return RespEncoder::ok();
    }
    return RespEncoder::err("ERR unsupported CONFIG parameter: " + key);
}

RespValue encodeStreamEntries(const std::vector<KvStore::StreamEntry>& entries) {
    RespValue arr;
    arr.type = RespType::Array;
    for(const auto& entry : entries) {
        RespValue row;
        row.type = RespType::Array;
        row.array.push_back(RespEncoder::bulk(entry.id));
        std::vector<std::string> field_names;
        field_names.reserve(entry.fields.size());
        for(const auto& f : entry.fields) {
            field_names.push_back(f.first);
        }
        std::sort(field_names.begin(), field_names.end());
        for(const auto& name : field_names) {
            row.array.push_back(RespEncoder::bulk(name));
            row.array.push_back(RespEncoder::bulk(entry.fields.at(name)));
        }
        arr.array.push_back(row);
    }
    return arr;
}

bool parseDbIndex(const RespValue& v, int& db_out) {
    if(v.is_null || v.type != RespType::BulkString) {
        return false;
    }
    if(v.str.empty()) {
        return false;
    }
    char* end = nullptr;
    long n = std::strtol(v.str.c_str(), &end, 10);
    if(end == v.str.c_str() || *end != '\0' || n < 0 || n > 15) {
        return false;
    }
    db_out = (int)n;
    return true;
}

bool parseInt64Arg(const RespValue& v, int64_t& out) {
    if(v.is_null || v.type != RespType::BulkString || v.str.empty()) {
        return false;
    }
    char* end = nullptr;
    long long n = std::strtoll(v.str.c_str(), &end, 10);
    if(end == v.str.c_str() || *end != '\0') {
        return false;
    }
    out = (int64_t)n;
    return true;
}

bool parseDoubleArg(const RespValue& v, double& out) {
    if(v.is_null || v.type != RespType::BulkString || v.str.empty()) {
        return false;
    }
    char* end = nullptr;
    const double n = std::strtod(v.str.c_str(), &end);
    if(end == v.str.c_str() || *end != '\0') {
        return false;
    }
    out = n;
    return true;
}

std::string formatScore(double score) {
    std::ostringstream ss;
    ss << score;
    return ss.str();
}

std::string upperArg(const std::string& s) {
    std::string out = s;
    for(char& c : out) {
        c = (char)std::toupper((unsigned char)c);
    }
    return out;
}

struct SetOptions {
    bool nx = false;
    bool xx = false;
    int64_t ex = -1;
    int64_t px = -1;
};

bool parseSetOptions(const RespValue& req, SetOptions& opt, std::string& err) {
    for(size_t i = 3; i < req.array.size(); ++i) {
        if(req.array[i].is_null) {
            err = "ERR syntax error";
            return false;
        }
        const std::string flag = upperArg(req.array[i].str);
        if(flag == "NX") {
            if(opt.xx) {
                err = "ERR syntax error";
                return false;
            }
            opt.nx = true;
        } else if(flag == "XX") {
            if(opt.nx) {
                err = "ERR syntax error";
                return false;
            }
            opt.xx = true;
        } else if(flag == "EX") {
            if(opt.px >= 0) {
                err = "ERR syntax error";
                return false;
            }
            if(i + 1 >= req.array.size() || req.array[i + 1].is_null) {
                err = "ERR syntax error";
                return false;
            }
            if(!parseInt64Arg(req.array[i + 1], opt.ex)) {
                err = "ERR value is not an integer or out of range";
                return false;
            }
            ++i;
        } else if(flag == "PX") {
            if(opt.ex >= 0) {
                err = "ERR syntax error";
                return false;
            }
            if(i + 1 >= req.array.size() || req.array[i + 1].is_null) {
                err = "ERR syntax error";
                return false;
            }
            if(!parseInt64Arg(req.array[i + 1], opt.px)) {
                err = "ERR value is not an integer or out of range";
                return false;
            }
            ++i;
        } else {
            err = "ERR syntax error";
            return false;
        }
    }
    return true;
}

RespValue dispatchIncrBy(KvContext& ctx, const RespValue& req, KvStore::ptr store,
                         int64_t delta, const char* cmd, size_t min_args) {
    if(req.array.size() != min_args || req.array[1].is_null) {
        return RespEncoder::err(std::string("ERR wrong number of arguments for '") + cmd + "' command");
    }
    int64_t value = 0;
    std::string err;
    if(!store->incrBy(ctx.db, req.array[1].str, delta, value, err)) {
        return RespEncoder::err(err);
    }
    return RespEncoder::integer(value);
}

bool isPubsubAllowedCommand(const std::string& name) {
    return name == "SUBSCRIBE" || name == "UNSUBSCRIBE"
        || name == "PSUBSCRIBE" || name == "PUNSUBSCRIBE"
        || name == "PING" || name == "QUIT";
}

RespValue makePubSubMetaReply(const std::string& kind, const std::string& channel, int64_t count) {
    RespValue one;
    one.type = RespType::Array;
    one.array.push_back(RespEncoder::bulk(kind));
    one.array.push_back(RespEncoder::bulk(channel));
    one.array.push_back(RespEncoder::integer(count));
    return one;
}

RespValue dispatchZrange(KvContext& ctx, const RespValue& req, KvStore::ptr store,
                         bool reverse, const char* cmd_name) {
    if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null
       || req.array[3].is_null) {
        return RespEncoder::err(std::string("ERR wrong number of arguments for '") + cmd_name
                                + "' command");
    }
    int64_t start = 0;
    int64_t stop = 0;
    if(!parseInt64Arg(req.array[2], start) || !parseInt64Arg(req.array[3], stop)) {
        return RespEncoder::err("ERR value is not an integer or out of range");
    }
    bool with_scores = false;
    if(req.array.size() >= 5 && !req.array[4].is_null
       && upperArg(req.array[4].str) == "WITHSCORES") {
        with_scores = true;
    }
    std::vector<std::pair<std::string, double>> values;
    bool found = false;
    std::string err;
    if(!store->zrange(ctx.db, req.array[1].str, start, stop, reverse, values, found, &err)) {
        return RespEncoder::err(err);
    }
    RespValue arr;
    arr.type = RespType::Array;
    for(const auto& item : values) {
        arr.array.push_back(RespEncoder::bulk(item.first));
        if(with_scores) {
            arr.array.push_back(RespEncoder::bulk(formatScore(item.second)));
        }
    }
    return arr;
}

void clearWatch(KvContext& ctx) {
    ctx.watched.clear();
}

bool watchConflict(KvContext& ctx, KvStore::ptr store) {
    for(const auto& entry : ctx.watched) {
        if(store->watchToken(entry.db, entry.key) != entry.token) {
            return true;
        }
    }
    return false;
}

void notifyListPush(int db, const std::string& key, KvStore::ptr store) {
    BlockingListHub::ptr hub = getBlockingListHub();
    if(hub) {
        hub->onPush(db, key, store);
    }
}

RespValue tryBlockingPop(KvContext& ctx, const RespValue& req, KvStore::ptr store, bool right) {
    if(req.array.size() < 3) {
        return RespEncoder::err(std::string("ERR wrong number of arguments for '")
                                + (right ? "brpop" : "blpop") + "' command");
    }
    std::vector<std::string> keys;
    for(size_t i = 1; i + 1 < req.array.size(); ++i) {
        if(req.array[i].is_null) {
            return RespEncoder::err("ERR syntax error");
        }
        keys.push_back(req.array[i].str);
    }
    if(keys.empty() || req.array.back().is_null) {
        return RespEncoder::err("ERR syntax error");
    }
    double timeout = 0;
    if(!parseDoubleArg(req.array.back(), timeout) || timeout < 0) {
        return RespEncoder::err("ERR timeout is not a float or out of range");
    }

    for(const auto& key : keys) {
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listPop(ctx.db, key, !right, value, found, &err)) {
            return RespEncoder::err(err);
        }
        if(found) {
            RespValue arr;
            arr.type = RespType::Array;
            arr.array.push_back(RespEncoder::bulk(key));
            arr.array.push_back(RespEncoder::bulk(value));
            return arr;
        }
    }

    if(timeout == 0) {
        return RespEncoder::nullBulk();
    }

    BlockingListHub::ptr hub = getBlockingListHub();
    if(!hub || !zero::Scheduler::GetThis()) {
        return RespEncoder::nullBulk();
    }

    int64_t deadline_ms = -1;
    if(timeout > 0) {
        deadline_ms = KvStore::nowMs() + (int64_t)(timeout * 1000.0);
    }

    ListWaiter waiter;
    waiter.session = ctx.session;
    waiter.db = ctx.db;
    waiter.keys = keys;
    waiter.pop_right = right;
    waiter.deadline_ms = deadline_ms;
    waiter.scheduler = zero::Scheduler::GetThis();
    waiter.fiber = zero::Fiber::GetThis();

    hub->addWaiter(&waiter);
    while(!waiter.done) {
        if(deadline_ms >= 0 && KvStore::nowMs() >= deadline_ms) {
            hub->removeWaiter(&waiter);
            return RespEncoder::nullBulk();
        }
        zero::Fiber::YieldToHold();
    }
    hub->removeWaiter(&waiter);
    if(waiter.result_key.empty()) {
        return RespEncoder::nullBulk();
    }
    RespValue arr;
    arr.type = RespType::Array;
    arr.array.push_back(RespEncoder::bulk(waiter.result_key));
    arr.array.push_back(RespEncoder::bulk(waiter.result_value));
    return arr;
}

ReplicationManager* replicationFromCtx(KvContext& ctx) {
    if(ctx.replication) {
        return ctx.replication;
    }
    return getReplicationForConfig().get();
}

RespValue makeRoleReply(KvContext& ctx) {
    ReplicationManager* repl = replicationFromCtx(ctx);
    RespValue arr;
    arr.type = RespType::Array;
    if(!repl || repl->role() == ReplicationManager::Role::Master) {
        arr.array.push_back(RespEncoder::bulk("master"));
        arr.array.push_back(RespEncoder::integer(repl ? repl->replOffset() : 0));
        RespValue slaves;
        slaves.type = RespType::Array;
        arr.array.push_back(slaves);
        return arr;
    }
    arr.array.push_back(RespEncoder::bulk("slave"));
    arr.array.push_back(RespEncoder::bulk(repl->masterHost()));
    arr.array.push_back(RespEncoder::integer(repl->masterPort()));
    arr.array.push_back(RespEncoder::bulk("connect"));
    arr.array.push_back(RespEncoder::bulk(std::to_string(repl->masterLinkStatus())));
    return arr;
}

} // namespace

std::function<bool()> g_bgsave_runner;
std::function<bool()> g_bgrewriteaof_runner;
std::function<std::vector<RedisClientSnapshot>()> g_client_list_fn;

bool isSlaveWriteAllowed(const std::string& upper_cmd) {
    static const char* kAllowed[] = {
        "AUTH", "PING", "INFO", "ROLE", "SLAVEOF", "REPLICAOF", "QUIT", "CONFIG", "CLIENT",
        nullptr
    };
    for(const char** p = kAllowed; *p; ++p) {
        if(upper_cmd == *p) {
            return true;
        }
    }
    return false;
}

void bindBgsaveForConfig(std::function<bool()> runner) {
    g_bgsave_runner = std::move(runner);
}

void bindBgRewriteAofForConfig(std::function<bool()> runner) {
    g_bgrewriteaof_runner = std::move(runner);
}

void bindClientListForConfig(std::function<std::vector<RedisClientSnapshot>()> fn) {
    g_client_list_fn = std::move(fn);
}

void bindLuaEngineForConfig(std::function<LuaEngine::ptr()> fn) {
    g_get_lua_engine_cfg = std::move(fn);
}

void CommandDispatch::addCommand(const std::string& name, CommandHandler::ptr handler) {
    RWMutexType::WriteLock lock(m_mutex);
    // Register both original case and lowercase for fast-path dispatch (Phase 7)
    m_handlers[name] = handler;
    std::string lower = name;
    for(char& c : lower) {
        c = (char)std::tolower((unsigned char)c);
    }
    if(lower != name) {
        m_handlers[lower] = handler;  // shared_ptr copy — both entries share ownership
    }
}

void CommandDispatch::addCommand(const std::string& name, FunctionCommandHandler::callback cb) {
    addCommand(name, std::make_shared<FunctionCommandHandler>(std::move(cb)));
}

std::string CommandDispatch::upperName(const RespValue& request) {
    return cmdNameFromRequest(request);
}

std::string CommandDispatch::upper(const std::string& s) {
    std::string out = s;
    for(char& c : out) {
        c = (char)std::toupper((unsigned char)c);
    }
    return out;
}

RespValue CommandDispatch::dispatchExecOne(KvContext& ctx, const RespValue& request,
                                           KvStore::ptr store) {
    if(request.type != RespType::Array) {
        return RespEncoder::err("ERR protocol error: expected array");
    }
    if(request.array.empty() || request.array[0].type != RespType::BulkString
            || request.array[0].is_null) {
        return RespEncoder::err("ERR empty command");
    }
    // Fast-path: try raw command name first (no copy, no toupper) — Phase 7
    const std::string& rawName = request.array[0].str;
    CommandHandler::ptr handler;
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_handlers.find(rawName);
        if(it == m_handlers.end()) {
            // Fallback: lowercase lookup
            std::string lower = rawName;
            for(char& c : lower) {
                c = (char)std::tolower((unsigned char)c);
            }
            it = m_handlers.find(lower);
        }
        if(it != m_handlers.end()) {
            handler = it->second;
        } else {
            handler = m_default;
        }
    }
    if(!handler) {
        return RespEncoder::err("ERR unknown command '" + rawName + "'");
    }
    return handler->handle(ctx, request, store);
}

RespValue CommandDispatch::dispatch(KvContext& ctx, const RespValue& request, KvStore::ptr store) {
    if(request.type != RespType::Array) {
        return RespEncoder::err("ERR protocol error: expected array");
    }
    if(request.array.empty() || request.array[0].type != RespType::BulkString
            || request.array[0].is_null) {
        return RespEncoder::err("ERR empty command");
    }
    // Fast-path: try raw command name first (no copy, no toupper) — Phase 7
    const std::string& rawName = request.array[0].str;
    // Compute uppercase name lazily — only when needed for checks
    std::string upperName;
    auto getUpper = [&]() -> const std::string& {
        if(upperName.empty()) {
            upperName = rawName;
            for(char& c : upperName) {
                c = (char)std::toupper((unsigned char)c);
            }
        }
        return upperName;
    };

    if(ctx.pubsub_mode && !isPubsubAllowedCommand(rawName)) {
        if(!isPubsubAllowedCommand(getUpper())) {
            return RespEncoder::err("ERR only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT allowed in Pub/Sub mode");
        }
    }
    if(isAuthRequired() && !ctx.authenticated && !isCommandAuthExempt(rawName)) {
        if(!isCommandAuthExempt(getUpper())) {
            return RespEncoder::err("NOAUTH Authentication required.");
        }
    }
    ReplicationManager* repl = ctx.replication;
    if(!repl) {
        ReplicationManager::ptr global_repl = getReplicationForConfig();
        repl = global_repl.get();
    }
    if(!ctx.repl_apply && repl && repl->role() == ReplicationManager::Role::Slave
       && (isMutatingCommand(rawName) || isMutatingCommand(getUpper()))
       && !isSlaveWriteAllowed(rawName) && !isSlaveWriteAllowed(getUpper())) {
        return RespEncoder::err("READONLY You can't write against a read only replica.");
    }
    if(ctx.multi_mode) {
        const std::string& n = getUpper();
        if(n == "EXEC" || n == "DISCARD") {
            // fall through
        } else if(n == "MULTI") {
            return RespEncoder::err("ERR MULTI calls can not be nested");
        } else if(n == "WATCH" || n == "BLPOP" || n == "BRPOP") {
            return RespEncoder::err("ERR " + n + " not allowed in MULTI");
        } else {
            ctx.multi_queue.push_back(request);
            return RespEncoder::queued();
        }
    }

    // Fast-path handler lookup: try raw name, fallback to lowercase
    CommandHandler::ptr handler;
    {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_handlers.find(rawName);
        if(it == m_handlers.end()) {
            // Fallback: lowercase lookup
            std::string lower = rawName;
            for(char& c : lower) {
                c = (char)std::tolower((unsigned char)c);
            }
            it = m_handlers.find(lower);
        }
        if(it != m_handlers.end()) {
            handler = it->second;
        } else {
            handler = m_default;
        }
    }
    if(!handler) {
        return RespEncoder::err("ERR unknown command '" + rawName + "'");
    }
    const int64_t t0 = KvStore::nowMs();
    RespValue result = handler->handle(ctx, request, store);
    const int64_t t1 = KvStore::nowMs();
    getSlowlog().record(ctx.client_id, ctx.client_addr, request, (t1 - t0) * 1000);
    return result;
}

void registerBuiltinCommands(CommandDispatch::ptr dispatch) {
    dispatch->addCommand("PING", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() >= 2 && !req.array[1].is_null) {
            return RespEncoder::bulk(req.array[1].str);
        }
        return RespEncoder::pong();
    });

    dispatch->addCommand("CONFIG", [](KvContext&, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'config' command");
        }
        std::string sub = req.array[1].str;
        for(char& c : sub) {
            c = (char)std::toupper((unsigned char)c);
        }
        if(sub == "GET") {
            return configGet(req, store);
        }
        if(sub == "SET") {
            return configSet(req, store);
        }
        return RespEncoder::err("ERR CONFIG subcommand must be GET or SET");
    });

    dispatch->addCommand("INFO", [](KvContext& ctx, const RespValue&, KvStore::ptr store) {
        return RespEncoder::bulk(buildInfoText(ctx.db, store));
    });

    dispatch->addCommand("SELECT", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'select' command");
        }
        int db = 0;
        if(!parseDbIndex(req.array[1], db)) {
            return RespEncoder::err("ERR invalid DB index");
        }
        ctx.db = db;
        return RespEncoder::ok();
    });

    dispatch->addCommand("DBSIZE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'dbsize' command");
        }
        return RespEncoder::integer(store->dbsize(ctx.db));
    });

    dispatch->addCommand("FLUSHDB", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'flushdb' command");
        }
        store->flushdb(ctx.db);
        return RespEncoder::ok();
    });

    dispatch->addCommand("FLUSHALL", [](KvContext&, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'flushall' command");
        }
        store->flushall();
        return RespEncoder::ok();
    });

    dispatch->addCommand("TYPE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'type' command");
        }
        return RespEncoder::bulk(store->type(ctx.db, req.array[1].str));
    });

    dispatch->addCommand("KEYS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'keys' command");
        }
        std::vector<std::string> matched = store->keys(ctx.db, req.array[1].str);
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& k : matched) {
            arr.array.push_back(RespEncoder::bulk(k));
        }
        return arr;
    });

    dispatch->addCommand("SCAN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'scan' command");
        }
        int64_t cursor_val = 0;
        if(!parseInt64Arg(req.array[1], cursor_val) || cursor_val < 0) {
            return RespEncoder::err("ERR invalid cursor");
        }
        std::string pattern = "*";
        int64_t count = 10;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            const std::string opt = upperArg(req.array[i].str);
            if(opt == "MATCH") {
                if(i + 1 >= req.array.size() || req.array[i + 1].is_null) {
                    return RespEncoder::err("ERR syntax error");
                }
                pattern = req.array[i + 1].str;
                ++i;
            } else if(opt == "COUNT") {
                if(i + 1 >= req.array.size() || !parseInt64Arg(req.array[i + 1], count)) {
                    return RespEncoder::err("ERR syntax error");
                }
                ++i;
            } else {
                return RespEncoder::err("ERR syntax error");
            }
        }
        ScanResult sr = store->scan(ctx.db, (uint64_t)cursor_val, pattern, (int)count);
        RespValue arr;
        arr.type = RespType::Array;
        arr.array.push_back(RespEncoder::bulk(std::to_string(sr.cursor)));
        RespValue keys;
        keys.type = RespType::Array;
        for(const auto& k : sr.keys) {
            keys.array.push_back(RespEncoder::bulk(k));
        }
        arr.array.push_back(keys);
        return arr;
    });

    dispatch->addCommand("ECHO", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'echo' command");
        }
        return RespEncoder::bulk(req.array[1].str);
    });

    dispatch->addCommand("GET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'get' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->get(ctx.db, req.array[1].str, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(std::move(value)) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("GETSET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'getset' command");
        }
        std::string old_val;
        bool found = false;
        std::string err;
        if(!store->getset(ctx.db, req.array[1].str, req.array[2].str, old_val, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(std::move(old_val)) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("GETDEL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'getdel' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->getdel(ctx.db, req.array[1].str, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(std::move(value)) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("EXISTS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'exists' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        return RespEncoder::integer(store->existsMany(ctx.db, keys));
    });

    dispatch->addCommand("SET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'set' command");
        }
        SetOptions opt;
        std::string err;
        if(!parseSetOptions(req, opt, err)) {
            return RespEncoder::err(err);
        }
        const int rc = store->setOpt(ctx.db, req.array[1].str, req.array[2].str,
            opt.nx, opt.xx, opt.ex, opt.px);
        return rc ? RespEncoder::ok() : RespEncoder::nullBulk();
    });

    dispatch->addCommand("SETEX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'setex' command");
        }
        int64_t ttl = 0;
        if(!parseInt64Arg(req.array[2], ttl)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        store->setex(ctx.db, req.array[1].str, req.array[3].str, ttl);
        return RespEncoder::ok();
    });

    dispatch->addCommand("EXPIRE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'expire' command");
        }
        int64_t sec = 0;
        if(!parseInt64Arg(req.array[2], sec)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        return RespEncoder::integer(store->expire(ctx.db, req.array[1].str, sec));
    });

    dispatch->addCommand("PEXPIRE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'pexpire' command");
        }
        int64_t ms = 0;
        if(!parseInt64Arg(req.array[2], ms)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        return RespEncoder::integer(store->pexpire(ctx.db, req.array[1].str, ms));
    });

    dispatch->addCommand("TTL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'ttl' command");
        }
        return RespEncoder::integer(store->ttl(ctx.db, req.array[1].str));
    });

    dispatch->addCommand("PTTL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'pttl' command");
        }
        return RespEncoder::integer(store->pttl(ctx.db, req.array[1].str));
    });

    dispatch->addCommand("PERSIST", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'persist' command");
        }
        return RespEncoder::integer(store->persist(ctx.db, req.array[1].str));
    });

    dispatch->addCommand("EXPIREAT", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'expireat' command");
        }
        int64_t ts = 0;
        if(!parseInt64Arg(req.array[2], ts)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        return RespEncoder::integer(store->expireAt(ctx.db, req.array[1].str, ts));
    });

    dispatch->addCommand("PEXPIREAT", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'pexpireat' command");
        }
        int64_t ts = 0;
        if(!parseInt64Arg(req.array[2], ts)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        return RespEncoder::integer(store->pexpireAt(ctx.db, req.array[1].str, ts));
    });

    dispatch->addCommand("MGET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'mget' command");
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                arr.array.push_back(RespEncoder::nullBulk());
                continue;
            }
            std::string value;
            bool found = false;
            std::string err;
            if(!store->get(ctx.db, req.array[i].str, value, found, &err)) {
                return RespEncoder::err(err);
            }
            arr.array.push_back(found ? RespEncoder::bulk(value) : RespEncoder::nullBulk());
        }
        return arr;
    });

    dispatch->addCommand("MSET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array.size() % 2 == 0) {
            return RespEncoder::err("ERR wrong number of arguments for 'mset' command");
        }
        for(size_t i = 1; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error in MSET");
            }
            store->set(ctx.db, req.array[i].str, req.array[i + 1].str);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("DEL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'del' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        return RespEncoder::integer(store->delMany(ctx.db, keys));
    });

    dispatch->addCommand("INCR", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchIncrBy(ctx, req, store, 1, "incr", 2);
    });

    dispatch->addCommand("DECR", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchIncrBy(ctx, req, store, -1, "decr", 2);
    });

    dispatch->addCommand("INCRBY", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'incrby' command");
        }
        int64_t delta = 0;
        if(!parseInt64Arg(req.array[2], delta)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        int64_t value = 0;
        std::string err;
        if(!store->incrBy(ctx.db, req.array[1].str, delta, value, err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(value);
    });

    dispatch->addCommand("DECRBY", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'decrby' command");
        }
        int64_t delta = 0;
        if(!parseInt64Arg(req.array[2], delta)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        int64_t value = 0;
        std::string err;
        if(!store->incrBy(ctx.db, req.array[1].str, -delta, value, err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(value);
    });

    dispatch->addCommand("HSET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array.size() % 2 != 0 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hset' command");
        }
        if(req.array.size() == 4) {
            std::string err;
            const int64_t n = store->hset(ctx.db, req.array[1].str, req.array[2].str,
                req.array[3].str, &err);
            if(n < 0) {
                return RespEncoder::err(err);
            }
            return RespEncoder::integer(n);
        }
        std::vector<std::pair<std::string, std::string>> fields;
        for(size_t i = 2; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            fields.emplace_back(req.array[i].str, req.array[i + 1].str);
        }
        std::string err;
        const int64_t n = store->hsetFields(ctx.db, req.array[1].str, fields, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("HGET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hget' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->hget(ctx.db, req.array[1].str, req.array[2].str, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(value) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("HGETALL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hgetall' command");
        }
        std::vector<std::pair<std::string, std::string>> fields;
        bool found = false;
        std::string err;
        if(!store->hgetall(ctx.db, req.array[1].str, fields, found, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        if(!found) {
            return arr;
        }
        for(const auto& pair : fields) {
            arr.array.push_back(RespEncoder::bulk(pair.first));
            arr.array.push_back(RespEncoder::bulk(pair.second));
        }
        return arr;
    });

    dispatch->addCommand("HDEL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hdel' command");
        }
        int64_t removed = 0;
        std::string err;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            int64_t n = 0;
            if(!store->hdel(ctx.db, req.array[1].str, req.array[i].str, n, &err)) {
                return RespEncoder::err(err);
            }
            removed += n;
        }
        return RespEncoder::integer(removed);
    });

    dispatch->addCommand("HEXISTS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hexists' command");
        }
        bool exists_out = false;
        std::string err;
        if(!store->hexists(ctx.db, req.array[1].str, req.array[2].str, exists_out, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(exists_out ? 1 : 0);
    });

    dispatch->addCommand("HLEN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hlen' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->hlen(ctx.db, req.array[1].str, found, &err);
        if(!err.empty()) {
            return RespEncoder::err(err);
        }
        if(!found) {
            return RespEncoder::integer(0);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("HMSET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || (req.array.size() % 2) != 0 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hmset' command");
        }
        std::vector<std::pair<std::string, std::string>> fields;
        for(size_t i = 2; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            fields.emplace_back(req.array[i].str, req.array[i + 1].str);
        }
        std::string err;
        if(!store->hmset(ctx.db, req.array[1].str, fields, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("HMGET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hmget' command");
        }
        std::vector<std::string> fields;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            fields.push_back(req.array[i].str);
        }
        std::vector<std::pair<bool, std::string>> values;
        std::string err;
        if(!store->hmget(ctx.db, req.array[1].str, fields, values, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& item : values) {
            arr.array.push_back(item.first ? RespEncoder::bulk(item.second) : RespEncoder::nullBulk());
        }
        return arr;
    });

    dispatch->addCommand("HKEYS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hkeys' command");
        }
        std::vector<std::string> keys;
        bool found = false;
        std::string err;
        if(!store->hkeys(ctx.db, req.array[1].str, keys, found, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& k : keys) {
            arr.array.push_back(RespEncoder::bulk(k));
        }
        return arr;
    });

    dispatch->addCommand("HVALS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hvals' command");
        }
        std::vector<std::string> vals;
        bool found = false;
        std::string err;
        if(!store->hvals(ctx.db, req.array[1].str, vals, found, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& v : vals) {
            arr.array.push_back(RespEncoder::bulk(v));
        }
        return arr;
    });

    dispatch->addCommand("WATCH", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'watch' command");
        }
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            WatchEntry entry;
            entry.db = ctx.db;
            entry.key = req.array[i].str;
            entry.token = store->watchToken(entry.db, entry.key);
            bool dup = false;
            for(const auto& w : ctx.watched) {
                if(w.db == entry.db && w.key == entry.key) {
                    dup = true;
                    break;
                }
            }
            if(!dup) {
                ctx.watched.push_back(entry);
            }
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("UNWATCH", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'unwatch' command");
        }
        clearWatch(ctx);
        return RespEncoder::ok();
    });

    dispatch->addCommand("MULTI", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'multi' command");
        }
        if(ctx.multi_mode) {
            return RespEncoder::err("ERR MULTI calls can not be nested");
        }
        ctx.multi_mode = true;
        ctx.multi_queue.clear();
        return RespEncoder::ok();
    });

    dispatch->addCommand("DISCARD", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'discard' command");
        }
        if(!ctx.multi_mode) {
            return RespEncoder::err("ERR DISCARD without MULTI");
        }
        ctx.multi_mode = false;
        ctx.multi_queue.clear();
        clearWatch(ctx);
        return RespEncoder::ok();
    });

    dispatch->addCommand("EXEC", [dispatch](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'exec' command");
        }
        if(!ctx.multi_mode) {
            return RespEncoder::err("ERR EXEC without MULTI");
        }
        if(watchConflict(ctx, store)) {
            ctx.multi_mode = false;
            ctx.multi_queue.clear();
            clearWatch(ctx);
            return RespEncoder::nullBulk();
        }
        std::vector<RespValue> queue = std::move(ctx.multi_queue);
        ctx.multi_mode = false;
        ctx.multi_queue.clear();
        clearWatch(ctx);

        RespValue arr;
        arr.type = RespType::Array;
        AofLog::ptr aof = getAofForConfig();
        ReplicationManager* repl = replicationFromCtx(ctx);
        for(const auto& cmd : queue) {
            RespValue rsp = dispatch->dispatchExecOne(ctx, cmd, store);
            arr.array.push_back(rsp);
            const std::string name = cmdNameFromRequest(cmd);
            if(rsp.type != RespType::Error && isMutatingCommand(name)) {
                if(aof && aof->isEnabled()) {
                    aof->append(cmd);
                }
                if(repl) {
                    repl->propagateCommand(cmd);
                }
            }
        }
        return arr;
    });

    dispatch->addCommand("LPUSH", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lpush' command");
        }
        std::vector<std::string> values;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            values.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->listPush(ctx.db, req.array[1].str, values, true, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        notifyListPush(ctx.db, req.array[1].str, store);
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("RPUSH", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'rpush' command");
        }
        std::vector<std::string> values;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            values.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->listPush(ctx.db, req.array[1].str, values, false, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        notifyListPush(ctx.db, req.array[1].str, store);
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("BLPOP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return tryBlockingPop(ctx, req, store, false);
    });

    dispatch->addCommand("BRPOP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return tryBlockingPop(ctx, req, store, true);
    });

    dispatch->addCommand("LPOP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lpop' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listPop(ctx.db, req.array[1].str, true, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(value) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("RPOP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'rpop' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listPop(ctx.db, req.array[1].str, false, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(value) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("LLEN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'llen' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->listLen(ctx.db, req.array[1].str, found, &err);
        if(!err.empty()) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(found ? n : 0);
    });

    dispatch->addCommand("LRANGE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lrange' command");
        }
        int64_t start = 0;
        int64_t stop = 0;
        if(!parseInt64Arg(req.array[2], start) || !parseInt64Arg(req.array[3], stop)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        std::vector<std::string> values;
        bool found = false;
        std::string err;
        if(!store->listRange(ctx.db, req.array[1].str, start, stop, values, found, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& v : values) {
            arr.array.push_back(RespEncoder::bulk(v));
        }
        return arr;
    });

    dispatch->addCommand("SADD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'sadd' command");
        }
        std::vector<std::string> members;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            members.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->setAdd(ctx.db, req.array[1].str, members, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("SREM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'srem' command");
        }
        std::vector<std::string> members;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            members.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->setRem(ctx.db, req.array[1].str, members, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("SMEMBERS", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'smembers' command");
        }
        std::vector<std::string> members;
        bool found = false;
        std::string err;
        if(!store->setMembers(ctx.db, req.array[1].str, members, found, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& m : members) {
            arr.array.push_back(RespEncoder::bulk(m));
        }
        return arr;
    });

    dispatch->addCommand("SCARD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'scard' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->setCard(ctx.db, req.array[1].str, found, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(found ? n : 0);
    });

    dispatch->addCommand("SISMEMBER", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'sismember' command");
        }
        bool is_member = false;
        bool found = false;
        std::string err;
        if(!store->setIsMember(ctx.db, req.array[1].str, req.array[2].str, is_member, found, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(is_member ? 1 : 0);
    });

    dispatch->addCommand("ZADD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zadd' command");
        }
        ZaddOptions opt;
        size_t idx = 2;
        for(; idx < req.array.size(); ++idx) {
            if(req.array[idx].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            const std::string flag = upperArg(req.array[idx].str);
            if(flag == "NX") {
                opt.nx = true;
            } else if(flag == "XX") {
                opt.xx = true;
            } else if(flag == "GT") {
                opt.gt = true;
            } else if(flag == "LT") {
                opt.lt = true;
            } else if(flag == "CH") {
                opt.ch = true;
            } else {
                break;
            }
        }
        if((req.array.size() - idx) % 2 != 0) {
            return RespEncoder::err("ERR syntax error");
        }
        std::vector<std::pair<double, std::string>> members;
        for(size_t i = idx; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            double score = 0;
            if(!parseDoubleArg(req.array[i], score)) {
                return RespEncoder::err("ERR value is not a valid float");
            }
            members.emplace_back(score, req.array[i + 1].str);
        }
        std::string err;
        const int64_t n = store->zaddOpt(ctx.db, req.array[1].str, opt, members, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("ZREM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zrem' command");
        }
        std::vector<std::string> members;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            members.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->zrem(ctx.db, req.array[1].str, members, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("ZSCORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zscore' command");
        }
        double score = 0;
        bool found = false;
        std::string err;
        if(!store->zscore(ctx.db, req.array[1].str, req.array[2].str, score, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(formatScore(score)) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("ZCARD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zcard' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->zcard(ctx.db, req.array[1].str, found, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(found ? n : 0);
    });

    dispatch->addCommand("ZRANGE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchZrange(ctx, req, store, false, "zrange");
    });

    dispatch->addCommand("ZREVRANGE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchZrange(ctx, req, store, true, "zrevrange");
    });

    dispatch->addCommand("ZINCRBY", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zincrby' command");
        }
        double increment = 0;
        if(!parseDoubleArg(req.array[2], increment)) {
            return RespEncoder::err("ERR value is not a valid float");
        }
        double score = 0;
        bool existed = false;
        std::string err;
        if(!store->zincrby(ctx.db, req.array[1].str, req.array[3].str, increment, score, existed, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::bulk(formatScore(score));
    });

    dispatch->addCommand("ZRANK", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zrank' command");
        }
        int64_t rank = -1;
        bool key_found = false;
        std::string err;
        if(!store->zrank(ctx.db, req.array[1].str, req.array[2].str, false, rank, key_found, &err)) {
            return RespEncoder::err(err);
        }
        return rank >= 0 ? RespEncoder::integer(rank) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("ZREVRANK", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zrevrank' command");
        }
        int64_t rank = -1;
        bool key_found = false;
        std::string err;
        if(!store->zrank(ctx.db, req.array[1].str, req.array[2].str, true, rank, key_found, &err)) {
            return RespEncoder::err(err);
        }
        return rank >= 0 ? RespEncoder::integer(rank) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("ROLE", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'role' command");
        }
        return makeRoleReply(ctx);
    });

    dispatch->addCommand("SLAVEOF", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'slaveof' command");
        }
        ReplicationManager* repl = replicationFromCtx(ctx);
        if(!repl) {
            return RespEncoder::err("ERR replication unavailable");
        }
        if(upperArg(req.array[1].str) == "NO" && upperArg(req.array[2].str) == "ONE") {
            repl->promoteToMaster();
            return RespEncoder::ok();
        }
        int64_t port = 0;
        if(!parseInt64Arg(req.array[2], port) || port <= 0 || port > 65535) {
            return RespEncoder::err("ERR invalid port");
        }
        repl->setSlaveOf(req.array[1].str, (int)port);
        return RespEncoder::ok();
    });

    dispatch->addCommand("PSYNC", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'psync' command");
        }
        ReplicationManager* repl = replicationFromCtx(ctx);
        if(!repl) {
            return RespEncoder::err("ERR replication unavailable");
        }
        int64_t offset = 0;
        if(!parseInt64Arg(req.array[2], offset)) {
            return RespEncoder::err("ERR invalid replication offset");
        }
        ReplicationManager::PsyncResult result =
            repl->handlePsync(req.array[1].str, offset, store);
        if(result.kind == ReplicationManager::PsyncResult::Kind::Error) {
            return result.line;
        }
        KvSession::ptr session = ctx.session.lock();
        if(session) {
            session->appendResponse(result.line);
            if(result.kind == ReplicationManager::PsyncResult::Kind::Full) {
                session->appendResponse(RespEncoder::bulk(result.payload));
                repl->registerReplica(session);
                ctx.replica_connection = true;
            } else if(!result.payload.empty()) {
                session->pushReplicationPayload(result.payload);
                repl->registerReplica(session);
                ctx.replica_connection = true;
            }
            return RespEncoder::none();
        }
        return result.line;
    });

    dispatch->addCommand("SAVE", [](KvContext&, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'save' command");
        }
        std::string err;
        if(!store->saveRdb(&err)) {
            return RespEncoder::err(err.empty() ? "ERR save failed" : err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("LASTSAVE", [](KvContext&, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'lastsave' command");
        }
        return RespEncoder::integer(store->lastSaveUnixSec());
    });

    dispatch->addCommand("BGREWRITEAOF", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'bgrewriteaof' command");
        }
        AofLog::ptr aof = getAofForConfig();
        if(!aof || !aof->isEnabled()) {
            return RespEncoder::err("ERR AOF disabled");
        }
        if(!g_bgrewriteaof_runner || !g_bgrewriteaof_runner()) {
            return RespEncoder::err("ERR Background AOF rewrite already in progress");
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("SUBSCRIBE", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'subscribe' command");
        }
        if(!ctx.pubsub) {
            return RespEncoder::err("ERR Pub/Sub unavailable");
        }
        KvSession::ptr session = ctx.session.lock();
        if(!session) {
            return RespEncoder::err("ERR session unavailable");
        }
        ctx.pubsub_mode = true;
        RespValue out;
        out.type = RespType::Array;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                continue;
            }
            const std::string& channel = req.array[i].str;
            ctx.pubsub->subscribe(channel, session);
            const int64_t count = ctx.pubsub->subscriptionCount(session);
            out.array.push_back(makePubSubMetaReply("subscribe", channel, count));
        }
        return out;
    });

    dispatch->addCommand("UNSUBSCRIBE", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(!ctx.pubsub) {
            return RespEncoder::err("ERR Pub/Sub unavailable");
        }
        KvSession::ptr session = ctx.session.lock();
        if(!session) {
            return RespEncoder::err("ERR session unavailable");
        }
        RespValue out;
        out.type = RespType::Array;
        if(req.array.size() == 1) {
            ctx.pubsub->unsubscribeAll(session);
            out.array.push_back(makePubSubMetaReply("unsubscribe", "", 0));
            if(ctx.pubsub->subscriptionCount(session) == 0) {
                ctx.pubsub_mode = false;
            }
            return out;
        }
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                continue;
            }
            const std::string& channel = req.array[i].str;
            ctx.pubsub->unsubscribe(channel, session);
            const int64_t count = ctx.pubsub->subscriptionCount(session);
            out.array.push_back(makePubSubMetaReply("unsubscribe", channel, count));
        }
        if(ctx.pubsub->subscriptionCount(session) == 0) {
            ctx.pubsub_mode = false;
        }
        return out;
    });

    dispatch->addCommand("PSUBSCRIBE", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'psubscribe' command");
        }
        if(!ctx.pubsub) {
            return RespEncoder::err("ERR Pub/Sub unavailable");
        }
        KvSession::ptr session = ctx.session.lock();
        if(!session) {
            return RespEncoder::err("ERR session unavailable");
        }
        ctx.pubsub_mode = true;
        RespValue out;
        out.type = RespType::Array;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                continue;
            }
            const std::string& pattern = req.array[i].str;
            ctx.pubsub->psubscribe(pattern, session);
            const int64_t count = ctx.pubsub->subscriptionCount(session);
            out.array.push_back(makePubSubMetaReply("psubscribe", pattern, count));
        }
        return out;
    });

    dispatch->addCommand("PUNSUBSCRIBE", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(!ctx.pubsub) {
            return RespEncoder::err("ERR Pub/Sub unavailable");
        }
        KvSession::ptr session = ctx.session.lock();
        if(!session) {
            return RespEncoder::err("ERR session unavailable");
        }
        RespValue out;
        out.type = RespType::Array;
        if(req.array.size() == 1) {
            const int64_t before = ctx.pubsub->subscriptionCount(session);
            ctx.pubsub->punsubscribeAll(session);
            out.array.push_back(makePubSubMetaReply("punsubscribe", "", 0));
            if(before > 0 && ctx.pubsub->subscriptionCount(session) == 0) {
                ctx.pubsub_mode = false;
            }
            return out;
        }
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                continue;
            }
            const std::string& pattern = req.array[i].str;
            ctx.pubsub->punsubscribe(pattern, session);
            const int64_t count = ctx.pubsub->subscriptionCount(session);
            out.array.push_back(makePubSubMetaReply("punsubscribe", pattern, count));
        }
        if(ctx.pubsub->subscriptionCount(session) == 0) {
            ctx.pubsub_mode = false;
        }
        return out;
    });

    dispatch->addCommand("PUBLISH", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'publish' command");
        }
        if(!ctx.pubsub) {
            return RespEncoder::integer(0);
        }
        const int64_t n = ctx.pubsub->publish(req.array[1].str, req.array[2].str);
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("AUTH", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'auth' command");
        }
        if(!isAuthRequired() || req.array[1].str == getRequirePass()) {
            ctx.authenticated = true;
            return RespEncoder::ok();
        }
        return RespEncoder::err("ERR invalid password");
    });

    dispatch->addCommand("APPEND", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'append' command");
        }
        int64_t len = 0;
        std::string err;
        if(!store->append(ctx.db, req.array[1].str, req.array[2].str, len, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(len);
    });

    dispatch->addCommand("STRLEN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'strlen' command");
        }
        int64_t len = 0;
        bool found = false;
        std::string err;
        if(!store->strlen(ctx.db, req.array[1].str, len, found, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(found ? len : 0);
    });

    dispatch->addCommand("HINCRBY", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hincrby' command");
        }
        int64_t delta = 0;
        if(!parseInt64Arg(req.array[3], delta)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        int64_t out = 0;
        std::string err;
        if(!store->hincrby(ctx.db, req.array[1].str, req.array[2].str, delta, out, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(out);
    });

    dispatch->addCommand("LINDEX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lindex' command");
        }
        int64_t index = 0;
        if(!parseInt64Arg(req.array[2], index)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listIndex(ctx.db, req.array[1].str, index, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(value) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("LTRIM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'ltrim' command");
        }
        int64_t start = 0;
        int64_t stop = 0;
        if(!parseInt64Arg(req.array[2], start) || !parseInt64Arg(req.array[3], stop)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        std::string err;
        const int64_t rc = store->listTrim(ctx.db, req.array[1].str, start, stop, &err);
        if(rc < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("LSET", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lset' command");
        }
        int64_t index = 0;
        if(!parseInt64Arg(req.array[2], index)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        std::string err;
        if(!store->listSet(ctx.db, req.array[1].str, index, req.array[3].str, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("LREM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'lrem' command");
        }
        int64_t count = 0;
        if(!parseInt64Arg(req.array[2], count)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        std::string err;
        const int64_t rc = store->listRem(ctx.db, req.array[1].str, count, req.array[3].str, &err);
        if(rc < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(rc);
    });

    dispatch->addCommand("RPOPLPUSH", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'rpoplpush' command");
        }
        std::string value;
        bool found = false;
        std::string err;
        if(!store->listRPopLPush(ctx.db, req.array[1].str, req.array[2].str, value, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(value) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("SINTER", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'sinter' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::vector<std::string> members;
        std::string err;
        if(!store->setInter(ctx.db, keys, members, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& m : members) {
            arr.array.push_back(RespEncoder::bulk(m));
        }
        return arr;
    });

    dispatch->addCommand("SUNION", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'sunion' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::vector<std::string> members;
        std::string err;
        if(!store->setUnion(ctx.db, keys, members, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& m : members) {
            arr.array.push_back(RespEncoder::bulk(m));
        }
        return arr;
    });

    dispatch->addCommand("SDIFF", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'sdiff' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 1; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::vector<std::string> members;
        std::string err;
        if(!store->setDiff(ctx.db, keys, members, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& m : members) {
            arr.array.push_back(RespEncoder::bulk(m));
        }
        return arr;
    });

    dispatch->addCommand("RENAME", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'rename' command");
        }
        std::string err;
        const int rc = store->renameKey(ctx.db, req.array[1].str, req.array[2].str, false, &err);
        if(rc == -2) {
            return RespEncoder::err(err.empty() ? "ERR no such key" : err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("RENAMENX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'renamenx' command");
        }
        std::string err;
        const int rc = store->renameKey(ctx.db, req.array[1].str, req.array[2].str, true, &err);
        if(rc == 0) {
            return RespEncoder::integer(0);
        }
        if(rc == -2) {
            return RespEncoder::err(err.empty() ? "ERR no such key" : err);
        }
        return RespEncoder::integer(1);
    });

    dispatch->addCommand("BGSAVE", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'bgsave' command");
        }
        if(!g_bgsave_runner || !g_bgsave_runner()) {
            return RespEncoder::err("ERR Background save already in progress");
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("XADD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xadd' command");
        }
        if((req.array.size() - 3) % 2 != 0) {
            return RespEncoder::err("ERR syntax error");
        }
        std::vector<std::pair<std::string, std::string>> fields;
        for(size_t i = 3; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            fields.emplace_back(req.array[i].str, req.array[i + 1].str);
        }
        std::string id_out;
        std::string err;
        if(!store->xadd(ctx.db, req.array[1].str, req.array[2].str, fields, id_out, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::bulk(id_out);
    });

    dispatch->addCommand("XLEN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xlen' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->xlen(ctx.db, req.array[1].str, found, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("XRANGE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xrange' command");
        }
        int64_t count = -1;
        if(req.array.size() >= 5) {
            if(!parseInt64Arg(req.array[4], count)) {
                return RespEncoder::err("ERR value is not an integer or out of range");
            }
        }
        std::vector<KvStore::StreamEntry> entries;
        bool found = false;
        std::string err;
        if(!store->xrange(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str, count, false,
                          entries, found, &err)) {
            return RespEncoder::err(err);
        }
        return encodeStreamEntries(entries);
    });

    dispatch->addCommand("XREVRANGE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xrevrange' command");
        }
        int64_t count = -1;
        if(req.array.size() >= 5) {
            if(!parseInt64Arg(req.array[4], count)) {
                return RespEncoder::err("ERR value is not an integer or out of range");
            }
        }
        std::vector<KvStore::StreamEntry> entries;
        bool found = false;
        std::string err;
        if(!store->xrange(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str, count, true,
                          entries, found, &err)) {
            return RespEncoder::err(err);
        }
        return encodeStreamEntries(entries);
    });

    dispatch->addCommand("XREAD", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4) {
            return RespEncoder::err("ERR wrong number of arguments for 'xread' command");
        }
        int64_t count = -1;
        size_t key_start = 1;
        if(req.array[1].type == RespType::BulkString && upperArg(req.array[1].str) == "COUNT") {
            if(req.array.size() < 5 || !parseInt64Arg(req.array[2], count)) {
                return RespEncoder::err("ERR syntax error");
            }
            key_start = 3;
        }
        if(req.array.size() < key_start + 2 || req.array[key_start].is_null
           || upperArg(req.array[key_start].str) != "STREAMS") {
            return RespEncoder::err("ERR syntax error");
        }
        const size_t mid = (req.array.size() - key_start - 1) / 2 + key_start;
        if(mid <= key_start || mid >= req.array.size()) {
            return RespEncoder::err("ERR syntax error");
        }
        std::vector<std::string> keys;
        std::vector<std::string> ids;
        for(size_t i = key_start + 1; i < mid; ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        for(size_t i = mid; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            ids.push_back(req.array[i].str);
        }
        std::vector<std::pair<std::string, std::vector<KvStore::StreamEntry>>> result;
        std::string err;
        if(!store->xread(ctx.db, keys, ids, count, result, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& item : result) {
            RespValue row;
            row.type = RespType::Array;
            row.array.push_back(RespEncoder::bulk(item.first));
            row.array.push_back(encodeStreamEntries(item.second));
            arr.array.push_back(row);
        }
        return arr;
    });

    dispatch->addCommand("XDEL", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xdel' command");
        }
        std::vector<std::string> ids;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            ids.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->xdel(ctx.db, req.array[1].str, ids, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("GETEX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'getex' command");
        }
        int64_t ex = 0, px = 0;
        bool persist = false;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            std::string opt = upperArg(req.array[i].str);
            if(opt == "PERSIST") {
                persist = true;
            } else if((opt == "EX" || opt == "PX") && i + 1 < req.array.size()) {
                int64_t n = 0;
                if(!parseInt64Arg(req.array[i + 1], n)) {
                    return RespEncoder::err("ERR value is not an integer or out of range");
                }
                if(opt == "EX") {
                    ex = n;
                } else {
                    px = n;
                }
                ++i;
            } else {
                return RespEncoder::err("ERR syntax error");
            }
        }
        std::string val;
        bool found = false;
        std::string err;
        if(!store->getex(ctx.db, req.array[1].str, ex, px, persist, val, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(val) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("SETNX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'setnx' command");
        }
        const int rc = store->setOpt(ctx.db, req.array[1].str, req.array[2].str, true, false, -1, -1);
        return RespEncoder::integer(rc ? 1 : 0);
    });

    dispatch->addCommand("HSETNX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'hsetnx' command");
        }
        std::string err;
        const int rc = store->hsetnx(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str, &err);
        if(rc < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(rc);
    });

    dispatch->addCommand("SPOP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'spop' command");
        }
        std::string member;
        bool found = false;
        std::string err;
        if(!store->spop(ctx.db, req.array[1].str, member, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(member) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("ZPOPMIN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zpopmin' command");
        }
        std::string member;
        double score = 0;
        bool found = false;
        std::string err;
        if(!store->zpopmin(ctx.db, req.array[1].str, member, score, found, &err)) {
            return RespEncoder::err(err);
        }
        if(!found) {
            RespValue empty;
            empty.type = RespType::Array;
            return empty;
        }
        RespValue arr;
        arr.type = RespType::Array;
        arr.array.push_back(RespEncoder::bulk(member));
        arr.array.push_back(RespEncoder::bulk(std::to_string(score)));
        return arr;
    });

    dispatch->addCommand("ZPOPMAX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zpopmax' command");
        }
        std::string member;
        double score = 0;
        bool found = false;
        std::string err;
        if(!store->zpopmax(ctx.db, req.array[1].str, member, score, found, &err)) {
            return RespEncoder::err(err);
        }
        if(!found) {
            RespValue empty;
            empty.type = RespType::Array;
            return empty;
        }
        RespValue arr;
        arr.type = RespType::Array;
        arr.array.push_back(RespEncoder::bulk(member));
        arr.array.push_back(RespEncoder::bulk(std::to_string(score)));
        return arr;
    });

    dispatch->addCommand("CLIENT", [](KvContext& ctx, const RespValue& req, KvStore::ptr) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'client' command");
        }
        std::string sub = upperArg(req.array[1].str);
        if(sub == "ID") {
            return RespEncoder::integer(ctx.client_id);
        }
        if(sub == "SETNAME") {
            if(req.array.size() != 3 || req.array[2].is_null) {
                return RespEncoder::err("ERR wrong number of arguments for 'client setname' command");
            }
            ctx.client_name = req.array[2].str;
            return RespEncoder::ok();
        }
        if(sub == "LIST") {
            if(!g_client_list_fn) {
                return RespEncoder::bulk("");
            }
            const auto clients = g_client_list_fn();
            std::ostringstream ss;
            for(const auto& c : clients) {
                ss << "id=" << c.id << " addr=" << c.addr << " name=" << c.name << " db=" << ctx.db << "\n";
            }
            return RespEncoder::bulk(ss.str());
        }
        if(sub == "GETNAME") {
            return RespEncoder::bulk(ctx.client_name);
        }
        if(sub == "KILL") {
            return RespEncoder::integer(0);
        }
        return RespEncoder::err("ERR unknown CLIENT subcommand");
    });

    dispatch->addCommand("XGROUP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xgroup' command");
        }
        std::string sub = upperArg(req.array[1].str);
        if(sub == "CREATE") {
            bool mkstream = false;
            size_t id_idx = 4;
            if(req.array.size() >= 5 && !req.array[4].is_null
               && upperArg(req.array[4].str) == "MKSTREAM") {
                mkstream = true;
                id_idx = 5;
            }
            if(req.array.size() <= id_idx || req.array[id_idx].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            std::string err;
            if(!store->xgroupCreate(ctx.db, req.array[2].str, req.array[3].str,
                                    req.array[id_idx].str, mkstream, &err)) {
                return RespEncoder::err(err);
            }
            return RespEncoder::ok();
        }
        if(sub == "DESTROY") {
            std::string err;
            if(!store->xgroupDestroy(ctx.db, req.array[2].str, req.array[3].str, &err)) {
                return RespEncoder::err(err);
            }
            return RespEncoder::ok();
        }
        return RespEncoder::err("ERR unknown XGROUP subcommand");
    });

    dispatch->addCommand("XREADGROUP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 6) {
            return RespEncoder::err("ERR wrong number of arguments for 'xreadgroup' command");
        }
	        if(upperArg(req.array[1].str) != "GROUP" || req.array[2].is_null || req.array[3].is_null) {
	            return RespEncoder::err("ERR syntax error");
	        }
        const std::string& group = req.array[2].str;
        const std::string& consumer = req.array[3].str;
        int64_t count = -1;
	        size_t streams_idx = 4;
	        if(req.array.size() > 5 && upperArg(req.array[4].str) == "COUNT") {
	            if(req.array.size() < 7 || !parseInt64Arg(req.array[5], count)) {
	                return RespEncoder::err("ERR syntax error");
	            }
	            streams_idx = 6;
	        }
        if(req.array.size() <= streams_idx || upperArg(req.array[streams_idx].str) != "STREAMS") {
            return RespEncoder::err("ERR syntax error");
        }
        const size_t mid = streams_idx + 1 + (req.array.size() - streams_idx - 1) / 2;
        std::vector<std::string> keys, ids;
        for(size_t i = streams_idx + 1; i < mid; ++i) {
            keys.push_back(req.array[i].str);
        }
        for(size_t i = mid; i < req.array.size(); ++i) {
            ids.push_back(req.array[i].str);
        }
        std::vector<std::pair<std::string, std::vector<KvStore::StreamEntry>>> result;
        std::string err;
        if(!store->xreadGroup(ctx.db, group, consumer, keys, ids, count, result, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& item : result) {
            RespValue row;
            row.type = RespType::Array;
            row.array.push_back(RespEncoder::bulk(item.first));
            row.array.push_back(encodeStreamEntries(item.second));
            arr.array.push_back(row);
        }
        return arr;
    });

    dispatch->addCommand("XACK", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xack' command");
        }
        std::vector<std::string> ids;
        for(size_t i = 3; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            ids.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->xack(ctx.db, req.array[1].str, req.array[2].str, ids, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("QUIT", [](KvContext& ctx, const RespValue&, KvStore::ptr) {
        ctx.quit = true;
        return RespEncoder::ok();
    });

    // ============================================================
    // Lua Scripting 命令
    // ============================================================
    static LuaEngine::ptr g_luaEngine;

    auto ensureLua = []() -> LuaEngine::ptr {
        if (!g_luaEngine) {
            g_luaEngine = std::make_shared<LuaEngine>();
            // Apply cached lua-global-atomic config
            g_luaEngine->setGlobalAtomic(g_lua_global_atomic_cfg);
            // Bind for CONFIG GET/SET lua-global-atomic
            bindLuaEngineForConfig([]() -> LuaEngine::ptr { return g_luaEngine; });
        }
        return g_luaEngine;
    };

    // EVAL script numkeys key [key ...] arg [arg ...]
    dispatch->addCommand("EVAL", [dispatch, ensureLua](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if (req.array.size() < 3) {
            return RespEncoder::err("ERR wrong number of arguments for 'eval' command");
        }
        auto lua = ensureLua();
        std::string script = req.array[1].is_null ? "" : req.array[1].str;
        long long numkeys = strtoll(req.array[2].str.c_str(), nullptr, 10);
        if (numkeys < 0) {
            numkeys = 0;
        }

        std::vector<std::string> keys, argv;
        size_t idx = 3;
        for (long long i = 0; i < numkeys && idx < req.array.size(); ++i, ++idx) {
            keys.push_back(req.array[idx].is_null ? "" : req.array[idx].str);
        }
        for (; idx < req.array.size(); ++idx) {
            argv.push_back(req.array[idx].is_null ? "" : req.array[idx].str);
        }

        if (script.empty()) {
            return RespEncoder::err("ERR invalid script");
        }

        CommandDispatch* dispatchPtr = dispatch.get();
        RespValue result = lua->eval(script, keys, argv, store, dispatchPtr, &ctx);
        return result;
    });

    // EVALSHA sha1 numkeys key [key ...] arg [arg ...]
    dispatch->addCommand("EVALSHA", [dispatch, ensureLua](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if (req.array.size() < 3) {
            return RespEncoder::err("ERR wrong number of arguments for 'evalsha' command");
        }
        auto lua = ensureLua();
        std::string sha1 = req.array[1].is_null ? "" : req.array[1].str;

        // 查找缓存的脚本 body
        std::string script = lua->getScriptBody(sha1);
        if (script.empty()) {
            return RespEncoder::err("NOSCRIPT No matching script. Please use EVAL.");
        }

        long long numkeys = strtoll(req.array[2].str.c_str(), nullptr, 10);
        if (numkeys < 0) numkeys = 0;

        std::vector<std::string> keys, argv;
        size_t idx = 3;
        for (long long i = 0; i < numkeys && idx < req.array.size(); ++i, ++idx) {
            keys.push_back(req.array[idx].is_null ? "" : req.array[idx].str);
        }
        for (; idx < req.array.size(); ++idx) {
            argv.push_back(req.array[idx].is_null ? "" : req.array[idx].str);
        }

        CommandDispatch* dispatchPtr = dispatch.get();
        RespValue result = lua->eval(script, keys, argv, store, dispatchPtr, &ctx);
        return result;
    });

    // SCRIPT LOAD script
    dispatch->addCommand("SCRIPT", [ensureLua](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if (req.array.size() < 2) {
            return RespEncoder::err("ERR wrong number of arguments for 'script' command");
        }
        auto lua = ensureLua();
        std::string subCmd = req.array[1].str;
        for (char& c : subCmd) { c = (char)std::toupper((unsigned char)c); }

        if (subCmd == "LOAD") {
            if (req.array.size() < 3) {
                return RespEncoder::err("ERR wrong number of arguments for 'script|load' command");
            }
            std::string script = req.array[2].str;
            std::string sha1 = lua->scriptLoad(script);
            return RespEncoder::bulk(sha1);
        }
        else if (subCmd == "EXISTS") {
            std::vector<std::string> sha1s;
            for (size_t i = 2; i < req.array.size(); ++i) {
                sha1s.push_back(req.array[i].str);
            }
            auto result = lua->scriptExists(sha1s);
            RespValue arr;
            arr.type = RespType::Array;
            for (bool b : result) {
                arr.array.push_back(RespEncoder::integer(b ? 1 : 0));
            }
            return arr;
        }
        else if (subCmd == "KILL") {
            lua->scriptKill();
            return RespEncoder::ok();
        }
        else if (subCmd == "FLUSH") {
            lua->scriptFlush();
            return RespEncoder::ok();
        }
        else {
            return RespEncoder::err("ERR Unknown SCRIPT subcommand: " + subCmd);
        }
    });

    registerP0P1Commands(dispatch);
}

} // namespace kv
} // namespace zero

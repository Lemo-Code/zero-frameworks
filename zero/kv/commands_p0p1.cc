/**
 * @file commands_p0p1.cc
 * @brief 基础命令实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/command_dispatch.h"
#include "zero/kv/slowlog.h"
#include "zero/kv/blocking/blocking_list_hub.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/concurrency/scheduler.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <sys/time.h>

namespace zero {
namespace kv {

namespace {

std::function<void(bool save)> g_shutdown_fn;

std::string upperArg(const std::string& s) {
    std::string out = s;
    for(char& c : out) {
        c = (char)std::toupper((unsigned char)c);
    }
    return out;
}

bool parseInt64Arg(const RespValue& v, int64_t& out) {
    if(v.type != RespType::BulkString || v.is_null) {
        return false;
    }
    char* end = nullptr;
    long long n = std::strtoll(v.str.c_str(), &end, 10);
    if(end == v.str.c_str()) {
        return false;
    }
    out = (int64_t)n;
    return true;
}

bool parseDoubleArg(const RespValue& v, double& out) {
    if(v.type != RespType::BulkString || v.is_null) {
        return false;
    }
    char* end = nullptr;
    out = std::strtod(v.str.c_str(), &end);
    return end != v.str.c_str() && !std::isnan(out);
}

std::string formatScore(double score) {
    std::ostringstream ss;
    ss << score;
    return ss.str();
}

RespValue encodeCollectionScan(const CollectionScanResult& sr) {
    RespValue arr;
    arr.type = RespType::Array;
    arr.array.push_back(RespEncoder::bulk(std::to_string(sr.cursor)));
    RespValue items;
    items.type = RespType::Array;
    for(const auto& item : sr.items) {
        items.array.push_back(RespEncoder::bulk(item));
    }
    arr.array.push_back(items);
    return arr;
}

RespValue dispatchCollectionScan(KvContext& ctx, const RespValue& req, KvStore::ptr store,
                                 const std::string& cmd,
                                 CollectionScanResult (KvStore::*fn)(int, const std::string&, uint64_t,
                                                                    const std::string&, int,
                                                                    std::string*)) {
    if(req.array.size() < 3 || req.array[1].is_null || req.array[2].is_null) {
        return RespEncoder::err("ERR wrong number of arguments for '" + cmd + "' command");
    }
    int64_t cursor_val = 0;
    if(!parseInt64Arg(req.array[2], cursor_val) || cursor_val < 0) {
        return RespEncoder::err("ERR invalid cursor");
    }
    std::string pattern = "*";
    int64_t count = 10;
    for(size_t i = 3; i < req.array.size(); ++i) {
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
    std::string err;
    CollectionScanResult sr = (store.get()->*fn)(ctx.db, req.array[1].str, (uint64_t)cursor_val,
                                                 pattern, (int)count, &err);
    if(!err.empty()) {
        return RespEncoder::err(err);
    }
    return encodeCollectionScan(sr);
}

RespValue dispatchZrangeByScore(KvContext& ctx, const RespValue& req, KvStore::ptr store,
                                bool reverse, const std::string& cmd) {
    if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null
       || req.array[3].is_null) {
        return RespEncoder::err("ERR wrong number of arguments for '" + cmd + "' command");
    }
    bool withscores = false;
    int64_t offset = 0;
    int64_t count = -1;
    for(size_t i = 4; i < req.array.size(); ++i) {
        if(req.array[i].is_null) {
            return RespEncoder::err("ERR syntax error");
        }
        const std::string opt = upperArg(req.array[i].str);
        if(opt == "WITHSCORES") {
            withscores = true;
        } else if(opt == "LIMIT") {
            if(i + 2 >= req.array.size() || !parseInt64Arg(req.array[i + 1], offset)
               || !parseInt64Arg(req.array[i + 2], count)) {
                return RespEncoder::err("ERR syntax error");
            }
            i += 2;
        } else {
            return RespEncoder::err("ERR syntax error");
        }
    }
    std::vector<std::pair<std::string, double>> out;
    bool found = false;
    std::string err;
    if(!store->zrangeByScore(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str, reverse,
                             offset, count, withscores, out, found, &err)) {
        return RespEncoder::err(err);
    }
    RespValue arr;
    arr.type = RespType::Array;
    for(const auto& item : out) {
        arr.array.push_back(RespEncoder::bulk(item.first));
        if(withscores) {
            arr.array.push_back(RespEncoder::bulk(formatScore(item.second)));
        }
    }
    return arr;
}

RespValue tryBlockingRPopLPush(KvContext& ctx, const RespValue& req, KvStore::ptr store) {
    if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null
       || req.array[3].is_null) {
        return RespEncoder::err("ERR wrong number of arguments for 'brpoplpush' command");
    }
    const std::string& src = req.array[1].str;
    const std::string& dst = req.array[2].str;
    double timeout = 0;
    if(!parseDoubleArg(req.array[3], timeout) || timeout < 0) {
        return RespEncoder::err("ERR timeout is not a float or out of range");
    }
    std::string value;
    bool found = false;
    std::string err;
    if(store->listRPopLPush(ctx.db, src, dst, value, found, &err)) {
        if(found) {
            return RespEncoder::bulk(value);
        }
    } else if(!err.empty()) {
        return RespEncoder::err(err);
    }
    if(timeout == 0) {
        return RespEncoder::nullBulk();
    }
    BlockingListHub::ptr hub = getBlockingListHub();
    if(!hub || !zero::Scheduler::GetThis()) {
        return RespEncoder::nullBulk();
    }
    int64_t deadline_ms = KvStore::nowMs() + (int64_t)(timeout * 1000.0);
    ListWaiter waiter;
    waiter.session = ctx.session;
    waiter.db = ctx.db;
    waiter.keys = {src};
    waiter.pop_right = true;
    waiter.deadline_ms = deadline_ms;
    waiter.scheduler = zero::Scheduler::GetThis();
    waiter.fiber = zero::Fiber::GetThis();
    hub->addWaiter(&waiter);
    while(!waiter.done) {
        if(KvStore::nowMs() >= deadline_ms) {
            hub->removeWaiter(&waiter);
            return RespEncoder::nullBulk();
        }
        zero::Fiber::YieldToHold();
    }
    hub->removeWaiter(&waiter);
    if(waiter.result_value.empty()) {
        return RespEncoder::nullBulk();
    }
    const int64_t n = store->listPush(ctx.db, dst, {waiter.result_value}, true, &err);
    if(n < 0) {
        return RespEncoder::err(err);
    }
    hub->onPush(ctx.db, dst, store);
    return RespEncoder::bulk(waiter.result_value);
}

RespValue slowlogGet(const RespValue& req) {
    int64_t count = 10;
    if(req.array.size() >= 3 && !req.array[2].is_null) {
        if(!parseInt64Arg(req.array[2], count)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
    }
    const auto entries = getSlowlog().get(count);
    RespValue arr;
    arr.type = RespType::Array;
    for(const auto& e : entries) {
        RespValue row;
        row.type = RespType::Array;
        row.array.push_back(RespEncoder::integer(e.id));
        row.array.push_back(RespEncoder::integer(e.timestamp_ms / 1000));
        row.array.push_back(RespEncoder::integer(e.duration_us));
        RespValue args;
        args.type = RespType::Array;
        for(const auto& arg : e.args) {
            args.array.push_back(RespEncoder::bulk(arg));
        }
        row.array.push_back(args);
        row.array.push_back(RespEncoder::integer(e.client_id));
        row.array.push_back(RespEncoder::bulk(e.client_addr));
        arr.array.push_back(row);
    }
    return arr;
}

} // namespace

void bindShutdownForConfig(std::function<void(bool save)> fn) {
    g_shutdown_fn = std::move(fn);
}

void registerP0P1Commands(CommandDispatch::ptr dispatch) {
    dispatch->addCommand("MSETNX", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array.size() % 2 == 0) {
            return RespEncoder::err("ERR wrong number of arguments for 'msetnx' command");
        }
        std::vector<std::pair<std::string, std::string>> pairs;
        for(size_t i = 1; i + 1 < req.array.size(); i += 2) {
            if(req.array[i].is_null || req.array[i + 1].is_null) {
                return RespEncoder::err("ERR syntax error in MSETNX");
            }
            pairs.emplace_back(req.array[i].str, req.array[i + 1].str);
        }
        std::string err;
        return RespEncoder::integer(store->msetnx(ctx.db, pairs, &err));
    });

    dispatch->addCommand("INCRBYFLOAT", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'incrbyfloat' command");
        }
        double delta = 0;
        if(!parseDoubleArg(req.array[2], delta)) {
            return RespEncoder::err("ERR value is not a valid float");
        }
        std::string out;
        std::string err;
        if(!store->incrByFloat(ctx.db, req.array[1].str, delta, out, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::bulk(out);
    });

    dispatch->addCommand("LINSERT", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 5 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null || req.array[4].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'linsert' command");
        }
        const std::string where = upperArg(req.array[2].str);
        bool before = where == "BEFORE";
        if(!before && where != "AFTER") {
            return RespEncoder::err("ERR syntax error");
        }
        std::string err;
        const int64_t n = store->listInsert(ctx.db, req.array[1].str, before, req.array[3].str,
                                            req.array[4].str, &err);
        if(n == -1) {
            return RespEncoder::err(err);
        }
        if(n == -2) {
            return RespEncoder::integer(0);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("BRPOPLPUSH", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return tryBlockingRPopLPush(ctx, req, store);
    });

    dispatch->addCommand("ZRANGEBYSCORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchZrangeByScore(ctx, req, store, false, "zrangebyscore");
    });

    dispatch->addCommand("ZREVRANGEBYSCORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchZrangeByScore(ctx, req, store, true, "zrevrangebyscore");
    });

    dispatch->addCommand("ZCOUNT", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'zcount' command");
        }
        bool found = false;
        std::string err;
        const int64_t n = store->zcount(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str,
                                        found, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("SINTERSTORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'sinterstore' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->setInterStore(ctx.db, req.array[1].str, keys, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("SUNIONSTORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'sunionstore' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->setUnionStore(ctx.db, req.array[1].str, keys, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("SDIFFSTORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'sdiffstore' command");
        }
        std::vector<std::string> keys;
        for(size_t i = 2; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            keys.push_back(req.array[i].str);
        }
        std::string err;
        const int64_t n = store->setDiffStore(ctx.db, req.array[1].str, keys, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("HSCAN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchCollectionScan(ctx, req, store, "hscan", &KvStore::hscan);
    });

    dispatch->addCommand("SSCAN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchCollectionScan(ctx, req, store, "sscan", &KvStore::sscan);
    });

    dispatch->addCommand("ZSCAN", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        return dispatchCollectionScan(ctx, req, store, "zscan", &KvStore::zscan);
    });

    dispatch->addCommand("XTRIM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xtrim' command");
        }
        if(upperArg(req.array[2].str) != "MAXLEN") {
            return RespEncoder::err("ERR syntax error");
        }
        bool approximate = false;
        size_t count_idx = 3;
        if(upperArg(req.array[3].str) == "~") {
            approximate = true;
            count_idx = 4;
        }
        int64_t maxlen = 0;
        if(req.array.size() <= count_idx || !parseInt64Arg(req.array[count_idx], maxlen)) {
            return RespEncoder::err("ERR syntax error");
        }
        std::string err;
        const int64_t n = store->xtrim(ctx.db, req.array[1].str, maxlen, approximate, &err);
        if(n < 0) {
            return RespEncoder::err(err);
        }
        return RespEncoder::integer(n);
    });

    dispatch->addCommand("XPENDING", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 3 || req.array[1].is_null || req.array[2].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xpending' command");
        }
        const std::string& key = req.array[1].str;
        const std::string& group = req.array[2].str;
        if(req.array.size() == 3) {
            KvStore::XPendingSummary summary;
            std::string err;
            if(!store->xpendingSummary(ctx.db, key, group, summary, &err)) {
                return RespEncoder::err(err);
            }
            RespValue arr;
            arr.type = RespType::Array;
            arr.array.push_back(RespEncoder::integer(summary.count));
            arr.array.push_back(RespEncoder::bulk(summary.min_id.empty() ? "0-0" : summary.min_id));
            arr.array.push_back(RespEncoder::bulk(summary.max_id.empty() ? "0-0" : summary.max_id));
            RespValue consumers;
            consumers.type = RespType::Array;
            for(const auto& cp : summary.consumers) {
                RespValue row;
                row.type = RespType::Array;
                row.array.push_back(RespEncoder::bulk(cp.first));
                row.array.push_back(RespEncoder::integer(cp.second));
                consumers.array.push_back(row);
            }
            arr.array.push_back(consumers);
            return arr;
        }
        if(req.array.size() >= 6) {
            int64_t count = 10;
            std::string consumer_filter;
            size_t idx = 3;
            if(!req.array[3].is_null && req.array[3].str == "-") {
                ++idx;
            }
            if(req.array.size() > idx + 1) {
                parseInt64Arg(req.array[idx + 1], count);
            }
            if(req.array.size() > idx + 2 && !req.array[idx + 2].is_null) {
                consumer_filter = req.array[idx + 2].str;
            }
            std::vector<KvStore::XPendingDetail> details;
            std::string err;
            const std::string start = req.array.size() > 3 && !req.array[3].is_null
                                          ? req.array[3].str
                                          : "-";
            const std::string end = req.array.size() > 4 && !req.array[4].is_null
                                        ? req.array[4].str
                                        : "+";
            if(!store->xpendingRange(ctx.db, key, group, start, end, count, consumer_filter,
                                     details, &err)) {
                return RespEncoder::err(err);
            }
            RespValue arr;
            arr.type = RespType::Array;
            for(const auto& d : details) {
                RespValue row;
                row.type = RespType::Array;
                row.array.push_back(RespEncoder::bulk(d.id));
                row.array.push_back(RespEncoder::bulk(d.consumer));
                row.array.push_back(RespEncoder::integer(d.idle_ms));
                row.array.push_back(RespEncoder::integer(d.delivery_count));
                arr.array.push_back(row);
            }
            return arr;
        }
        KvStore::XPendingSummary summary;
        std::string err;
        if(!store->xpendingSummary(ctx.db, key, group, summary, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        arr.array.push_back(RespEncoder::integer(summary.count));
        arr.array.push_back(RespEncoder::bulk(summary.min_id.empty() ? "0-0" : summary.min_id));
        arr.array.push_back(RespEncoder::bulk(summary.max_id.empty() ? "0-0" : summary.max_id));
        RespValue consumers;
        consumers.type = RespType::Array;
        for(const auto& cp : summary.consumers) {
            RespValue row;
            row.type = RespType::Array;
            row.array.push_back(RespEncoder::bulk(cp.first));
            row.array.push_back(RespEncoder::integer(cp.second));
            consumers.array.push_back(row);
        }
        arr.array.push_back(consumers);
        return arr;
    });

    dispatch->addCommand("XCLAIM", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 6 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null || req.array[4].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'xclaim' command");
        }
        int64_t min_idle = 0;
        if(!parseInt64Arg(req.array[4], min_idle)) {
            return RespEncoder::err("ERR value is not an integer or out of range");
        }
        bool force = false;
        std::vector<std::string> ids;
        for(size_t i = 5; i < req.array.size(); ++i) {
            if(req.array[i].is_null) {
                return RespEncoder::err("ERR syntax error");
            }
            if(upperArg(req.array[i].str) == "FORCE") {
                force = true;
            } else {
                ids.push_back(req.array[i].str);
            }
        }
        std::vector<KvStore::StreamEntry> claimed;
        std::string err;
        if(!store->xclaim(ctx.db, req.array[1].str, req.array[2].str, req.array[3].str, min_idle,
                          ids, force, claimed, &err)) {
            return RespEncoder::err(err);
        }
        RespValue arr;
        arr.type = RespType::Array;
        for(const auto& entry : claimed) {
            RespValue row;
            row.type = RespType::Array;
            row.array.push_back(RespEncoder::bulk(entry.id));
            RespValue fields;
            fields.type = RespType::Array;
            for(const auto& f : entry.fields) {
                fields.array.push_back(RespEncoder::bulk(f.first));
                fields.array.push_back(RespEncoder::bulk(f.second));
            }
            row.array.push_back(fields);
            arr.array.push_back(row);
        }
        return arr;
    });

    dispatch->addCommand("DUMP", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() != 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'dump' command");
        }
        int64_t ttl_ms = 0;
        std::string payload;
        bool found = false;
        std::string err;
        if(!store->dumpKey(ctx.db, req.array[1].str, ttl_ms, payload, found, &err)) {
            return RespEncoder::err(err);
        }
        return found ? RespEncoder::bulk(payload) : RespEncoder::nullBulk();
    });

    dispatch->addCommand("RESTORE", [](KvContext& ctx, const RespValue& req, KvStore::ptr store) {
        if(req.array.size() < 4 || req.array[1].is_null || req.array[2].is_null
           || req.array[3].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'restore' command");
        }
        int64_t ttl_ms = 0;
        if(!parseInt64Arg(req.array[2], ttl_ms)) {
            return RespEncoder::err("ERR invalid TTL value, must be an integer");
        }
        bool replace = false;
        for(size_t i = 4; i < req.array.size(); ++i) {
            if(!req.array[i].is_null && upperArg(req.array[i].str) == "REPLACE") {
                replace = true;
            }
        }
        std::string err;
        if(!store->restoreKey(ctx.db, req.array[1].str, ttl_ms, req.array[3].str, replace, &err)) {
            return RespEncoder::err(err);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("TIME", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() != 1) {
            return RespEncoder::err("ERR wrong number of arguments for 'time' command");
        }
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        RespValue arr;
        arr.type = RespType::Array;
        arr.array.push_back(RespEncoder::bulk(std::to_string(tv.tv_sec)));
        arr.array.push_back(RespEncoder::bulk(std::to_string(tv.tv_usec)));
        return arr;
    });

    dispatch->addCommand("SHUTDOWN", [](KvContext&, const RespValue& req, KvStore::ptr) {
        bool save = true;
        if(req.array.size() >= 2 && !req.array[1].is_null) {
            const std::string opt = upperArg(req.array[1].str);
            if(opt == "NOSAVE") {
                save = false;
            } else if(opt != "SAVE") {
                return RespEncoder::err("ERR syntax error");
            }
        }
        if(g_shutdown_fn) {
            g_shutdown_fn(save);
        }
        return RespEncoder::ok();
    });

    dispatch->addCommand("SLOWLOG", [](KvContext&, const RespValue& req, KvStore::ptr) {
        if(req.array.size() < 2 || req.array[1].is_null) {
            return RespEncoder::err("ERR wrong number of arguments for 'slowlog' command");
        }
        const std::string sub = upperArg(req.array[1].str);
        if(sub == "GET") {
            return slowlogGet(req);
        }
        if(sub == "LEN") {
            return RespEncoder::integer(getSlowlog().len());
        }
        if(sub == "RESET") {
            getSlowlog().reset();
            return RespEncoder::ok();
        }
        return RespEncoder::err("ERR unknown SLOWLOG subcommand");
    });
}

} // namespace kv
} // namespace zero

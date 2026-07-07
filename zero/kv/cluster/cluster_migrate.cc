/**
 * @file cluster_migrate.cc
 * @brief MIGRATE 命令实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cluster_migrate.h"
#include "cluster_manager.h"
#include "cluster_slot.h"
#include "zero/kv/resp_reader.h"
#include "zero/streams/socket_stream.h"
#include "zero/core/io/socket.h"
#include "zero/core/io/address.h"
#include "zero/core/log/log.h"
#include <cstring>

namespace zero {
namespace kv {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static bool sendCommand(SocketStream& stream, const RespValue& cmd) {
    std::string encoded;
    RespEncoder::encodeInto(cmd, encoded);
    return stream.writeFixSize(encoded.c_str(), encoded.size()) == (int)encoded.size();
}

static RespValue recvResponse(SocketStream& stream) {
    std::vector<char> buf(4096);
    RespValue result;
    size_t consumed = 0;
    while(true) {
        RespReader reader(buf.data(), buf.size());
        ParseStatus status = reader.tryParse(result, &consumed);
        if(status == ParseStatus::Ok) {
            break;
        }
        if(status == ParseStatus::Error) {
            return RespEncoder::err("ERR protocol error during MIGRATE");
        }
        // Need more data
        int n = stream.read(buf.data(), buf.size());
        if(n <= 0) {
            return RespEncoder::err("ERR connection lost during MIGRATE");
        }
    }
    return result;
}

RespValue handleMigrate(KvContext& ctx, const RespValue& req, KvStore::ptr store) {
    // MIGRATE host port key destination-db timeout [COPY] [REPLACE] [AUTH password] [KEYS key ...]
    if(req.array.size() < 6) {
        return RespEncoder::err("ERR wrong number of arguments for 'migrate' command");
    }
    if(req.array[1].is_null || req.array[2].is_null || req.array[3].is_null
       || req.array[4].is_null || req.array[5].is_null) {
        return RespEncoder::err("ERR syntax error");
    }

    std::string host = req.array[1].str;
    int port = (int)std::strtol(req.array[2].str.c_str(), nullptr, 10);
    std::string key = req.array[3].str;
    (void)std::strtol(req.array[4].str.c_str(), nullptr, 10);
    (void)std::strtol(req.array[5].str.c_str(), nullptr, 10);

    bool copy = false;
    bool replace = false;
    std::vector<std::string> keys;

    // Parse optional arguments
    for(size_t i = 6; i < req.array.size(); ++i) {
        if(req.array[i].is_null) continue;
        std::string opt = req.array[i].str;
        for(char& c : opt) c = (char)std::toupper((unsigned char)c);
        if(opt == "COPY") {
            copy = true;
        } else if(opt == "REPLACE") {
            replace = true;
        } else if(opt == "KEYS" && i + 1 < req.array.size()) {
            for(size_t j = i + 1; j < req.array.size(); ++j) {
                if(!req.array[j].is_null) {
                    keys.push_back(req.array[j].str);
                }
            }
            break;
        }
    }

    if(keys.empty()) {
        keys.push_back(key);
    }

    // Connect to target
    zero::Address::ptr addr = zero::Address::LookupAny(host + ":" + std::to_string(port));
    if(!addr) {
        return RespEncoder::err("ERR can't connect to target node: " + host + ":" + std::to_string(port));
    }
    zero::Socket::ptr sock = zero::Socket::CreateTCP(addr);
    if(!sock->connect(addr)) {
        return RespEncoder::err("ERR can't connect to target node: " + host + ":" + std::to_string(port));
    }
    zero::SocketStream stream(sock);

    int migrated = 0;
    for(const auto& k : keys) {
        // DUMP key
        std::string payload;
        int64_t ttl_ms = 0;
        bool found = false;
        std::string err;
        if(!store->dumpKey(ctx.db, k, ttl_ms, payload, found, &err)) {
            return RespEncoder::err("ERR " + err);
        }
        if(!found) {
            if(!copy) {
                // NOKEY is not an error if the key doesn't exist locally
                continue;
            }
            return RespEncoder::err("ERR can't DUMP key " + k);
        }

        // RESTORE key ttl serialized-value [REPLACE] [ABSTTL] [IDLETIME seconds] [FREQ frequency]
        RespValue restoreCmd;
        restoreCmd.type = RespType::Array;
        restoreCmd.array.push_back(RespEncoder::bulk("RESTORE"));
        restoreCmd.array.push_back(RespEncoder::bulk(k));
        restoreCmd.array.push_back(RespEncoder::bulk(std::to_string(ttl_ms)));
        restoreCmd.array.push_back(RespEncoder::bulk(payload));
        if(replace) {
            restoreCmd.array.push_back(RespEncoder::bulk("REPLACE"));
        }

        if(!sendCommand(stream, restoreCmd)) {
            return RespEncoder::err("ERR failed to send RESTORE to target");
        }

        RespValue rsp = recvResponse(stream);
        if(rsp.type == RespType::Error) {
            return RespEncoder::err("ERR Target instance replied with error: " + rsp.str);
        }

        // Delete local key if not COPY
        if(!copy) {
            store->del(ctx.db, k);
        }
        ++migrated;
    }

    return RespEncoder::ok();
}

} // namespace kv
} // namespace zero

/**
 * @file lua_engine.cc
 * @brief Lua 脚本引擎实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "lua_engine.h"
#include "resp.h"
#include "command_dispatch.h"
#include "kv_context.h"
#include "store/kv_store.h"
#include "zero/core/log/log.h"
#include "zero/core/concurrency/mutex.h"

#include <lua.hpp>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <openssl/sha.h>

namespace zero {
namespace kv {

static zero::Logger::ptr g_lua_logger = ZERO_LOG_NAME("lua");

// ============================================================
// EvalContext 存取（通过 Lua registry）
// ============================================================
static const char* kEvalContextKey = "__zero_eval_ctx";

static LuaEngine::EvalContext* getEvalContext(lua_State* L) {
    lua_pushstring(L, kEvalContextKey);
    lua_gettable(L, LUA_REGISTRYINDEX);
    if (!lua_islightuserdata(L, -1)) {
        lua_pop(L, 1);
        return nullptr;
    }
    auto* ctx = static_cast<LuaEngine::EvalContext*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

static void setEvalContext(lua_State* L, LuaEngine::EvalContext* ctx) {
    lua_pushstring(L, kEvalContextKey);
    lua_pushlightuserdata(L, ctx);
    lua_settable(L, LUA_REGISTRYINDEX);
}

// ============================================================
// SHA1
// ============================================================
std::string LuaEngine::sha1Hex(const std::string& data) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash);
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << (unsigned)hash[i];
    }
    return oss.str();
}

// ============================================================
// RESP ↔ Lua 类型转换
// ============================================================
void LuaEngine::pushRespValue(lua_State* L, const RespValue& v) {
    // 处理 null bulk string
    if (v.type == RespType::BulkString && v.is_null) {
        lua_pushboolean(L, 0);
        return;
    }
    switch (v.type) {
        case RespType::SimpleString:
        case RespType::BulkString:
            lua_pushlstring(L, v.str.data(), v.str.size());
            break;
        case RespType::Integer:
            lua_pushnumber(L, (lua_Number)v.integer);
            break;
        case RespType::Error:
            lua_createtable(L, 0, 1);
            lua_pushstring(L, "err");
            lua_pushlstring(L, v.str.data(), v.str.size());
            lua_settable(L, -3);
            break;
        case RespType::Array: {
            lua_createtable(L, (int)v.array.size(), 0);
            for (size_t i = 0; i < v.array.size(); ++i) {
                pushRespValue(L, v.array[i]);
                lua_rawseti(L, -2, (int)(i + 1));
            }
            break;
        }
        case RespType::Null:
            lua_pushboolean(L, 0);
            break;
    }
}

RespValue LuaEngine::luaToResp(lua_State* L, int index) {
    int t = lua_type(L, index);

    if (t == LUA_TBOOLEAN) {
        if (lua_toboolean(L, index)) {
            RespValue v;
            v.type = RespType::Integer;
            v.integer = 1;
            return v;
        } else {
            RespValue v;
            v.type = RespType::Null;
            return v;
        }
    }

    if (t == LUA_TNUMBER) {
        RespValue v;
        v.type = RespType::Integer;
        v.integer = (int64_t)lua_tonumber(L, index);
        return v;
    }

    if (t == LUA_TSTRING) {
        size_t len;
        const char* s = lua_tolstring(L, index, &len);
        RespValue v;
        v.type = RespType::BulkString;
        v.str.assign(s, len);
        return v;
    }

    if (t == LUA_TTABLE) {
        // 检查是否是 status reply: {ok="..."} 或 {err="..."}
        lua_pushstring(L, "ok");
        lua_gettable(L, index < 0 ? index - 1 : index);
        int hasOk = lua_type(L, -1);
        lua_pop(L, 1);

        if (hasOk == LUA_TSTRING) {
            // Status reply
            lua_pushstring(L, "ok");
            lua_gettable(L, index < 0 ? index - 1 : index);
            size_t len;
            const char* s = lua_tolstring(L, -1, &len);
            RespValue v;
            v.type = RespType::SimpleString;
            v.str.assign(s, len);
            lua_pop(L, 1);
            return v;
        }

        lua_pushstring(L, "err");
        lua_gettable(L, index < 0 ? index - 1 : index);
        int hasErr = lua_type(L, -1);
        lua_pop(L, 1);

        if (hasErr == LUA_TSTRING) {
            // Error reply
            lua_pushstring(L, "err");
            lua_gettable(L, index < 0 ? index - 1 : index);
            size_t len;
            const char* s = lua_tolstring(L, -1, &len);
            RespValue v;
            v.type = RespType::Error;
            v.str.assign(s, len);
            lua_pop(L, 1);
            return v;
        }

        // Regular array
        RespValue v;
        v.type = RespType::Array;
        lua_pushnil(L);
        while (lua_next(L, index < 0 ? index - 1 : index) != 0) {
            // key at -2, value at -1
            if (lua_type(L, -2) == LUA_TNUMBER) {
                v.array.push_back(luaToResp(L, -1));
            }
            lua_pop(L, 1);
        }
        return v;
    }

    if (t == LUA_TNIL || t == LUA_TNONE) {
        RespValue v;
        v.type = RespType::Null;
        return v;
    }

    // 其他类型 → false (null)
    RespValue v;
    v.type = RespType::Null;
    return v;
}

// ============================================================
// redis.call / redis.pcall 实现
// ============================================================
// 共享的 redis.call/redis.pcall 实现
static int luaRedisCallImpl(lua_State* L, bool raiseError) {
    auto* ctx = getEvalContext(L);
    if (!ctx || !ctx->store || !ctx->dispatch) {
        lua_pushstring(L, "ERR redis.call not available outside EVAL");
        lua_error(L);
        return 0;
    }

    int nargs = lua_gettop(L);
    if (nargs < 1) {
        if (raiseError) {
            lua_pushstring(L, "ERR wrong number of arguments for redis.call");
            lua_error(L);
            return 0;
        }
        lua_createtable(L, 0, 1);
        lua_pushstring(L, "err");
        lua_pushstring(L, "ERR wrong number of arguments");
        lua_settable(L, -3);
        return 1;
    }

    // 构造 RESP 命令
    RespValue req;
    req.type = RespType::Array;
    for (int i = 1; i <= nargs; ++i) {
        RespValue arg;
        arg.type = RespType::BulkString;
        if (lua_isstring(L, i)) {
            size_t len;
            const char* s = lua_tolstring(L, i, &len);
            arg.str.assign(s, len);
        } else if (lua_isnumber(L, i)) {
            arg.str = std::to_string((int64_t)lua_tonumber(L, i));
        } else if (lua_isboolean(L, i)) {
            arg.str = lua_toboolean(L, i) ? "1" : "0";
        }
        req.array.push_back(std::move(arg));
    }

    // 提取命令名
    std::string cmdName = req.array[0].str;
    for (char& c : cmdName) { c = (char)std::toupper((unsigned char)c); }

    // 禁止递归调用脚本/事务命令
    if (cmdName == "EVAL" || cmdName == "EVALSHA" ||
        cmdName == "SCRIPT" || cmdName == "MULTI" || cmdName == "EXEC") {
        if (raiseError) {
            lua_pushstring(L, ("ERR " + cmdName + " not allowed inside script").c_str());
            lua_error(L);
            return 0;
        }
        lua_createtable(L, 0, 1);
        lua_pushstring(L, "err");
        lua_pushstring(L, ("ERR " + cmdName + " not allowed inside script").c_str());
        lua_settable(L, -3);
        return 1;
    }

    // 执行命令
    RespValue rsp = ctx->dispatch->dispatch(*ctx->ctx, req, ctx->store);

    // redis.call: Redis error 应该抛 Lua error
    if (raiseError && rsp.type == RespType::Error) {
        lua_pushstring(L, rsp.str.c_str());
        lua_error(L);
        return 0;
    }

    LuaEngine::pushRespValue(L, rsp);
    return 1;
}

int LuaEngine::luaRedisCall(lua_State* L) {
    return luaRedisCallImpl(L, true);
}

int LuaEngine::luaRedisPCall(lua_State* L) {
    return luaRedisCallImpl(L, false);
}

// ============================================================
// redis.log
// ============================================================
int LuaEngine::luaRedisLog(lua_State* L) {
    int level = (int)luaL_checkinteger(L, 1);
    const char* msg = luaL_checkstring(L, 2);

    switch (level) {
        case 0: // DEBUG
            ZERO_LOG_DEBUG(g_lua_logger) << "[lua] " << msg;
            break;
        case 1: // NOTICE
            ZERO_LOG_INFO(g_lua_logger) << "[lua] " << msg;
            break;
        case 2: // WARNING
            ZERO_LOG_WARN(g_lua_logger) << "[lua] " << msg;
            break;
        default:
            ZERO_LOG_INFO(g_lua_logger) << "[lua] " << msg;
            break;
    }
    return 0;
}

int LuaEngine::luaRedisStatusReply(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    lua_createtable(L, 0, 1);
    lua_pushstring(L, "ok");
    lua_pushstring(L, msg);
    lua_settable(L, -3);
    return 1;
}

int LuaEngine::luaRedisErrorReply(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    lua_createtable(L, 0, 1);
    lua_pushstring(L, "err");
    lua_pushstring(L, msg);
    lua_settable(L, -3);
    return 1;
}

int LuaEngine::luaRedisSha1Hex(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    std::string hash = sha1Hex(std::string(s, len));
    lua_pushstring(L, hash.c_str());
    return 1;
}

int LuaEngine::luaRedisAclCheckCmd(lua_State* L) {
    // 简化实现：始终返回 true（无 ACL）
    lua_pushboolean(L, 1);
    return 1;
}

// ============================================================
// 帮助函数
// ============================================================
RespValue LuaEngine::makeError(const std::string& msg) {
    RespValue v;
    v.type = RespType::Error;
    v.str = "ERR " + msg;
    return v;
}

RespValue LuaEngine::makeTableError(const std::string& msg) {
    RespValue v;
    v.type = RespType::Error;
    v.str = msg;
    return v;
}

// ============================================================
// 初始化
// ============================================================
LuaEngine::LuaEngine()
    : m_L(nullptr) {
    m_L = luaL_newstate();
    initLuaState();
}

LuaEngine::~LuaEngine() {
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
}

void LuaEngine::initLuaState() {
    lua_State* L = m_L;

    // 加载标准库（受限集）
    luaL_openlibs(L);

    // 移除危险函数
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "load");
    lua_pushnil(L); lua_setglobal(L, "require");
    lua_pushnil(L); lua_setglobal(L, "package");
    lua_pushnil(L); lua_setglobal(L, "io");
    lua_pushnil(L); lua_setglobal(L, "os");

    // 但保留 os.clock 用于性能测试
    // 简化：直接移除整个 os 库

    // 注册 Redis API
    lua_register(L, "redis_call", luaRedisCall);
    lua_register(L, "redis_pcall", luaRedisPCall);
    lua_register(L, "redis_log", luaRedisLog);
    lua_register(L, "redis_status_reply", luaRedisStatusReply);
    lua_register(L, "redis_error_reply", luaRedisErrorReply);
    lua_register(L, "redis_sha1hex", luaRedisSha1Hex);
    lua_register(L, "redis_acl_check_cmd", luaRedisAclCheckCmd);

    // 创建 redis table: redis.call, redis.pcall, redis.log, ...
    lua_createtable(L, 0, 7);

    lua_pushcfunction(L, luaRedisCall);
    lua_setfield(L, -2, "call");

    lua_pushcfunction(L, luaRedisPCall);
    lua_setfield(L, -2, "pcall");

    lua_pushcfunction(L, luaRedisLog);
    lua_setfield(L, -2, "log");

    lua_pushcfunction(L, luaRedisStatusReply);
    lua_setfield(L, -2, "status_reply");

    lua_pushcfunction(L, luaRedisErrorReply);
    lua_setfield(L, -2, "error_reply");

    lua_pushcfunction(L, luaRedisSha1Hex);
    lua_setfield(L, -2, "sha1hex");

    lua_pushcfunction(L, luaRedisAclCheckCmd);
    lua_setfield(L, -2, "acl_check_cmd");

    lua_setglobal(L, "redis");

    // 注册 redis.LOG_* 常量
    lua_getglobal(L, "redis");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "LOG_DEBUG");
    lua_pushinteger(L, 1); lua_setfield(L, -2, "LOG_NOTICE");
    lua_pushinteger(L, 2); lua_setfield(L, -2, "LOG_WARNING");
    lua_pushinteger(L, 3); lua_setfield(L, -2, "LOG_ERR");
    lua_pop(L, 1); // pop redis table

    // 设置指令计数限制（防止死循环）：5M 指令 + SCRIPT KILL 检查
    lua_sethook(L, [](lua_State* L, lua_Debug*) {
        auto* ctx = getEvalContext(L);
        if (ctx) {
            ctx->scriptKillCount++;
            if (ctx->scriptKillCount > 5000000) {
                luaL_error(L, "ERR Script killed because it exceeded the maximum execution time");
            }
        }
    }, LUA_MASKCOUNT, 100000);
}

// ============================================================
// EVAL
// ============================================================
RespValue LuaEngine::eval(const std::string& script,
                           const std::vector<std::string>& keys,
                           const std::vector<std::string>& argv,
                           KvStore::ptr store,
                           CommandDispatch* dispatch,
                           KvContext* ctx) {
    zero::Mutex::Lock lock(m_mutex);
    m_killRequested = false;

    // Lua 全局原子性：开启后锁定全部 16 shard 写锁，
    // 脚本期间所有其他连接无法读写任何 key，严重损失并发度
    if (m_globalAtomic && store) {
        store->lockAllShards(true);
    }

    lua_State* L = m_L;
    if (!L) {
        if (m_globalAtomic && store) {
            store->unlockAllShards();
        }
        return makeError("Lua engine not initialized");
    }

    // 设置执行上下文
    m_curCtx.store = store;
    m_curCtx.dispatch = dispatch;
    m_curCtx.ctx = ctx;
    m_curCtx.scriptKillCount = 0;
    setEvalContext(L, &m_curCtx);

    // 加载脚本
    if (luaL_loadstring(L, script.c_str()) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "unknown error";
        lua_pop(L, 1);
        setEvalContext(L, nullptr);
        if (m_globalAtomic && store) store->unlockAllShards();
        return makeTableError("ERR " + error);
    }

    // 设置 KEYS 全局变量（Lua table）
    lua_createtable(L, (int)keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        lua_pushlstring(L, keys[i].data(), keys[i].size());
        lua_rawseti(L, -2, (int)(i + 1));
    }
    lua_setglobal(L, "KEYS");

    // 设置 ARGV 全局变量（Lua table）
    lua_createtable(L, (int)argv.size(), 0);
    for (size_t i = 0; i < argv.size(); ++i) {
        lua_pushlstring(L, argv[i].data(), argv[i].size());
        lua_rawseti(L, -2, (int)(i + 1));
    }
    lua_setglobal(L, "ARGV");

    // 执行脚本
    int rc = lua_pcall(L, 0, 1, 0);

    RespValue result;
    if (rc == LUA_OK) {
        // 脚本执行成功，取栈顶返回值
        result = luaToResp(L, -1);
        lua_pop(L, 1);
    } else {
        // 脚本执行错误
        const char* err = lua_tostring(L, -1);
        std::string error = err ? err : "unknown script error";
        lua_pop(L, 1);
        result = makeTableError("ERR " + error);
    }

    // 清理上下文
    lua_pushnil(L); lua_setglobal(L, "KEYS");
    lua_pushnil(L); lua_setglobal(L, "ARGV");
    setEvalContext(L, nullptr);
    m_curCtx.store = nullptr;
    m_curCtx.dispatch = nullptr;
    m_curCtx.ctx = nullptr;

    // 释放全局 shard 锁（若开启了 lua-global-atomic）
    if (m_globalAtomic && store) {
        store->unlockAllShards();
    }

    return result;
}

// ============================================================
// SCRIPT 命令
// ============================================================
std::string LuaEngine::scriptLoad(const std::string& script) {
    std::string sha = sha1Hex(script);
    m_scripts[sha] = script;
    return sha;
}

std::vector<bool> LuaEngine::scriptExists(const std::vector<std::string>& sha1s) {
    std::vector<bool> result;
    result.reserve(sha1s.size());
    for (const auto& s : sha1s) {
        result.push_back(m_scripts.find(s) != m_scripts.end());
    }
    return result;
}

void LuaEngine::scriptFlush() {
    m_scripts.clear();
}

void LuaEngine::scriptKill() {
    m_killRequested = true;
}

std::string LuaEngine::getScriptBody(const std::string& sha1) const {
    auto it = m_scripts.find(sha1);
    if (it != m_scripts.end()) {
        return it->second;
    }
    return "";
}

} // namespace kv
} // namespace zero

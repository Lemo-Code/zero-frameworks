/**
 * @file lua_engine.h
 * @brief Lua 脚本引擎 — EVAL/EVALSHA/SCRIPT 命令支持
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_LUA_ENGINE_H__
#define __ZERO_KV_LUA_ENGINE_H__

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "store/kv_store.h"

struct lua_State;

namespace zero {
namespace kv {

class CommandDispatch;
class KvContext;
struct RespValue;

// Lua 脚本引擎
class LuaEngine {
public:
    using ptr = std::shared_ptr<LuaEngine>;

    LuaEngine();
    ~LuaEngine();

    // 禁止拷贝
    LuaEngine(const LuaEngine&) = delete;
    LuaEngine& operator=(const LuaEngine&) = delete;

    // 执行 Lua 脚本，返回 RESP 结果
    RespValue eval(const std::string& script,
                   const std::vector<std::string>& keys,
                   const std::vector<std::string>& argv,
                   KvStore::ptr store,
                   CommandDispatch* dispatch,
                   KvContext* ctx);

    // 加载脚本，返回 SHA1
    std::string scriptLoad(const std::string& script);

    // 检查脚本是否存在
    std::vector<bool> scriptExists(const std::vector<std::string>& sha1s);

    // 清空脚本缓存
    void scriptFlush();

    // 获取缓存的脚本 body（用于 EVALSHA → EVAL 传播）
    std::string getScriptBody(const std::string& sha1) const;

    // 获取脚本缓存大小
    size_t scriptCount() const { return m_scripts.size(); }

    // SCRIPT KILL — 请求终止正在执行的脚本
    void scriptKill();
    bool isKillRequested() const { return m_killRequested; }

    // Lua 全局原子性配置（默认关闭，严重损失并发，不推荐开启）
    void setGlobalAtomic(bool v) { m_globalAtomic = v; }
    bool isGlobalAtomic() const { return m_globalAtomic; }

    // EVAL 执行上下文（通过 Lua registry 传递给 C 函数）
    struct EvalContext {
        KvStore::ptr store;
        CommandDispatch* dispatch = nullptr;
        KvContext* ctx = nullptr;
        int scriptKillCount = 0;
    };

    // RESP ↔ Lua 类型转换（public，供 C 回调函数调用）
    static void pushRespValue(lua_State* L, const RespValue& v);
    static RespValue luaToResp(lua_State* L, int index);

private:
    // 初始化 Lua state，注入 redis.call / redis.pcall / redis.log 等
    void initLuaState();

    // 线程安全锁
    mutable zero::Mutex m_mutex;

    // SCRIPT KILL 标志
    std::atomic<bool> m_killRequested{false};

    // Lua 全局原子性：开启后 EVAL 执行前锁定全部 16 shard
    // 默认 false，开启后严重损失并发度，不推荐
    bool m_globalAtomic = false;

    // Redis 命令调用回调（由 redis.call / redis.pcall 触发）
    static int luaRedisCall(lua_State* L);
    static int luaRedisPCall(lua_State* L);
    static int luaRedisLog(lua_State* L);
    static int luaRedisStatusReply(lua_State* L);
    static int luaRedisErrorReply(lua_State* L);
    static int luaRedisSha1Hex(lua_State* L);
    static int luaRedisAclCheckCmd(lua_State* L);

    // SHA1 计算
    static std::string sha1Hex(const std::string& data);

    // 错误格式化
    static RespValue makeError(const std::string& msg);
    static RespValue makeTableError(const std::string& msg);

    lua_State* m_L;
    std::unordered_map<std::string, std::string> m_scripts; // sha1 → script body
    EvalContext m_curCtx; // 当前 EVAL 上下文
};

} // namespace kv
} // namespace zero

#endif

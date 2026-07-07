/**
 * @file command_dispatch.h
 * @brief 命令路由表（类比 http::ServletDispatch）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_COMMAND_DISPATCH_H__
#define __ZERO_KV_COMMAND_DISPATCH_H__

#include "command.h"
#include "zero/core/concurrency/thread.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace zero {
namespace kv {

class CommandDispatch;
class LuaEngine;

class CommandDispatch {
public:
    typedef std::shared_ptr<CommandDispatch> ptr;
    typedef RWMutex RWMutexType;

    void addCommand(const std::string& name, CommandHandler::ptr handler);
    void addCommand(const std::string& name, FunctionCommandHandler::callback cb);

    RespValue dispatch(KvContext& ctx, const RespValue& request, KvStore::ptr store);
    /** EXEC 内执行单条命令（绕过 MULTI 队列） */
    RespValue dispatchExecOne(KvContext& ctx, const RespValue& request, KvStore::ptr store);

private:
    static std::string upperName(const RespValue& request);
    static std::string upper(const std::string& s);

    RWMutexType m_mutex;
    std::unordered_map<std::string, CommandHandler::ptr> m_handlers;
    CommandHandler::ptr m_default;
};

void registerBuiltinCommands(CommandDispatch::ptr dispatch);
void registerP0P1Commands(CommandDispatch::ptr dispatch);

void bindBgsaveForConfig(std::function<bool()> runner);
void bindBgRewriteAofForConfig(std::function<bool()> runner);

struct RedisClientSnapshot {
    int64_t id = 0;
    std::string name;
    std::string addr;
};
void bindClientListForConfig(std::function<std::vector<RedisClientSnapshot>()> fn);
void bindShutdownForConfig(std::function<void(bool save)> fn);
void bindLuaEngineForConfig(std::function<std::shared_ptr<LuaEngine>()> fn);

} // namespace kv
} // namespace zero

#endif

/**
 * @file command.h
 * @brief Redis 命令处理接口（类比 http::Servlet）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_COMMAND_H__
#define __ZERO_KV_COMMAND_H__

#include "kv_context.h"
#include "resp.h"
#include "store/kv_store.h"
#include <functional>
#include <memory>
#include <string>

namespace zero {
namespace kv {

class CommandHandler {
public:
    typedef std::shared_ptr<CommandHandler> ptr;
    virtual ~CommandHandler() {}
    virtual RespValue handle(KvContext& ctx, const RespValue& request, KvStore::ptr store) = 0;
};

class FunctionCommandHandler : public CommandHandler {
public:
    typedef std::shared_ptr<FunctionCommandHandler> ptr;
    typedef std::function<RespValue (KvContext&, const RespValue&, KvStore::ptr)> callback;

    explicit FunctionCommandHandler(callback cb)
        :m_cb(std::move(cb)) {}

    RespValue handle(KvContext& ctx, const RespValue& request, KvStore::ptr store) override {
        return m_cb(ctx, request, store);
    }
private:
    callback m_cb;
};

} // namespace kv
} // namespace zero

#endif

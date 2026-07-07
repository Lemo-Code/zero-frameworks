/**
 * @file aof.h
 * @brief AOF 追加日志（RESP 命令序列）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_PERSISTENCE_AOF_H__
#define __ZERO_KV_PERSISTENCE_AOF_H__

#include "zero/kv/command_dispatch.h"
#include "zero/kv/resp.h"
#include "zero/kv/store/kv_store.h"
#include "zero/core/concurrency/mutex.h"
#include <memory>
#include <string>
#include <vector>

namespace zero {
namespace kv {

enum class AofFsyncPolicy {
    Always,
    EverySec,
    No
};

class AofLog {
public:
    typedef std::shared_ptr<AofLog> ptr;

    void setPath(const std::string& path);
    const std::string& getPath() const;
    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setFsyncPolicy(AofFsyncPolicy policy);
    AofFsyncPolicy fsyncPolicy() const;
    void setFsyncPolicyString(const std::string& policy);
    std::string fsyncPolicyString() const;
    bool syncToDisk();

    bool append(const RespValue& command);
    bool replay(KvStore::ptr store, CommandDispatch::ptr dispatch, std::string* err = nullptr);
    bool rewrite(KvStore::ptr store, std::string* err = nullptr);

private:
    static void buildRewriteCommands(const KvStore& store, std::vector<RespValue>& out);

    mutable zero::Mutex m_mutex;
    std::string m_path = "./appendonly.aof";
    bool m_enabled = false;
    AofFsyncPolicy m_fsync = AofFsyncPolicy::EverySec;
};

bool isMutatingCommand(const std::string& upper_name);

void bindAofForConfig(AofLog::ptr aof);
AofLog::ptr getAofForConfig();

} // namespace kv
} // namespace zero

#endif

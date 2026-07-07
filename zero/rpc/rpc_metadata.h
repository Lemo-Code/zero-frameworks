/**
 * @file rpc_metadata.h
 * @brief RPC 调用元数据（traceId、caller、路由标签等）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_RPC_RPC_METADATA_H__
#define __ZERO_RPC_RPC_METADATA_H__

#include <string>
#include <map>

namespace zero {
namespace rpc {

/**
 * @brief RPC 调用元数据
 */
struct RpcMetadata {
    std::string traceId;
    std::string spanId;
    std::string parentSpanId;
    std::string caller;
    std::string callerIp;
    std::map<std::string, std::string> tags;

    void set(const std::string& key, const std::string& value) {
        tags[key] = value;
    }

    std::string get(const std::string& key) const {
        auto it = tags.find(key);
        return it != tags.end() ? it->second : "";
    }

    bool empty() const {
        return traceId.empty() && spanId.empty() && caller.empty() && tags.empty();
    }
};

} // namespace rpc
} // namespace zero

#endif // __ZERO_RPC_RPC_METADATA_H__

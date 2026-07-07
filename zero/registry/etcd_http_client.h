/**
 * @file etcd_http_client.h
 * @brief 基于 zero-framework HTTP 客户端的 etcd v3 HTTP API 封装
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_REGISTRY_ETCD_HTTP_CLIENT_H__
#define __ZERO_REGISTRY_ETCD_HTTP_CLIENT_H__

#include <string>
#include <vector>
#include <map>
#include <memory>

namespace zero {

/**
 * @brief etcd v3 HTTP 响应
 */
struct EtcdResponse {
    bool ok = false;
    int httpStatus = 0;
    std::string body;
    std::string error;
};

/**
 * @brief etcd v3 HTTP 客户端
 * 
 * 使用 zero-framework 内置 HttpConnection 调用 etcd gRPC-gateway。
 */
class EtcdHttpClient {
public:
    typedef std::shared_ptr<EtcdHttpClient> ptr;

    EtcdHttpClient(const std::vector<std::string>& endpoints);

    EtcdResponse put(const std::string& key, const std::string& value, int64_t lease = 0);
    EtcdResponse get(const std::string& key);
    EtcdResponse getPrefix(const std::string& prefix);
    EtcdResponse del(const std::string& key);
    EtcdResponse delPrefix(const std::string& prefix);

    // lease
    EtcdResponse leaseGrant(int64_t ttl);
    EtcdResponse leaseKeepalive(int64_t leaseId);

    // lock
    EtcdResponse lock(const std::string& name, int64_t leaseId);
    EtcdResponse unlock(const std::string& lockKey);

private:
    EtcdResponse doPost(const std::string& path, const std::string& body);

    std::vector<std::string> m_endpoints;
    size_t m_index = 0;
};

} // namespace zero

#endif // __ZERO_REGISTRY_ETCD_HTTP_CLIENT_H__

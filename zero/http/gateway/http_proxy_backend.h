/**
 * @file http_proxy_backend.h
 * @brief Gateway HTTP 反向代理后端
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_HTTP_PROXY_BACKEND_H__
#define __ZERO_HTTP_HTTP_PROXY_BACKEND_H__

#include "zero/http/http.h"
#include "zero/http/http_connection.h"
#include <string>
#include <functional>
#include <map>

namespace zero {
namespace http {

/**
 * @brief HTTP 反向代理配置
 */
struct HttpProxyConfig {
    std::string upstreamUrl;        ///< 上游地址，例如 http://127.0.0.1:8080
    uint64_t timeoutMs = 5000;
    int maxRetries = 1;
    std::map<std::string, std::string> defaultHeaders;
};

/**
 * @brief HTTP 反向代理后端处理函数
 */
class HttpProxyBackend {
public:
    typedef std::shared_ptr<HttpProxyBackend> ptr;

    explicit HttpProxyBackend(const HttpProxyConfig& config);

    /**
     * @brief 处理网关请求，转发到上游
     */
    bool handle(HttpRequest::ptr request, HttpResponse::ptr response);

private:
    bool doRequest(HttpRequest::ptr request, HttpResponse::ptr response);

    HttpProxyConfig m_config;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_HTTP_PROXY_BACKEND_H__

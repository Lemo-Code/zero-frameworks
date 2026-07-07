/**
 * @file http_reverse_proxy.h
 * @brief API 网关 HTTP 反向代理后端
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_GATEWAY_HTTP_REVERSE_PROXY_H__
#define __ZERO_HTTP_GATEWAY_HTTP_REVERSE_PROXY_H__

#include "zero/http/gateway/gateway_servlet.h"
#include <string>

namespace zero {
namespace http {

/**
 * @brief 创建 HTTP 反向代理后端
 * @param[in] upstreamUrl 上游基础 URL，例如 http://127.0.0.1:8080
 * @param[in] timeoutMs   单次请求超时（毫秒）
 * @param[in] maxRetries  失败重试次数（含首次）
 * @return GatewayBackend 回调
 */
GatewayBackend createHttpReverseProxyBackend(const std::string& upstreamUrl,
                                             int timeoutMs = 3000,
                                             int maxRetries = 1);

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_GATEWAY_HTTP_REVERSE_PROXY_H__

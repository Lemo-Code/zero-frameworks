/**
 * @file http_reverse_proxy.cc
 * @brief API 网关 HTTP 反向代理后端实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "http_reverse_proxy.h"
#include "zero/http/http_connection.h"
#include "zero/core/util/uri.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

GatewayBackend createHttpReverseProxyBackend(const std::string& upstreamUrl,
                                             int timeoutMs,
                                             int maxRetries) {
    std::string base = upstreamUrl;
    if(!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    return [base, timeoutMs, maxRetries](HttpRequest::ptr req,
                                         HttpResponse::ptr rsp) -> bool {
        std::string target = base + req->getPath();
        if(!req->getQuery().empty()) {
            target += "?" + req->getQuery();
        }

        std::map<std::string, std::string> headers(req->getHeaders().begin(), req->getHeaders().end());
        // 移除 hop-by-hop 头，避免影响上游连接管理
        headers.erase("Host");
        headers.erase("Connection");
        headers.erase("Keep-Alive");
        headers.erase("Transfer-Encoding");
        headers.erase("Proxy-Connection");

        std::string body = req->getBody();
        HttpResult::ptr result;

        for(int i = 0; i < maxRetries; ++i) {
            result = HttpConnection::DoRequest(req->getMethod(), target,
                                               timeoutMs, headers, body);
            if(result && result->result == 0 && result->response) {
                break;
            }
            if(i + 1 < maxRetries) {
                ZERO_LOG_WARN(g_logger) << "HttpReverseProxy retry " << (i + 1)
                    << " for " << target;
            }
        }

        if(!result || result->result != 0 || !result->response) {
            ZERO_LOG_ERROR(g_logger) << "HttpReverseProxy failed: " << target
                << " error=" << (result ? result->error : "null");
            rsp->setStatus(HttpStatus::BAD_GATEWAY);
            rsp->setHeader("Content-Type", "application/json");
            rsp->setBody("{\"error\":\"bad gateway\"}");
            return false;
        }

        auto upstreamRsp = result->response;
        rsp->setStatus(upstreamRsp->getStatus());
        for(auto& kv : upstreamRsp->getHeaders()) {
            if(kv.first == "Connection" || kv.first == "Keep-Alive" ||
               kv.first == "Transfer-Encoding") {
                continue;
            }
            rsp->setHeader(kv.first, kv.second);
        }
        rsp->setBody(upstreamRsp->getBody());
        return true;
    };
}

} // namespace http
} // namespace zero

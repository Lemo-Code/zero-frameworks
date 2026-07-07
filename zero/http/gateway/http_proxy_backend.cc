/**
 * @file http_proxy_backend.cc
 * @brief HTTP 反向代理后端实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "http_proxy_backend.h"
#include "zero/core/log/log.h"
#include "zero/core/util/uri.h"
#include <thread>
#include <chrono>

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

HttpProxyBackend::HttpProxyBackend(const HttpProxyConfig& config)
    : m_config(config) {
}

static std::string methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default: return "GET";
    }
}

bool HttpProxyBackend::doRequest(HttpRequest::ptr request, HttpResponse::ptr response) {
    auto uri = Uri::Create(m_config.upstreamUrl);
    if (!uri) {
        response->setStatus(HttpStatus::BAD_GATEWAY);
        response->setBody("invalid upstream url");
        return false;
    }

    // 构造上游完整 URL
    std::string upstream = m_config.upstreamUrl;
    if (upstream.back() == '/') upstream.pop_back();
    std::string targetUrl = upstream + request->getPath();
    if (!request->getQuery().empty()) {
        targetUrl += "?" + request->getQuery();
    }

    std::map<std::string, std::string> headers(
        request->getHeaders().begin(), request->getHeaders().end());
    for (const auto& kv : m_config.defaultHeaders) {
        headers[kv.first] = kv.second;
    }
    headers["Host"] = uri->getHost();

    HttpResult::ptr result = HttpConnection::DoRequest(
        request->getMethod(), targetUrl, m_config.timeoutMs, headers, request->getBody());

    if (!result || result->result != 0 || !result->response) {
        response->setStatus(HttpStatus::BAD_GATEWAY);
        response->setBody(result ? result->error : "upstream error");
        return false;
    }

    *response = *(result->response);
    return true;
}

bool HttpProxyBackend::handle(HttpRequest::ptr request, HttpResponse::ptr response) {
    for (int i = 0; i < m_config.maxRetries; ++i) {
        if (doRequest(request, response)) return true;
        if (i < m_config.maxRetries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50 * (i + 1)));
        }
    }
    return false;
}

} // namespace http
} // namespace zero

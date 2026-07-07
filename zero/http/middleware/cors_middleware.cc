/**
 * @file cors_middleware.cc
 * @brief CORS 跨域中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cors_middleware.h"
#include <sstream>

namespace zero {
namespace http {

CORSMiddleware::CORSMiddleware(const CORSConfig& config)
    : m_config(config) {
}

CORSMiddleware::ptr CORSMiddleware::create(const CORSConfig& config) {
    return std::shared_ptr<CORSMiddleware>(new CORSMiddleware(config));
}

bool CORSMiddleware::isOriginAllowed(const std::string& origin) const {
    if(m_config.allowedOrigins.empty()) {
        return true;
    }
    return m_config.allowedOrigins.find(origin) != m_config.allowedOrigins.end();
}

std::string CORSMiddleware::joinMethods() const {
    std::stringstream ss;
    bool first = true;
    for(auto& m : m_config.allowedMethods) {
        if(!first) ss << ", ";
        ss << m;
        first = false;
    }
    return ss.str();
}

std::string CORSMiddleware::joinHeaders() const {
    std::stringstream ss;
    bool first = true;
    for(auto& h : m_config.allowedHeaders) {
        if(!first) ss << ", ";
        ss << h;
        first = false;
    }
    return ss.str();
}

int32_t CORSMiddleware::handle(HttpRequest::ptr request,
                               HttpResponse::ptr response,
                               HttpSession::ptr session) {
    (void)session;
    std::string origin = request->getHeader("Origin");
    if(origin.empty()) {
        return 0;
    }

    if(!isOriginAllowed(origin)) {
        return 0;
    }

    if(m_config.allowedOrigins.empty()) {
        response->setHeader("Access-Control-Allow-Origin", "*");
    } else {
        response->setHeader("Access-Control-Allow-Origin", origin);
        response->setHeader("Vary", "Origin");
    }

    if(m_config.allowCredentials) {
        response->setHeader("Access-Control-Allow-Credentials", "true");
    }

    // 处理 OPTIONS 预检请求
    if(request->getMethod() == HttpMethod::OPTIONS) {
        response->setHeader("Access-Control-Allow-Methods", joinMethods());
        response->setHeader("Access-Control-Allow-Headers", joinHeaders());
        response->setHeader("Access-Control-Max-Age", std::to_string(m_config.maxAge));
        response->setStatus(HttpStatus::NO_CONTENT);
        return -1;
    }

    return 0;
}

} // namespace http
} // namespace zero

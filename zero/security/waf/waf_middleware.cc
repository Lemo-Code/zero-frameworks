/**
 * @file waf_middleware.cc
 * @brief WAF 中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "waf_middleware.h"

namespace zero {
namespace http {

WafConfig::WafConfig() {
    // 常见 SQL 注入特征
    sqlPatterns.emplace_back(
        R"((?:union\s+select|select\s+.*\s+from|insert\s+into|delete\s+from|drop\s+table|update\s+.*\s+set|--|;|--|'/\*|\*/|xp_|0x[0-9a-fA-F]+))",
        std::regex::icase);
    // 常见 XSS 特征（\b 确保 on\w+= 只匹配事件处理器，避免 "content=" 误报）
    xssPatterns.emplace_back(
        R"((?:<script|javascript:|\bon\w+\s*=|<iframe|<object|<embed|alert\s*\(|confirm\s*\(|prompt\s*\())",
        std::regex::icase);
}

WafMiddleware::ptr WafMiddleware::create(const WafConfig& config) {
    return WafMiddleware::ptr(new WafMiddleware(config));
}

WafMiddleware::WafMiddleware(const WafConfig& config)
    : m_config(config), m_ipBlacklist(config.ipBlacklist) {
}

void WafMiddleware::addIpBlacklist(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ipBlacklist.insert(ip);
}

void WafMiddleware::removeIpBlacklist(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ipBlacklist.erase(ip);
}

bool WafMiddleware::detectSqlInjection(const std::string& value) const {
    for (const auto& re : m_config.sqlPatterns) {
        if (std::regex_search(value, re)) return true;
    }
    return false;
}

bool WafMiddleware::detectXss(const std::string& value) const {
    for (const auto& re : m_config.xssPatterns) {
        if (std::regex_search(value, re)) return true;
    }
    return false;
}

bool WafMiddleware::isIpBlocked(const std::string& ip) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_ipBlacklist.find(ip) != m_ipBlacklist.end();
}

bool WafMiddleware::scanRequest(HttpRequest::ptr request) const {
    if (!request) return true;
    // 扫描 query string
    auto params = request->getParams();
    for (const auto& kv : params) {
        if (m_config.blockSqlInjection && detectSqlInjection(kv.second)) return false;
        if (m_config.blockXss && detectXss(kv.second)) return false;
    }
    // 扫描 header
    for (const auto& kv : request->getHeaders()) {
        if (m_config.blockSqlInjection && detectSqlInjection(kv.second)) return false;
        if (m_config.blockXss && detectXss(kv.second)) return false;
    }
    // 扫描 body（简单字符串扫描）
    auto body = request->getBody();
    if (!body.empty()) {
        if (m_config.blockSqlInjection && detectSqlInjection(body)) return false;
        if (m_config.blockXss && detectXss(body)) return false;
    }
    return true;
}

int32_t WafMiddleware::handle(HttpRequest::ptr request,
                              HttpResponse::ptr response,
                              HttpSession::ptr session) {
    (void)session;
    if (m_config.blockIpBlacklist) {
        auto clientAddr = request->getHeader("X-Real-IP");
        if (clientAddr.empty()) {
            clientAddr = request->getHeader("X-Forwarded-For");
        }
        if (!clientAddr.empty() && isIpBlocked(clientAddr)) {
            response->setStatus(HttpStatus::FORBIDDEN);
            response->setBody(m_config.blockMessage);
            return -1;
        }
    }
    if (!scanRequest(request)) {
        response->setStatus(HttpStatus::FORBIDDEN);
        response->setBody(m_config.blockMessage);
        return -1;
    }
    return 0;
}

} // namespace http
} // namespace zero

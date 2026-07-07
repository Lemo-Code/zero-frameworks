/**
 * @file waf_middleware.h
 * @brief Web 应用防火墙中间件（SQL 注入、XSS、IP 黑名单）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_SECURITY_WAF_WAF_MIDDLEWARE_H__
#define __ZERO_SECURITY_WAF_WAF_MIDDLEWARE_H__

#include "zero/http/middleware.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <regex>
#include <mutex>

namespace zero {
namespace http {

/**
 * @brief WAF 规则配置
 */
struct WafConfig {
    bool blockSqlInjection = true;
    bool blockXss = true;
    bool blockIpBlacklist = true;
    std::vector<std::regex> sqlPatterns;
    std::vector<std::regex> xssPatterns;
    std::unordered_set<std::string> ipBlacklist;
    int32_t blockStatusCode = 403;
    std::string blockMessage = "Forbidden by WAF";

    WafConfig();
};

/**
 * @brief WAF 中间件
 */
class WafMiddleware : public Middleware {
public:
    typedef std::shared_ptr<WafMiddleware> ptr;

    static WafMiddleware::ptr create(const WafConfig& config = WafConfig());

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

    void addIpBlacklist(const std::string& ip);
    void removeIpBlacklist(const std::string& ip);

private:
    explicit WafMiddleware(const WafConfig& config);

    bool detectSqlInjection(const std::string& value) const;
    bool detectXss(const std::string& value) const;
    bool isIpBlocked(const std::string& ip) const;
    bool scanRequest(HttpRequest::ptr request) const;

    WafConfig m_config;
    mutable std::mutex m_mutex;
    std::unordered_set<std::string> m_ipBlacklist;
};

} // namespace http
} // namespace zero

#endif // __ZERO_SECURITY_WAF_WAF_MIDDLEWARE_H__

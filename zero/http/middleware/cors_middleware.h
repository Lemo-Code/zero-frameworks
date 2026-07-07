/**
 * @file cors_middleware.h
 * @brief CORS 跨域中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_CORS_H__
#define __ZERO_HTTP_MIDDLEWARE_CORS_H__

#include "zero/http/middleware.h"
#include <unordered_set>

namespace zero {
namespace http {

/**
 * @brief CORS 配置
 */
struct CORSConfig {
    /// 允许的源，空表示允许所有（*）
    std::unordered_set<std::string> allowedOrigins;
    /// 允许的方法
    std::unordered_set<std::string> allowedMethods = {"GET", "POST", "PUT", "DELETE", "OPTIONS"};
    /// 允许的请求头
    std::unordered_set<std::string> allowedHeaders = {"Content-Type", "Authorization"};
    /// 是否允许携带凭证
    bool allowCredentials = false;
    /// 预检缓存时间（秒）
    int maxAge = 86400;
};

/**
 * @brief CORS 中间件
 * 
 * 自动处理 OPTIONS 预检请求，并在响应头中添加 CORS 相关字段。
 */
class CORSMiddleware : public Middleware {
public:
    typedef std::shared_ptr<CORSMiddleware> ptr;

    static CORSMiddleware::ptr create(const CORSConfig& config = CORSConfig());

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    explicit CORSMiddleware(const CORSConfig& config);

    bool isOriginAllowed(const std::string& origin) const;
    std::string joinMethods() const;
    std::string joinHeaders() const;

    CORSConfig m_config;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_CORS_H__

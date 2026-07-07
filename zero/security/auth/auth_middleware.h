/**
 * @file auth_middleware.h
 * @brief JWT 风格认证中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_MIDDLEWARE_AUTH_H__
#define __ZERO_HTTP_MIDDLEWARE_AUTH_H__

#include "zero/http/middleware.h"
#include "zero/util/crypto_util.h"
#include <functional>
#include <unordered_set>

namespace zero {
namespace http {

/**
 * @brief 认证信息
 */
struct AuthContext {
    std::string userId;
    std::string role;
    std::map<std::string, std::string> claims;
    bool authenticated = false;
};

/**
 * @brief Token 验证函数
 * @return 验证成功返回 true，并把用户信息写入 ctx
 */
typedef std::function<bool(const std::string& token, AuthContext& ctx)> TokenVerifier;

/**
 * @brief 认证中间件
 * 
 * 从 Authorization: Bearer <token> 头部提取 token，调用 verifier 验证。
 * 验证失败返回 401 Unauthorized。
 */
class AuthMiddleware : public Middleware {
public:
    typedef std::shared_ptr<AuthMiddleware> ptr;

    /**
     * @brief 创建认证中间件
     * @param[in] verifier Token 验证函数
     */
    static AuthMiddleware::ptr create(TokenVerifier verifier);

    /**
     * @brief 创建基于静态 Secret 的 JWT-like 中间件（简化版 HMAC-SHA256）
     * @param[in] secret 签名密钥
     * @param[in] issuer 签发者
     */
    /**
     * @brief 基于静态 token 映射的认证
     */
    static AuthMiddleware::ptr staticTokens(
        const std::map<std::string, AuthContext>& tokens);

    /**
     * @brief 基于 JWT（HS256）的认证
     * @param[in] secret 签名密钥
     * @param[in] issuer 签发者，为空则不校验
     */
    static AuthMiddleware::ptr jwt(const std::string& secret,
                                   const std::string& issuer = "");

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    explicit AuthMiddleware(TokenVerifier verifier);

    TokenVerifier m_verifier;
};

/**
 * @brief 鉴权中间件（基于角色）
 * 
 * 需在 AuthMiddleware 之后使用，检查用户角色是否在允许列表中。
 */
class RBACMiddleware : public Middleware {
public:
    typedef std::shared_ptr<RBACMiddleware> ptr;

    /**
     * @brief 创建 RBAC 中间件
     * @param[in] allowedRoles 允许访问的角色集合
     * @param[in] roleClaimKey 角色在 AuthContext claims 中的键名
     */
    static RBACMiddleware::ptr create(const std::unordered_set<std::string>& allowedRoles,
                                      const std::string& roleClaimKey = "role");

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    RBACMiddleware(const std::unordered_set<std::string>& allowedRoles,
                   const std::string& roleClaimKey);

    std::unordered_set<std::string> m_allowedRoles;
    std::string m_roleClaimKey;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_MIDDLEWARE_AUTH_H__

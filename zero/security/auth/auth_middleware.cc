/**
 * @file auth_middleware.cc
 * @brief 认证鉴权中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "auth_middleware.h"
#include "zero/core/log/log.h"
#include "zero/util/jwt_util.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static const char* kAuthHeader = "Authorization";
static const char* kUserIdHeader = "X-User-Id";
static const char* kUserRoleHeader = "X-User-Role";

// ============================================================
// AuthMiddleware
// ============================================================

AuthMiddleware::AuthMiddleware(TokenVerifier verifier)
    : m_verifier(verifier) {
}

AuthMiddleware::ptr AuthMiddleware::create(TokenVerifier verifier) {
    return std::shared_ptr<AuthMiddleware>(new AuthMiddleware(verifier));
}

AuthMiddleware::ptr AuthMiddleware::staticTokens(
        const std::map<std::string, AuthContext>& tokens) {
    return create([tokens](const std::string& token, AuthContext& ctx) mutable {
        auto it = tokens.find(token);
        if(it == tokens.end()) {
            return false;
        }
        ctx = it->second;
        ctx.authenticated = true;
        return true;
    });
}

AuthMiddleware::ptr AuthMiddleware::jwt(const std::string& secret,
                                        const std::string& issuer) {
    return create([secret, issuer](const std::string& token, AuthContext& ctx) {
        zero::JWTPayload payload;
        if(!zero::JWTUtil::verify(secret, token, payload)) {
            return false;
        }
        if(!issuer.empty() && payload.iss != issuer) {
            return false;
        }
        ctx.userId = payload.sub;
        ctx.role = payload.role;
        ctx.claims = payload.claims;
        ctx.authenticated = true;
        return true;
    });
}

int32_t AuthMiddleware::handle(HttpRequest::ptr request,
                               HttpResponse::ptr response,
                               HttpSession::ptr session) {
    std::string auth = request->getHeader(kAuthHeader);
    std::string token;
    const std::string prefix = "Bearer ";
    if(auth.substr(0, prefix.size()) == prefix) {
        token = auth.substr(prefix.size());
    } else {
        token = auth;
    }

    AuthContext ctx;
    if(token.empty() || !m_verifier || !m_verifier(token, ctx)) {
        ZERO_LOG_WARN(g_logger) << "Unauthorized request: " << request->getPath();
        response->setStatus(HttpStatus::UNAUTHORIZED);
        response->setHeader("Content-Type", "application/json");
        response->setBody("{\"error\":\"unauthorized\"}");
        return -1;
    }

    // 将认证信息注入请求头，供后续业务 Servlet 读取
    request->setHeader(kUserIdHeader, ctx.userId);
    request->setHeader(kUserRoleHeader, ctx.role);
    for(auto& kv : ctx.claims) {
        request->setHeader("X-Claim-" + kv.first, kv.second);
    }
    return 0;
}

// ============================================================
// RBACMiddleware
// ============================================================

RBACMiddleware::RBACMiddleware(const std::unordered_set<std::string>& allowedRoles,
                               const std::string& roleClaimKey)
    : m_allowedRoles(allowedRoles)
    , m_roleClaimKey(roleClaimKey) {
}

RBACMiddleware::ptr RBACMiddleware::create(const std::unordered_set<std::string>& allowedRoles,
                                           const std::string& roleClaimKey) {
    return std::shared_ptr<RBACMiddleware>(
        new RBACMiddleware(allowedRoles, roleClaimKey));
}

int32_t RBACMiddleware::handle(HttpRequest::ptr request,
                               HttpResponse::ptr response,
                               HttpSession::ptr session) {
    std::string role = request->getHeader(kUserRoleHeader);
    if(role.empty()) {
        role = request->getHeader("X-Claim-" + m_roleClaimKey);
    }
    if(m_allowedRoles.find(role) == m_allowedRoles.end()) {
        ZERO_LOG_WARN(g_logger) << "Forbidden request: " << request->getPath()
                                << " role=" << role;
        response->setStatus(HttpStatus::FORBIDDEN);
        response->setHeader("Content-Type", "application/json");
        response->setBody("{\"error\":\"forbidden\"}");
        return -1;
    }
    return 0;
}

} // namespace http
} // namespace zero

/**
 * @file permission_middleware.h
 * @brief 权限校验中间件
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_SECURITY_AUTH_PERMISSION_MIDDLEWARE_H__
#define __ZERO_SECURITY_AUTH_PERMISSION_MIDDLEWARE_H__

#include "zero/http/middleware.h"
#include <memory>
#include <string>
#include <unordered_set>

namespace zero {
namespace http {

/**
 * @brief 权限校验中间件
 *
 * 从请求头 X-Claim-{permissionClaimKey} 中解析用户权限列表，
 * 若包含任一 requiredPermissions 则放行，否则返回 403。
 */
class PermissionMiddleware : public Middleware {
public:
    typedef std::shared_ptr<PermissionMiddleware> ptr;

    /**
     * @brief 创建权限中间件
     * @param[in] requiredPermissions 所需权限集合（满足任一即可）
     * @param[in] permissionClaimKey  请求头后缀，默认 "permissions"
     */
    static PermissionMiddleware::ptr create(
        const std::unordered_set<std::string>& requiredPermissions,
        const std::string& permissionClaimKey = "permissions");

    int32_t handle(HttpRequest::ptr request,
                   HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    PermissionMiddleware(const std::unordered_set<std::string>& requiredPermissions,
                         const std::string& permissionClaimKey);

    std::unordered_set<std::string> m_requiredPermissions;
    std::string m_permissionClaimKey;
};

} // namespace http
} // namespace zero

#endif // __ZERO_SECURITY_AUTH_PERMISSION_MIDDLEWARE_H__

/**
 * @file permission_middleware.cc
 * @brief 权限校验中间件实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "permission_middleware.h"
#include "zero/core/log/log.h"

namespace zero {
namespace http {

PermissionMiddleware::ptr PermissionMiddleware::create(
    const std::unordered_set<std::string>& requiredPermissions,
    const std::string& permissionClaimKey) {
    return std::shared_ptr<PermissionMiddleware>(
        new PermissionMiddleware(requiredPermissions, permissionClaimKey));
}

PermissionMiddleware::PermissionMiddleware(
    const std::unordered_set<std::string>& requiredPermissions,
    const std::string& permissionClaimKey)
    : m_requiredPermissions(requiredPermissions),
      m_permissionClaimKey(permissionClaimKey) {
}

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

int32_t PermissionMiddleware::handle(HttpRequest::ptr request,
                                     HttpResponse::ptr response,
                                     HttpSession::ptr session) {
    (void)session;
    std::string permsStr = request->getHeader("X-Claim-" + m_permissionClaimKey);
    if (permsStr.empty()) {
        ZERO_LOG_WARN(g_logger) << "Forbidden request: " << request->getPath()
                                << " no permissions claim";
        response->setStatus(HttpStatus::FORBIDDEN);
        response->setBody("Forbidden: no permissions");
        return -1;
    }

    std::unordered_set<std::string> userPerms;
    std::stringstream ss(permsStr);
    std::string perm;
    while (std::getline(ss, perm, ',')) {
        // trim
        size_t start = perm.find_first_not_of(" \t");
        size_t end = perm.find_last_not_of(" \t");
        if (start != std::string::npos) {
            userPerms.insert(perm.substr(start, end - start + 1));
        }
    }

    for (const auto& required : m_requiredPermissions) {
        if (userPerms.find(required) != userPerms.end()) {
            return 0;
        }
    }

    ZERO_LOG_WARN(g_logger) << "Forbidden request: " << request->getPath()
                            << " permission denied";
    response->setStatus(HttpStatus::FORBIDDEN);
    response->setBody("Forbidden: permission denied");
    return -1;
}

} // namespace http
} // namespace zero

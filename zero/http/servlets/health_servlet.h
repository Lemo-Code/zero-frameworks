/**
 * @file health_servlet.h
 * @brief K8s 健康检查 Servlet
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_SERVLETS_HEALTH_H__
#define __ZERO_HTTP_SERVLETS_HEALTH_H__

#include "zero/http/servlet.h"
#include <mutex>
#include <vector>
#include <memory>

namespace zero {
namespace http {

/**
 * @brief 健康检查回调接口
 */
class HealthCheck {
public:
    typedef std::shared_ptr<HealthCheck> ptr;
    virtual ~HealthCheck() = default;
    /**
     * @return true 健康；false 不健康，message 填写原因
     */
    virtual bool check(std::string& message) = 0;
};

/**
 * @brief K8s 存活探针 /healthz
 * 只要服务器在运行就返回 200
 */
class HealthzServlet : public Servlet {
public:
    HealthzServlet();
    int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response,
                   HttpSession::ptr session) override;
};

/**
 * @brief K8s 就绪探针 /readyz
 * 支持注册自定义依赖检查
 */
class ReadyzServlet : public Servlet {
public:
    ReadyzServlet();
    void addCheck(const std::string& name, HealthCheck::ptr check);
    int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    std::vector<std::pair<std::string, HealthCheck::ptr>> m_checks;
    std::mutex m_mutex;
};

/**
 * @brief K8s 深度健康检查 /livez
 * 默认检查磁盘空间，支持注册自定义检查
 */
class LivezServlet : public Servlet {
public:
    LivezServlet();
    void addCheck(const std::string& name, HealthCheck::ptr check);
    int32_t handle(HttpRequest::ptr request, HttpResponse::ptr response,
                   HttpSession::ptr session) override;

private:
    std::vector<std::pair<std::string, HealthCheck::ptr>> m_checks;
    std::mutex m_mutex;
};

} // namespace http
} // namespace zero

#endif

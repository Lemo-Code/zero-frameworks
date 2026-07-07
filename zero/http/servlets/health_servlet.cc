/**
 * @file health_servlet.cc
 * @brief K8s 健康检查 Servlet 实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "health_servlet.h"
#include "zero/core/log/log.h"
#include <sstream>
#include <sys/statvfs.h>

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// ============================================================
// HealthzServlet - 存活探针
// ============================================================

HealthzServlet::HealthzServlet() : Servlet("HealthzServlet") {}

int32_t HealthzServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response,
                                 HttpSession::ptr session) {
    response->setStatus(HttpStatus::OK);
    response->setHeader("Content-Type", "application/json");
    response->setBody("{\"status\":\"up\"}");
    return 0;
}

// ============================================================
// ReadyzServlet - 就绪探针
// ============================================================

ReadyzServlet::ReadyzServlet() : Servlet("ReadyzServlet") {}

void ReadyzServlet::addCheck(const std::string& name, HealthCheck::ptr check) {
    if(!check) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_checks.emplace_back(name, check);
}

int32_t ReadyzServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response,
                              HttpSession::ptr session) {
    bool healthy = true;
    std::ostringstream details;
    details << "{";

    std::vector<std::pair<std::string, HealthCheck::ptr>> checks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        checks = m_checks;
    }

    for(size_t i = 0; i < checks.size(); ++i) {
        std::string message;
        bool ok = checks[i].second->check(message);
        if(i > 0) details << ",";
        details << "\"" << checks[i].first << "\":\"" << (ok ? "ok" : "fail")
                << " " << message << "\"";
        if(!ok) healthy = false;
    }

    if(checks.empty()) {
        details << "\"status\":\"ready\"";
    }
    details << "}";

    response->setHeader("Content-Type", "application/json");
    response->setBody(details.str());
    if(healthy) {
        response->setStatus(HttpStatus::OK);
    } else {
        response->setStatus(HttpStatus::SERVICE_UNAVAILABLE);
    }
    return 0;
}

// ============================================================
// LivezServlet - 深度健康检查
// ============================================================

LivezServlet::LivezServlet() : Servlet("LivezServlet") {}

void LivezServlet::addCheck(const std::string& name, HealthCheck::ptr check) {
    if(!check) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_checks.emplace_back(name, check);
}

int32_t LivezServlet::handle(HttpRequest::ptr request, HttpResponse::ptr response,
                             HttpSession::ptr session) {
    bool healthy = true;
    std::ostringstream details;
    details << "{";
    size_t fieldCount = 0;

    // 检查磁盘空间
    struct statvfs buf;
    if(statvfs("/", &buf) == 0) {
        unsigned long long free = (unsigned long long)buf.f_bfree * buf.f_bsize;
        unsigned long long total = (unsigned long long)buf.f_blocks * buf.f_bsize;
        double freePercent = total > 0 ? (double)free / total * 100.0 : 0.0;
        if(freePercent < 5.0) {
            healthy = false;
            details << "\"disk\":\"critical\"";
        } else {
            details << "\"disk\":\"ok\"";
        }
        fieldCount = 1;
    } else {
        details << "\"disk\":\"unknown\"";
        fieldCount = 1;
    }

    std::vector<std::pair<std::string, HealthCheck::ptr>> checks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        checks = m_checks;
    }
    for(auto& check : checks) {
        std::string message;
        bool ok = check.second->check(message);
        if(fieldCount++ > 0) details << ",";
        details << "\"" << check.first << "\":\"" << (ok ? "ok" : "fail")
                << " " << message << "\"";
        if(!ok) healthy = false;
    }

    details << "}";

    if(healthy) {
        response->setStatus(HttpStatus::OK);
    } else {
        response->setStatus(HttpStatus::SERVICE_UNAVAILABLE);
    }
    response->setHeader("Content-Type", "application/json");
    response->setBody(details.str());
    return 0;
}

} // namespace http
} // namespace zero

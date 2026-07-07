/**
 * @file status_servlet.cc
 * @brief 状态 Servlet 实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "status_servlet.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/log/log.h"
#include "zero/core/util/util.h"
#include "zero/core/concurrency/worker.h"
#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace zero {
namespace http {

StatusServlet::StatusServlet()
    :Servlet("StatusServlet") {
}

std::string format_used_time(int64_t ts) {
    std::stringstream ss;
    bool v = false;
    if(ts >= 3600 * 24) {
        ss << (ts / 3600 / 24) << "d ";
        ts = ts % (3600 * 24);
        v = true;
    }
    if(ts >= 3600) {
        ss << (ts / 3600) << "h ";
        ts = ts % 3600;
        v = true;
    } else if(v) {
        ss << "0h ";
    }

    if(ts >= 60) {
        ss << (ts / 60) << "m ";
        ts = ts % 60;
    } else if(v) {
        ss << "0m ";
    }
    ss << ts << "s";
    return ss.str();
}

int32_t StatusServlet::handle(zero::http::HttpRequest::ptr request
                              ,zero::http::HttpResponse::ptr response
                              ,zero::http::HttpSession::ptr session) {
    response->setHeader("Content-Type", "text/text; charset=utf-8");
#define XX(key) \
    ss << std::setw(30) << std::right << key ": "
    std::stringstream ss;
    ss << "===================================================" << std::endl;
    XX("server_version") << "zero-framework/1.0.0" << std::endl;
    XX("host") << GetHostName() << std::endl;
    XX("ipv4") << GetIPv4() << std::endl;
    XX("pid") << getpid() << std::endl;
    ss << "===================================================" << std::endl;
    XX("fibers") << zero::Fiber::TotalFibers() << std::endl;
    ss << "===================================================" << std::endl;
    ss << "<Logger>" << std::endl;
    ss << zero::LoggerMgr::GetInstance()->toYamlString() << std::endl;
    ss << "===================================================" << std::endl;
    ss << "<Woker>" << std::endl;
    zero::WorkerMgr::GetInstance()->dump(ss) << std::endl;

    response->setBody(ss.str());
    return 0;
}

}
}

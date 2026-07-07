/**
 * @file config_servlet.h
 * @brief 配置 Servlet 定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_SERVLETS_CONFIG_SERVLET_H__
#define __ZERO_HTTP_SERVLETS_CONFIG_SERVLET_H__

#include "zero/http/servlet.h"

namespace zero {
namespace http {

class ConfigServlet : public Servlet {
public:
    ConfigServlet();
    virtual int32_t handle(zero::http::HttpRequest::ptr request
                   , zero::http::HttpResponse::ptr response
                   , zero::http::HttpSession::ptr session) override;
};

}
}

#endif

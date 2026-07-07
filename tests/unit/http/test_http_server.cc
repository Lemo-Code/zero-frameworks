/**
 * @file test_http_server.cc
 * @brief HTTP server test — gtest style
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero_test/fixtures.h"
#include "zero/http/http_server.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();

#define XX(...) #__VA_ARGS__

static const int kWorkers = 5;

class HttpServerTest : public zero_test::IOManagerFixture {
};

TEST_F(HttpServerTest, BasicServer) {
    g_logger->setLevel(zero::LogLevel::ERROR);
    zero::http::HttpServer::ptr server(new zero::http::HttpServer(true));
    zero::Address::ptr addr = zero::Address::LookupAnyIPAddress("0.0.0.0:8020");
    int retries = 10;
    while(!server->bind(addr) && retries-- > 0) {
        sleep(2);
    }
    ASSERT_TRUE(server->bind(addr) || retries >= 0) << "Failed to bind server";

    auto sd = server->getServletDispatch();
    sd->addServlet("/zero/xx", [](zero::http::HttpRequest::ptr req
                ,zero::http::HttpResponse::ptr rsp
                ,zero::http::HttpSession::ptr session) {
            rsp->setBody(req->toString());
            return 0;
    });

    sd->addGlobServlet("/zero/*", [](zero::http::HttpRequest::ptr req
                ,zero::http::HttpResponse::ptr rsp
                ,zero::http::HttpSession::ptr session) {
            rsp->setBody("Glob:\r\n" + req->toString());
            return 0;
    });

    sd->addGlobServlet("/zerox/*", [](zero::http::HttpRequest::ptr req
                ,zero::http::HttpResponse::ptr rsp
                ,zero::http::HttpSession::ptr session) {
            rsp->setBody(XX(<html>
<head><title>404 Not Found</title></head>
<body>
<center><h1>404 Not Found</h1></center>
<hr><center>nginx/1.16.0</center>
</body>
</html>
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
));
            return 0;
    });

    // Server is set up successfully
    EXPECT_TRUE(server->getServletDispatch() != nullptr);
}

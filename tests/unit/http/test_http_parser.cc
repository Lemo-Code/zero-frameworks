/**
 * @file test_http_parser.cc
 * @brief HTTP 模块 - 单元测试 - test_http_parser
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/http/http.h"
#include "zero/http/http_parser.h"

using namespace zero;
using namespace zero::http;

TEST(HttpRequestParser, ParseGetRequest) {
    const char* raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    HttpRequestParser parser;
    size_t n = parser.execute(raw, strlen(raw));
    EXPECT_GT(n, 0u);
    EXPECT_TRUE(parser.isFinished());

    auto req = parser.getData();
    EXPECT_EQ(req->getMethod(), HttpMethod::GET);
    EXPECT_EQ(req->getPath(), "/hello");
    EXPECT_EQ(req->getHeader("Host"), "localhost");
}

TEST(HttpRequestParser, ParsePostWithContentLength) {
    const char* raw =
        "POST /api/data HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "hello world";

    HttpRequestParser parser;
    size_t n = parser.execute(raw, strlen(raw));
    EXPECT_GT(n, 0u);
    EXPECT_TRUE(parser.isFinished());

    auto req = parser.getData();
    EXPECT_EQ(req->getMethod(), HttpMethod::POST);
    EXPECT_EQ(req->getHeader("Content-Length"), "11");
    // Body is consumed after headers by the HTTP session, not by the parser.
}

TEST(HttpResponseParser, ParseResponse) {
    char raw[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    HttpResponseParser parser;
    size_t n = parser.execute(raw, strlen(raw), false);
    EXPECT_GT(n, 0u);
    EXPECT_TRUE(parser.isFinished());

    auto resp = parser.getData();
    EXPECT_EQ(resp->getStatus(), HttpStatus::OK);
    EXPECT_EQ(resp->getHeader("Content-Length"), "5");
}

TEST(HttpRequest, SettersAndGetters) {
    HttpRequest req;
    req.setMethod(HttpMethod::POST);
    req.setPath("/test");
    req.setHeader("X-Custom", "value");
    req.setBody("body");

    EXPECT_EQ(req.getMethod(), HttpMethod::POST);
    EXPECT_EQ(req.getPath(), "/test");
    EXPECT_EQ(req.getHeader("X-Custom"), "value");
    EXPECT_EQ(req.getBody(), "body");
}

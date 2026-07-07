/**
 * @file test_http2.cc
 * @brief HTTP/2 基础测试 — gtest style
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/http/http2.h"
#include "zero/http/hpack.h"
#include <cstring>

using namespace zero;
using namespace zero::http::http2;

TEST(Http2, FrameHeaderEncodeDecode) {
    FrameHeader header;
    header.length = 10;
    header.type = SETTINGS;
    header.flags = 0;
    header.streamId = 5;

    uint8_t buf[FrameHeader::kSize];
    header.encode(buf);

    FrameHeader decoded;
    ASSERT_TRUE(decoded.decode(buf, FrameHeader::kSize));
    EXPECT_EQ(decoded.length, 10u);
    EXPECT_EQ(decoded.type, SETTINGS);
    EXPECT_EQ(decoded.streamId, 5u);
}

TEST(Http2, Preface) {
    auto conn = Http2Connection::create();
    const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    (void)preface;
    ASSERT_TRUE(conn->onFrame((const uint8_t*)preface, strlen(preface)));
    EXPECT_TRUE(conn->receivedClientPreface());
}

TEST(Http2, Settings) {
    auto conn = Http2Connection::create();
    const char* preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    (void)preface;
    ASSERT_TRUE(conn->onFrame((const uint8_t*)preface, strlen(preface)));

    auto settings = conn->sendLocalSettings();
    EXPECT_FALSE(settings.empty());
}

TEST(Http2, HPackStaticTable) {
    EXPECT_EQ(HPack::lookupStaticTable(":method", "GET"), 2);
    EXPECT_EQ(HPack::lookupStaticTable(":status", "200"), 8);
    EXPECT_EQ(HPack::lookupStaticTable("content-type", "application/json"), 0);
}

TEST(Http2, HPackEncodeDecode) {
    std::map<std::string, std::string> headers;
    headers[":method"] = "GET";
    headers[":path"] = "/";
    headers["content-type"] = "application/json";

    auto encoded = HPack::encode(headers);
    EXPECT_FALSE(encoded.empty());

    std::map<std::string, std::string> decoded;
    ASSERT_TRUE(HPack::decode(encoded.data(), encoded.size(), decoded));
    EXPECT_EQ(decoded[":method"], "GET");
    EXPECT_EQ(decoded[":path"], "/");
    EXPECT_EQ(decoded["content-type"], "application/json");
}

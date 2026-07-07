/**
 * @file test_kv_resp.cc
 * @brief KV 模块 - 单元测试 - test_kv_resp
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/resp.h"
#include "zero/kv/resp_reader.h"

#include <cstring>
#include <string>

using namespace zero::kv;

static RespValue arrayCmd(std::initializer_list<std::string> args) {
    RespValue req;
    req.type = RespType::Array;
    for(const auto& s : args) {
        req.array.push_back(RespEncoder::bulk(s));
    }
    return req;
}

TEST(RespReader, ParseGet) {
    const char req[] = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    RespValue out;
    size_t consumed = 0;
    RespReader reader(req, sizeof(req) - 1);
    EXPECT_EQ(reader.tryParse(out, &consumed), ParseStatus::Ok);
    EXPECT_EQ(out.type, RespType::Array);
    EXPECT_EQ(out.array.size(), 2u);
    EXPECT_EQ(out.array[0].str, "GET");
    EXPECT_EQ(out.array[1].str, "foo");
    EXPECT_EQ(consumed, sizeof(req) - 1);
}

TEST(RespReader, ParseInlinePing) {
    const char req[] = "PING\r\n";
    RespValue out;
    size_t consumed = 0;
    RespReader reader(req, sizeof(req) - 1);
    EXPECT_EQ(reader.tryParse(out, &consumed), ParseStatus::Ok);
    EXPECT_EQ(out.type, RespType::Array);
    EXPECT_EQ(out.array.size(), 1u);
    EXPECT_EQ(out.array[0].str, "PING");
}

TEST(RespEncoder, EncodePong) {
    std::string encoded = RespEncoder::encode(RespEncoder::pong());
    EXPECT_EQ(encoded, "+PONG\r\n");
}

TEST(RespReader, PipelineTwoCommands) {
    const char req[] =
        "*2\r\n$3\r\nGET\r\n$1\r\na\r\n"
        "*2\r\n$3\r\nGET\r\n$1\r\nb\r\n";
    size_t pos = 0;
    const size_t total = sizeof(req) - 1;

    RespValue first;
    size_t consumed = 0;
    RespReader r1(req + pos, total - pos);
    EXPECT_EQ(r1.tryParse(first, &consumed), ParseStatus::Ok);
    pos += consumed;

    RespValue second;
    RespReader r2(req + pos, total - pos);
    EXPECT_EQ(r2.tryParse(second, &consumed), ParseStatus::Ok);
    EXPECT_EQ(second.array[1].str, "b");
}

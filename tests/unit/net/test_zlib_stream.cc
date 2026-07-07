/**
 * @file test_zlib_stream.cc
 * @brief 网络模块 - 单元测试 - test_zlib_stream
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/net/streams/zlib_stream.h"
#include "zero/core/io/bytearray.h"

using namespace zero;

TEST(ZlibStream, GzipRoundTrip) {
    std::string original(1024, 'a');
    auto encode = ZlibStream::CreateGzip(true);
    encode->write(original.data(), original.size());
    encode->flush();

    std::string compressed = encode->getResult();
    EXPECT_GT(compressed.size(), 0u);
    EXPECT_LT(compressed.size(), original.size());

    auto decode = ZlibStream::CreateGzip(false);
    decode->write(compressed.data(), compressed.size());
    decode->flush();

    std::string output = decode->getResult();
    EXPECT_EQ(output, original);
}

TEST(ZlibStream, ZlibRoundTrip) {
    std::string original = "short text";
    auto encode = ZlibStream::CreateZlib(true);
    encode->write(original.data(), original.size());
    encode->flush();

    std::string compressed = encode->getResult();
    EXPECT_GT(compressed.size(), 0u);

    auto decode = ZlibStream::CreateZlib(false);
    decode->write(compressed.data(), compressed.size());
    decode->flush();

    std::string output = decode->getResult();
    EXPECT_EQ(output, original);
}

TEST(ZlibStream, DeflateRoundTrip) {
    std::string original = "raw deflate data";
    auto encode = ZlibStream::Create(true, 1024, ZlibStream::DEFLATE);
    encode->write(original.data(), original.size());
    encode->flush();

    std::string compressed = encode->getResult();
    EXPECT_GT(compressed.size(), 0u);

    auto decode = ZlibStream::Create(false, 1024, ZlibStream::DEFLATE);
    decode->write(compressed.data(), compressed.size());
    decode->flush();

    std::string output = decode->getResult();
    EXPECT_EQ(output, original);
}

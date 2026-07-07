/**
 * @file test_util.cc
 * @brief 核心模块 - 单元测试 - test_util
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/util/util.h"
#include <vector>
#include <string>

using namespace zero;

TEST(UtilString, ToUpper) {
    EXPECT_EQ(ToUpper("hello"), "HELLO");
    EXPECT_EQ(ToUpper("Hello World 123"), "HELLO WORLD 123");
    EXPECT_EQ(ToUpper(""), "");
}

TEST(UtilString, ToLower) {
    EXPECT_EQ(ToLower("HELLO"), "hello");
    EXPECT_EQ(ToLower("Hello World 123"), "hello world 123");
    EXPECT_EQ(ToLower(""), "");
}

TEST(UtilString, Trim) {
    EXPECT_EQ(StringUtil::Trim("  hello  "), "hello");
    EXPECT_EQ(StringUtil::Trim("\t\nhello\r\n"), "hello");
    EXPECT_EQ(StringUtil::Trim("hello"), "hello");
    EXPECT_EQ(StringUtil::Trim(""), "");
    EXPECT_EQ(StringUtil::Trim("xxhelloxx", "x"), "hello");
}

TEST(UtilString, TrimLeft) {
    EXPECT_EQ(StringUtil::TrimLeft("  hello  "), "hello  ");
    EXPECT_EQ(StringUtil::TrimLeft(""), "");
}

TEST(UtilString, TrimRight) {
    EXPECT_EQ(StringUtil::TrimRight("  hello  "), "  hello");
    EXPECT_EQ(StringUtil::TrimRight(""), "");
}

TEST(UtilString, Atoi) {
    EXPECT_EQ(TypeUtil::Atoi("123"), 123);
    EXPECT_EQ(TypeUtil::Atoi("-456"), -456);
    EXPECT_EQ(TypeUtil::Atoi("0"), 0);
    EXPECT_EQ(TypeUtil::Atoi("not_a_number"), 0);
}

TEST(UtilString, Atof) {
    EXPECT_DOUBLE_EQ(TypeUtil::Atof("3.14"), 3.14);
    EXPECT_DOUBLE_EQ(TypeUtil::Atof("-2.5"), -2.5);
    EXPECT_DOUBLE_EQ(TypeUtil::Atof("invalid"), 0.0);
}

TEST(UtilString, UrlEncodeDecode) {
    std::string raw = "hello world!";
    std::string encoded = StringUtil::UrlEncode(raw, true);
    EXPECT_NE(encoded, raw);
    EXPECT_EQ(StringUtil::UrlDecode(encoded, true), raw);

    std::string special = "a+b=c&d?e";
    std::string enc = StringUtil::UrlEncode(special, false);
    EXPECT_EQ(StringUtil::UrlDecode(enc, false), special);
}

TEST(UtilTime, GetCurrentMSUS) {
    uint64_t ms1 = GetCurrentMS();
    uint64_t us1 = GetCurrentUS();
    EXPECT_GT(ms1, 0u);
    EXPECT_GT(us1, 0u);
    EXPECT_GE(us1, ms1 * 1000u);
}

TEST(UtilBacktrace, BacktraceToStringNotEmpty) {
    std::string bt = BacktraceToString(16, 1, "  ");
    EXPECT_FALSE(bt.empty());
    // Should contain at least the current function name (demangled).
    EXPECT_NE(bt.find("BacktraceToStringNotEmpty"), std::string::npos);
}

TEST(UtilString, Format) {
    EXPECT_EQ(StringUtil::Format("%s-%d", "test", 42), "test-42");
    EXPECT_EQ(StringUtil::Format("%%"), "%");
}

TEST(UtilFileSystem, DirnameBasename) {
    EXPECT_EQ(FSUtil::Dirname("/a/b/c.txt"), "/a/b");
    EXPECT_EQ(FSUtil::Basename("/a/b/c.txt"), "c.txt");
    EXPECT_EQ(FSUtil::Basename("c.txt"), "c.txt");
}

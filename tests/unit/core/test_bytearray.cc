/**
 * @file test_bytearray.cc
 * @brief 核心模块 - 单元测试 - test_bytearray
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/core/io/bytearray.h"
#include <vector>

using namespace zero;

class ByteArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        ba = std::make_shared<ByteArray>(1024);
    }
    ByteArray::ptr ba;
};

TEST_F(ByteArrayTest, ReadWriteFint8) {
    ba->writeFint8(-5);
    ba->setPosition(0);
    EXPECT_EQ(ba->readFint8(), -5);
}

TEST_F(ByteArrayTest, ReadWriteFint16) {
    ba->writeFint16(-1234);
    ba->setPosition(0);
    EXPECT_EQ(ba->readFint16(), -1234);
}

TEST_F(ByteArrayTest, ReadWriteFint32) {
    ba->writeFint32(-12345678);
    ba->setPosition(0);
    EXPECT_EQ(ba->readFint32(), -12345678);
}

TEST_F(ByteArrayTest, ReadWriteFint64) {
    ba->writeFint64(-123456789012345LL);
    ba->setPosition(0);
    EXPECT_EQ(ba->readFint64(), -123456789012345LL);
}

TEST_F(ByteArrayTest, ReadWriteUint32) {
    ba->writeUint32(42);
    ba->setPosition(0);
    EXPECT_EQ(ba->readUint32(), 42u);
}

TEST_F(ByteArrayTest, ReadWriteFloatDouble) {
    ba->writeFloat(3.14f);
    ba->writeDouble(2.718281828);
    ba->setPosition(0);
    EXPECT_FLOAT_EQ(ba->readFloat(), 3.14f);
    EXPECT_DOUBLE_EQ(ba->readDouble(), 2.718281828);
}

TEST_F(ByteArrayTest, ReadWriteStringVint) {
    std::string s = "hello, zero-framework!";
    ba->writeStringVint(s);
    ba->setPosition(0);
    EXPECT_EQ(ba->readStringVint(), s);
}

TEST_F(ByteArrayTest, ReadWriteStringWithoutLength) {
    std::string s = "fixed";
    ba->writeStringWithoutLength(s);
    ba->setPosition(0);
    char buf[16] = {0};
    ba->read(buf, s.size());
    EXPECT_EQ(std::string(buf, s.size()), s);
}

TEST_F(ByteArrayTest, PositionBounds) {
    ba->writeStringVint("data");
    EXPECT_EQ(ba->getReadSize(), ba->getSize() - ba->getPosition());
    // Capacity equals base_size (1024) initially; position beyond it throws.
    EXPECT_THROW(ba->setPosition(1025), std::exception);
}

TEST_F(ByteArrayTest, Clear) {
    ba->writeStringVint("clear me");
    ba->clear();
    EXPECT_EQ(ba->getSize(), 0u);
    EXPECT_EQ(ba->getPosition(), 0u);
}

TEST_F(ByteArrayTest, Endianness) {
    // Default endianness is big-endian.
    EXPECT_FALSE(ba->isLittleEndian());
    ba->setIsLittleEndian(true);
    EXPECT_TRUE(ba->isLittleEndian());
    ba->setIsLittleEndian(false);
}

TEST_F(ByteArrayTest, ToStringAndHex) {
    ba->writeStringWithoutLength("ABC");
    ba->setPosition(0);
    EXPECT_EQ(ba->toString(), "ABC");
    std::string hex = ba->toHexString();
    EXPECT_FALSE(hex.empty());
}

TEST_F(ByteArrayTest, WriteToAndReadFromFile) {
    std::string path = "/tmp/test_bytearray_file.bin";
    ba->writeStringVint("file payload");
    // Stream-style write: set position to start before writing the whole buffer.
    ba->setPosition(0);
    EXPECT_TRUE(ba->writeToFile(path));

    ByteArray::ptr ba2 = std::make_shared<ByteArray>(1024);
    EXPECT_TRUE(ba2->readFromFile(path));
    ba2->setPosition(0);
    EXPECT_EQ(ba2->readStringVint(), "file payload");

    std::remove(path.c_str());
}

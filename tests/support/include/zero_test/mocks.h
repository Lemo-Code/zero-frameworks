/**
 * @file mocks.h
 * @brief 测试支持 - mocks
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef ZERO_TEST_MOCKS_H
#define ZERO_TEST_MOCKS_H

#include <gmock/gmock.h>
#include "zero/core/io/stream.h"
#include "zero/core/io/socket.h"
#include "zero/db/db_session.h"
#include "zero/registry/registry.h"
#include "zero/config/config_center.h"

namespace zero_test {

class MockStream : public zero::Stream {
public:
    MOCK_METHOD(int, read, (void* buffer, size_t length), (override));
    MOCK_METHOD(int, read, (zero::ByteArray::ptr ba, size_t length), (override));
    MOCK_METHOD(int, write, (const void* buffer, size_t length), (override));
    MOCK_METHOD(int, write, (zero::ByteArray::ptr ba, size_t length), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(zero::Address::ptr, getRemoteAddress, (), (override));
    MOCK_METHOD(zero::Address::ptr, getLocalAddress, (), (override));
    MOCK_METHOD(std::string, getRemoteAddressString, (), (override));
    MOCK_METHOD(std::string, getLocalAddressString, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
};

class MockDbSession : public zero::orm::DbSession {
public:
    MOCK_METHOD(std::vector<zero::orm::DbRow>, query,
                (const std::string& sql, const std::vector<std::string>& params),
                (override));
    MOCK_METHOD(uint64_t, execute,
                (const std::string& sql, const std::vector<std::string>& params),
                (override));
    MOCK_METHOD(uint64_t, insert,
                (const std::string& sql, const std::vector<std::string>& params),
                (override));
    MOCK_METHOD(bool, begin, (), (override));
    MOCK_METHOD(bool, commit, (), (override));
    MOCK_METHOD(bool, rollback, (), (override));
    MOCK_METHOD(bool, isConnected, (), (override));
};

class MockConfigCenter : public zero::ConfigCenter {
public:
    MOCK_METHOD(std::string, get,
                (const std::string& key, const std::string& def), (override));
    MOCK_METHOD(bool, set, (const std::string& key, const std::string& value), (override));
    MOCK_METHOD(void, addListener,
                (const std::string& key, zero::ConfigListener listener), (override));
    MOCK_METHOD(std::map<std::string, std::string>, getAll, (), (override));
};

} // namespace zero_test

#endif // ZERO_TEST_MOCKS_H

/**
 * @file service_helpers.h
 * @brief 测试支持 - service_helpers
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef ZERO_TEST_SERVICE_HELPERS_H
#define ZERO_TEST_SERVICE_HELPERS_H

#include <string>

namespace zero_test {

// Returns true if the TCP endpoint is reachable within timeout_ms.
bool is_reachable(const std::string& host, int port, int timeout_ms = 500);

// Convenience helpers for common external services used by tests.
inline bool require_etcd()    { return is_reachable("127.0.0.1", 2379); }
inline bool require_mysql()   { return is_reachable("127.0.0.1", 3306); }
inline bool require_redis()   { return is_reachable("127.0.0.1", 6379); }
inline bool require_zookeeper(){ return is_reachable("127.0.0.1", 2181); }
inline bool require_kafka()   { return is_reachable("127.0.0.1", 9092); }

} // namespace zero_test

#endif // ZERO_TEST_SERVICE_HELPERS_H

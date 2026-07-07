/**
 * @file test_kv_info.cc
 * @brief KV 模块 - 单元测试 - test_kv_info
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/kv/info.h"
#include "zero/kv/store/kv_store.h"

#include <string>

using namespace zero::kv;

TEST(KvInfo, TextAndJson) {
    KvStore::ptr store(new KvStore);
    store->setRdbPath("/tmp/dump.rdb");
    store->set(0, "k", "v");

    const std::string text = buildInfoText(0, store);
    EXPECT_NE(text.find("redis_engine:zero-redis"), std::string::npos);
    EXPECT_NE(text.find("keys=1"), std::string::npos);
    EXPECT_NE(text.find("rdb_path:/tmp/dump.rdb"), std::string::npos);

    const std::string json = buildInfoJson(0, store);
    EXPECT_TRUE(json.find("\"keys\":1") != std::string::npos || json.find("\"keys\": 1") != std::string::npos);
    EXPECT_NE(json.find("zero-redis"), std::string::npos);
}

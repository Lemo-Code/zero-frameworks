/**
 * @file rdb.h
 * @brief zero-redis JSON RDB 快照（启动加载 / SAVE / 定时落盘）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_PERSISTENCE_RDB_H__
#define __ZERO_KV_PERSISTENCE_RDB_H__

#include "zero/kv/store/kv_store.h"
#include <string>

namespace zero {
namespace kv {

class Rdb {
public:
    static bool save(const KvStore& store, const std::string& path, std::string* err = nullptr);
    static bool load(KvStore& store, const std::string& path, std::string* err = nullptr);
    static bool dump(const KvStore& store, std::string& data, std::string* err = nullptr);
    static bool loadFromString(KvStore& store, const std::string& data, std::string* err = nullptr);
    static bool dumpKey(const KvStore& store, int db, const std::string& key, int64_t& ttl_ms,
                        std::string& payload, bool& found, std::string* err = nullptr);
    static bool restoreKey(KvStore& store, int db, const std::string& key, int64_t ttl_ms,
                           const std::string& payload, bool replace, std::string* err = nullptr);

private:
    static void writeStreamEntry(const KvStore::Entry& e, std::string& out);
    static bool readStreamEntry(const char*& p, const char* end, KvStore::Entry& e);
    static bool skipEntryPayload(const char*& p, const char* end, KvStore::ObjType type);
    static bool dumpBinaryV2(const KvStore& store, std::string& data, std::string* err);
    static bool loadBinaryV2(KvStore& store, const std::string& data, std::string* err);
    static bool loadFromJsonV1(KvStore& store, const std::string& data, std::string* err);
    static void writeEntryPayload(const KvStore::Entry& e, std::string& out);
    static bool readEntryPayload(const char*& p, const char* end, KvStore::Entry& e,
                                 KvStore::ObjType type);
};

} // namespace kv
} // namespace zero

#endif

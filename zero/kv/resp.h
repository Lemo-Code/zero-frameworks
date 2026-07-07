/**
 * @file resp.h
 * @brief Redis RESP 类型与编解码
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_RESP_H__
#define __ZERO_KV_RESP_H__

#include <cstdint>
#include <string>
#include <vector>

namespace zero {
namespace kv {

// Pre-encoded RESP constants — avoids encodeInto() overhead on hot paths
extern const std::string kRespOk;          // "+OK\r\n"
extern const std::string kRespOne;         // ":1\r\n"
extern const std::string kRespZero;        // ":0\r\n"
extern const std::string kRespNullBulk;    // "$-1\r\n"
extern const std::string kRespEmptyArray;  // "*0\r\n"
extern const std::string kRespPong;        // "+PONG\r\n"
extern const std::string kRespQueued;      // "+QUEUED\r\n"

// Pre-encoded bulk string length headers for common small sizes (1-16 bytes)
// e.g. kRespBulkHdr[3] == "$3\r\n" — avoids appendInt64 for tiny payloads
extern const std::string kRespBulkHdr[17];

enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Null
};

struct RespValue {
    RespType type = RespType::Null;
    std::string str;
    int64_t integer = 0;
    std::vector<RespValue> array;
    bool is_null = false;
};

class RespEncoder {
public:
    static void encodeInto(const RespValue& v, std::string& out);
    static std::string encode(const RespValue& v);

    static RespValue ok();
    static RespValue err(const std::string& msg);
    static RespValue bulk(const std::string& s);
    static RespValue bulk(std::string&& s);  // move overload — avoids copy in hot path
    static RespValue integer(int64_t v);
    static RespValue pong() {
        RespValue v;
        v.type = RespType::SimpleString;
        v.str = "PONG";
        return v;
    }

    static RespValue emptyArray();
    static RespValue nullBulk();
    static RespValue queued();
    static RespValue none();
};

} // namespace kv
} // namespace zero

#endif

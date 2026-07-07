/**
 * @file resp.cc
 * @brief RESP 协议编码实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "resp.h"

namespace zero {
namespace kv {

// Pre-encoded RESP byte strings — direct-append to write buffer
const std::string kRespOk("+OK\r\n", 5);
const std::string kRespOne(":1\r\n", 4);
const std::string kRespZero(":0\r\n", 4);
const std::string kRespNullBulk("$-1\r\n", 5);
const std::string kRespEmptyArray("*0\r\n", 4);
const std::string kRespPong("+PONG\r\n", 7);
const std::string kRespQueued("+QUEUED\r\n", 9);

// Pre-encoded bulk string length headers for sizes 0-16
// e.g. kRespBulkHdr[3] == "$3\r\n"
const std::string kRespBulkHdr[17] = {
    "$0\r\n",  "$1\r\n",  "$2\r\n",  "$3\r\n",
    "$4\r\n",  "$5\r\n",  "$6\r\n",  "$7\r\n",
    "$8\r\n",  "$9\r\n",  "$10\r\n", "$11\r\n",
    "$12\r\n", "$13\r\n", "$14\r\n", "$15\r\n",
    "$16\r\n",
};

// Pre-encoded RESP type prefixes — avoids per-character append
static const char kPrefixSimple = '+';
static const char kPrefixError   = '-';
static const char kPrefixInteger = ':';
static const char kPrefixBulk    = '$';
static const char kPrefixArray   = '*';
static const char kCrlf[2] = {'\r', '\n'};

namespace {

// Fast int64-to-string append — stack buffer, no heap allocation.
// Handles INT64_MIN correctly.
void appendInt64(std::string& out, int64_t n) {
    // Branch for common small integers to avoid the full loop
    if (n == 0) {
        out.append("0", 1);
        return;
    }
    if (n == 1) {
        out.append("1", 1);
        return;
    }
    if (n > 0 && n < 10) {
        out.push_back('0' + (char)n);
        return;
    }

    char buf[21]; // max 20 digits + sign for int64
    int pos = 20;
    buf[pos] = '\0';

    bool neg = n < 0;
    if (neg) {
        // Handle INT64_MIN: work with positive remainder after first digit
        int64_t q = n / 10;
        int64_t r = n % 10;
        if (r < 0) { r = -r; q++; } // q is already negative, so add 1 to compensate
        buf[pos--] = '0' + (char)r;
        n = -q; // now n >= 0
    }
    do {
        buf[pos--] = '0' + (char)(n % 10);
        n /= 10;
    } while (n > 0);
    if (neg) buf[pos--] = '-';
    out.append(buf + pos + 1, 20 - pos);
}

} // namespace

void RespEncoder::encodeInto(const RespValue& v, std::string& out) {
    switch(v.type) {
        case RespType::SimpleString:
            out.push_back(kPrefixSimple);
            out.append(v.str);
            out.append(kCrlf, 2);
            break;
        case RespType::Error:
            out.push_back(kPrefixError);
            out.append(v.str);
            out.append(kCrlf, 2);
            break;
        case RespType::Integer:
            out.push_back(kPrefixInteger);
            appendInt64(out, v.integer);
            out.append(kCrlf, 2);
            break;
        case RespType::BulkString:
            if(v.is_null) {
                out.append(kRespNullBulk);
            } else {
                const size_t len = v.str.size();
                if(len <= 16) {
                    out.append(kRespBulkHdr[len]);
                } else {
                    out.push_back(kPrefixBulk);
                    appendInt64(out, (int64_t)len);
                    out.append(kCrlf, 2);
                }
                out.append(v.str);
                out.append(kCrlf, 2);
            }
            break;
        case RespType::Array:
            out.push_back(kPrefixArray);
            appendInt64(out, (int64_t)v.array.size());
            out.append(kCrlf, 2);
            for(const auto& item : v.array) {
                encodeInto(item, out);
            }
            break;
        case RespType::Null:
            out.append(kRespNullBulk);
            break;
    }
}

std::string RespEncoder::encode(const RespValue& v) {
    std::string out;
    encodeInto(v, out);
    return out;
}

RespValue RespEncoder::ok() {
    RespValue v;
    v.type = RespType::SimpleString;
    v.str = "OK";
    return v;
}

RespValue RespEncoder::err(const std::string& msg) {
    RespValue v;
    v.type = RespType::Error;
    v.str = msg;
    return v;
}

RespValue RespEncoder::bulk(const std::string& s) {
    RespValue v;
    v.type = RespType::BulkString;
    v.str = s;
    return v;
}

RespValue RespEncoder::bulk(std::string&& s) {
    RespValue v;
    v.type = RespType::BulkString;
    v.str = std::move(s);
    return v;
}

RespValue RespEncoder::integer(int64_t v) {
    RespValue r;
    r.type = RespType::Integer;
    r.integer = v;
    return r;
}

RespValue RespEncoder::nullBulk() {
    RespValue v;
    v.type = RespType::BulkString;
    v.is_null = true;
    return v;
}

RespValue RespEncoder::emptyArray() {
    RespValue v;
    v.type = RespType::Array;
    return v;
}

RespValue RespEncoder::queued() {
    RespValue v;
    v.type = RespType::SimpleString;
    v.str = "QUEUED";
    return v;
}

RespValue RespEncoder::none() {
    RespValue v;
    v.type = RespType::Null;
    return v;
}

} // namespace kv
} // namespace zero

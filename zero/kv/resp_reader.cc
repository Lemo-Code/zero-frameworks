/**
 * @file resp_reader.cc
 * @brief RESP 协议解码实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "resp_reader.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace zero {
namespace kv {

namespace {

// memchr-based CRLF scan — heavily optimized (SIMD on modern CPUs)
const char* find_crlf(const char* p, const char* end) {
    while(p + 1 < end) {
        const char* found = (const char*)memchr(p, '\r', end - p - 1);
        if(!found) return nullptr;
        if(found[1] == '\n') return found;
        p = found + 1;
    }
    return nullptr;
}

// Parse int64 directly from buffer, avoiding intermediate std::string (Phase 6)
// Returns pointer after the last consumed character, or nullptr on failure.
// Does NOT consume trailing \r\n — caller must verify those.
const char* readInt64(const char* p, const char* end, int64_t& out) {
    if(p >= end) return nullptr;
    bool neg = false;
    if(*p == '-') { neg = true; ++p; }
    else if(*p == '+') { ++p; }
    if(p >= end || *p < '0' || *p > '9') return nullptr;
    int64_t val = 0;
    do {
        int64_t digit = *p - '0';
        // Overflow check
        if(val > (INT64_MAX - digit) / 10) return nullptr;
        val = val * 10 + digit;
        ++p;
    } while(p < end && *p >= '0' && *p <= '9');
    out = neg ? -val : val;
    return p;
}

} // namespace

RespReader::RespReader(const char* data, size_t len)
    :m_data(data)
    ,m_len(len) {
}

bool RespReader::readLine(size_t& pos, std::string& line) const {
    if(pos >= m_len) {
        return false;
    }
    const char* crlf = find_crlf(m_data + pos, m_data + m_len);
    if(!crlf) {
        return false;
    }
    line.assign(m_data + pos, (size_t)(crlf - (m_data + pos)));
    pos = (size_t)(crlf - m_data) + 2;
    return true;
}

bool RespReader::isRespPrefix(char c) {
    return c == '*' || c == '$' || c == '+' || c == '-' || c == ':';
}

ParseStatus RespReader::parseInline(size_t& pos, RespValue& out) {
    const size_t line_start = pos;
    std::string line;
    if(!readLine(pos, line)) {
        pos = line_start;
        return ParseStatus::NeedMore;
    }
    if(line.empty()) {
        return ParseStatus::Error;
    }

    out.type = RespType::Array;
    out.array.clear();
    size_t i = 0;
    while(i < line.size()) {
        while(i < line.size() && std::isspace((unsigned char)line[i])) {
            ++i;
        }
        if(i >= line.size()) {
            break;
        }
        size_t j = i;
        while(j < line.size() && !std::isspace((unsigned char)line[j])) {
            ++j;
        }
        RespValue arg;
        arg.type = RespType::BulkString;
        arg.str.assign(line.data() + i, j - i);
        out.array.push_back(std::move(arg));
        i = j;
    }
    if(out.array.empty()) {
        return ParseStatus::Error;
    }
    return ParseStatus::Ok;
}

ParseStatus RespReader::parseValue(size_t& pos, RespValue& out) {
    if(pos >= m_len) {
        return ParseStatus::NeedMore;
    }
    const char prefix = m_data[pos++];
    std::string line;
    switch(prefix) {
        case '+':
        case '-':
            if(!readLine(pos, line)) {
                return ParseStatus::NeedMore;
            }
            if(prefix == '+') {
                out.type = RespType::SimpleString;
                out.str = line;
            } else {
                out.type = RespType::Error;
                out.str = line;
            }
            return ParseStatus::Ok;
        case ':': {
            // Fast-path: parse integer directly from buffer (Phase 6)
            const char* num_start = m_data + pos;
            int64_t val = 0;
            const char* num_end = readInt64(num_start, m_data + m_len, val);
            if(!num_end) {
                return ParseStatus::Error;
            }
            if(num_end + 1 >= m_data + m_len || num_end[0] != '\r' || num_end[1] != '\n') {
                return ParseStatus::NeedMore;
            }
            out.type = RespType::Integer;
            out.integer = val;
            pos = (size_t)(num_end - m_data) + 2;
            return ParseStatus::Ok;
        }
        case '$': {
            const size_t bulk_start = pos - 1;
            // Fast-path: parse bulk length directly from buffer (Phase 6)
            const char* num_start = m_data + pos;
            int64_t bulk_len_val = 0;
            const char* num_end = readInt64(num_start, m_data + m_len, bulk_len_val);
            if(!num_end) {
                return ParseStatus::Error;
            }
            // Must have \r\n after the length
            if(num_end + 1 >= m_data + m_len || num_end[0] != '\r' || num_end[1] != '\n') {
                if(num_end >= m_data + m_len) {
                    pos = bulk_start;
                    return ParseStatus::NeedMore;
                }
                // CRLF not found exactly where expected
            }
            pos = (size_t)(num_end - m_data) + 2; // advance past CRLF
            if(bulk_len_val < 0) {
                out.type = RespType::BulkString;
                out.is_null = true;
                return ParseStatus::Ok;
            }
            if(pos + (size_t)bulk_len_val + 2 > m_len) {
                pos = bulk_start;
                return ParseStatus::NeedMore;
            }
            out.type = RespType::BulkString;
            out.is_null = false;
            out.str.assign(m_data + pos, (size_t)bulk_len_val);
            pos += (size_t)bulk_len_val;
            if(m_data[pos] != '\r' || m_data[pos + 1] != '\n') {
                pos = bulk_start;
                return ParseStatus::Error;
            }
            pos += 2;
            return ParseStatus::Ok;
        }
        case '*': {
            const size_t array_start = pos - 1;
            // Fast-path: parse array count directly from buffer (Phase 6)
            const char* num_start = m_data + pos;
            int64_t count = 0;
            const char* num_end = readInt64(num_start, m_data + m_len, count);
            if(!num_end) {
                return ParseStatus::Error;
            }
            if(num_end + 1 >= m_data + m_len || num_end[0] != '\r' || num_end[1] != '\n') {
                pos = array_start;
                return ParseStatus::NeedMore;
            }
            pos = (size_t)(num_end - m_data) + 2;
            if(count < 0) {
                pos = array_start;
                return ParseStatus::Error;
            }
            out.type = RespType::Array;
            out.array.clear();
            out.array.reserve((size_t)count);
            for(long long i = 0; i < count; ++i) {
                RespValue item;
                ParseStatus st = parseValue(pos, item);
                if(st != ParseStatus::Ok) {
                    pos = array_start;
                    out.array.clear();
                    return st;
                }
                out.array.push_back(std::move(item));
            }
            return ParseStatus::Ok;
        }
        default:
            return ParseStatus::Error;
    }
}

ParseStatus RespReader::tryParse(RespValue& out, size_t* consumed) {
    size_t pos = 0;
    ParseStatus st;
    if(m_len > 0 && !isRespPrefix(m_data[0])) {
        st = parseInline(pos, out);
    } else {
        st = parseValue(pos, out);
    }
    if(st == ParseStatus::Ok && consumed) {
        *consumed = pos;
    }
    return st;
}

} // namespace kv
} // namespace zero

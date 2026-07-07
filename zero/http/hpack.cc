/**
 * @file hpack.cc
 * @brief HPACK 头部压缩实现（简化版）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "hpack.h"
#include <cstdint>

namespace zero {
namespace http {
namespace http2 {

// HPACK 静态表（RFC 7541 附录 A）部分常用条目
static const std::pair<const char*, const char*> kStaticTable[] = {
    {"", ""},                         // 0 未使用
    {":authority", ""},
    {":method", "GET"},
    {":method", "POST"},
    {":path", "/"},
    {":path", "/index.html"},
    {":scheme", "http"},
    {":scheme", "https"},
    {":status", "200"},
    {":status", "204"},
    {":status", "206"},
    {":status", "304"},
    {":status", "400"},
    {":status", "404"},
    {":status", "500"},
    {"accept-charset", ""},
    {"accept-encoding", "gzip, deflate"},
    {"accept-language", ""},
    {"accept-ranges", ""},
    {"accept", ""},
    {"access-control-allow-origin", ""},
    {"age", ""},
    {"allow", ""},
    {"authorization", ""},
    {"cache-control", ""},
    {"content-disposition", ""},
    {"content-encoding", ""},
    {"content-language", ""},
    {"content-length", ""},
    {"content-location", ""},
    {"content-range", ""},
    {"content-type", ""},
    {"cookie", ""},
    {"date", ""},
    {"etag", ""},
    {"expect", ""},
    {"expires", ""},
    {"from", ""},
    {"host", ""},
    {"if-match", ""},
    {"if-modified-since", ""},
    {"if-none-match", ""},
    {"if-range", ""},
    {"if-unmodified-since", ""},
    {"last-modified", ""},
    {"link", ""},
    {"location", ""},
    {"max-forwards", ""},
    {"proxy-authenticate", ""},
    {"proxy-authorization", ""},
    {"range", ""},
    {"referer", ""},
    {"refresh", ""},
    {"retry-after", ""},
    {"server", ""},
    {"set-cookie", ""},
    {"strict-transport-security", ""},
    {"transfer-encoding", ""},
    {"user-agent", ""},
    {"vary", ""},
    {"via", ""},
    {"www-authenticate", ""}
};

static const size_t kStaticTableSize = sizeof(kStaticTable) / sizeof(kStaticTable[0]);

int HPack::lookupStaticTable(const std::string& name, const std::string& value) {
    for(size_t i = 1; i < kStaticTableSize; ++i) {
        if(name == kStaticTable[i].first && value == kStaticTable[i].second) {
            return (int)i;
        }
    }
    return 0;
}

std::string HPack::encodeInteger(uint32_t value, uint8_t prefixBits) {
    uint8_t maxPrefix = (1 << prefixBits) - 1;
    std::string out;
    if(value < maxPrefix) {
        out.push_back((char)value);
        return out;
    }
    out.push_back((char)maxPrefix);
    value -= maxPrefix;
    while(value >= 128) {
        out.push_back((char)((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back((char)value);
    return out;
}

uint32_t HPack::decodeInteger(const uint8_t* data, size_t len, size_t& offset, uint8_t prefixBits) {
    uint8_t maxPrefix = (1 << prefixBits) - 1;
    uint8_t b = data[offset++] & maxPrefix;
    if(b < maxPrefix) return b;
    uint32_t value = b;
    uint32_t m = 0;
    while(offset < len) {
        uint8_t byte = data[offset++];
        value += (byte & 0x7F) << m;
        if((byte & 0x80) == 0) break;
        m += 7;
    }
    return value;
}

std::string HPack::encodeStringLiteral(const std::string& str) {
    std::string out = encodeInteger(str.size(), 7);
    out[0] &= 0x7F; // 不压缩（Huffman）
    out += str;
    return out;
}

bool HPack::decodeStringLiteral(const uint8_t* data, size_t len, size_t& offset, std::string& out) {
    if(offset >= len) return false;
    bool huffman = (data[offset] & 0x80) != 0;
    uint32_t strLen = decodeInteger(data, len, offset, 7);
    if(offset + strLen > len) return false;
    if(huffman) {
        // 简化：不支持 Huffman 解码
        return false;
    }
    out.assign((const char*)data + offset, strLen);
    offset += strLen;
    return true;
}

std::vector<uint8_t> HPack::encode(const std::map<std::string, std::string>& headers) {
    std::vector<uint8_t> out;
    for(auto& kv : headers) {
        int idx = lookupStaticTable(kv.first, kv.second);
        if(idx > 0) {
            // 索引头部字段表示：1xxxxxxx
            std::string enc = encodeInteger(idx, 7);
            enc[0] |= 0x80;
            out.insert(out.end(), enc.begin(), enc.end());
        } else {
            // 字面量头部字段表示：01xxxxxx（never indexed 简化）
            out.push_back(0x40); // 字面量索引化
            auto nameEnc = encodeStringLiteral(kv.first);
            out.insert(out.end(), nameEnc.begin(), nameEnc.end());
            auto valueEnc = encodeStringLiteral(kv.second);
            out.insert(out.end(), valueEnc.begin(), valueEnc.end());
        }
    }
    return out;
}

bool HPack::decode(const uint8_t* data, size_t len,
                   std::map<std::string, std::string>& headers) {
    size_t offset = 0;
    while(offset < len) {
        uint8_t b = data[offset];
        if(b & 0x80) {
            // 索引头部字段
            size_t start = offset;
            uint32_t idx = decodeInteger(data, len, offset, 7);
            if(idx == 0 || idx >= kStaticTableSize) return false;
            headers[kStaticTable[idx].first] = kStaticTable[idx].second;
            (void)start;
        } else if((b & 0xC0) == 0x40) {
            // 字面量索引化
            offset++;
            std::string name, value;
            if(b & 0x3F) {
                uint32_t idx = decodeInteger(data, len, offset, 6);
                if(idx == 0 || idx >= kStaticTableSize) return false;
                name = kStaticTable[idx].first;
            } else {
                if(!decodeStringLiteral(data, len, offset, name)) return false;
            }
            if(!decodeStringLiteral(data, len, offset, value)) return false;
            headers[name] = value;
        } else if((b & 0xF0) == 0x00 || (b & 0xF0) == 0x10) {
            // 字面量 never indexed / without indexing
            offset++;
            std::string name, value;
            if(b & 0x0F) {
                uint32_t idx = decodeInteger(data, len, offset, 4);
                if(idx == 0 || idx >= kStaticTableSize) return false;
                name = kStaticTable[idx].first;
            } else {
                if(!decodeStringLiteral(data, len, offset, name)) return false;
            }
            if(!decodeStringLiteral(data, len, offset, value)) return false;
            headers[name] = value;
        } else {
            // 动态表更新等未实现
            return false;
        }
    }
    return true;
}

} // namespace http2
} // namespace http
} // namespace zero

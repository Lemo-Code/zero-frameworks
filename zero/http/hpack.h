/**
 * @file hpack.h
 * @brief HPACK 头部压缩（简化版）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_HPACK_H__
#define __ZERO_HTTP_HPACK_H__

#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace zero {
namespace http {
namespace http2 {

/**
 * @brief HPACK 编码器/解码器（简化版）
 * 
 * 当前仅支持索引表查找和字面量编码，动态表未实现。
 * 用于 HTTP/2 头部压缩的演示和基础集成。
 */
class HPack {
public:
    /**
     * @brief 编码头部
     */
    static std::vector<uint8_t> encode(const std::map<std::string, std::string>& headers);

    /**
     * @brief 解码头部
     */
    static bool decode(const uint8_t* data, size_t len,
                       std::map<std::string, std::string>& headers);

    /**
     * @brief 查找静态表索引
     * @return >0 找到索引；0 未找到
     */
    static int lookupStaticTable(const std::string& name, const std::string& value);

private:
    static std::string encodeInteger(uint32_t value, uint8_t prefixBits);
    static uint32_t decodeInteger(const uint8_t* data, size_t len, size_t& offset, uint8_t prefixBits);
    static std::string encodeStringLiteral(const std::string& str);
    static bool decodeStringLiteral(const uint8_t* data, size_t len, size_t& offset, std::string& out);
};

} // namespace http2
} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_HPACK_H__

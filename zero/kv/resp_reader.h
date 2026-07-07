/**
 * @file resp_reader.h
 * @brief 增量 RESP 解析（连接级 buffer 上调用，不分配额外 IO buffer）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_RESP_READER_H__
#define __ZERO_KV_RESP_READER_H__

#include "resp.h"

namespace zero {
namespace kv {

enum class ParseStatus {
    NeedMore,
    Ok,
    Error
};

class RespReader {
public:
    explicit RespReader(const char* data, size_t len);

    ParseStatus tryParse(RespValue& out, size_t* consumed);

private:
    ParseStatus parseValue(size_t& pos, RespValue& out);
    ParseStatus parseInline(size_t& pos, RespValue& out);
    bool readLine(size_t& pos, std::string& line) const;
    static bool isRespPrefix(char c);

    const char* m_data;
    size_t m_len;
};

} // namespace kv
} // namespace zero

#endif

/**
 * @file info.h
 * @brief Redis INFO 文本/JSON 生成（命令与 Admin HTTP 共用）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_INFO_H__
#define __ZERO_KV_INFO_H__

#include "store/kv_store.h"
#include <string>

namespace zero {
namespace kv {

std::string buildInfoText(int db, KvStore::ptr store);
std::string buildInfoJson(int db, KvStore::ptr store);

} // namespace kv
} // namespace zero

#endif

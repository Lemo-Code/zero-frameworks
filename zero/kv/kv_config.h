/**
 * @file kv_config.h
 * @brief 进程级 Redis 运行时配置（AUTH 等）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_KV_CONFIG_H__
#define __ZERO_KV_KV_CONFIG_H__

#include <string>

namespace zero {
namespace kv {

void setRequirePass(const std::string& pass);
const std::string& getRequirePass();
bool isAuthRequired();
bool isCommandAuthExempt(const std::string& upper_cmd);

} // namespace kv
} // namespace zero

#endif

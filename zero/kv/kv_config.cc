/**
 * @file kv_config.cc
 * @brief KV 配置实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_config.h"

#include <mutex>

namespace zero {
namespace kv {

namespace {

std::mutex g_mutex;
std::string g_require_pass;

} // namespace

void setRequirePass(const std::string& pass) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_require_pass = pass;
}

const std::string& getRequirePass() {
    static thread_local std::string cached;
    std::lock_guard<std::mutex> lock(g_mutex);
    cached = g_require_pass;
    return cached;
}

bool isAuthRequired() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_require_pass.empty();
}

bool isCommandAuthExempt(const std::string& upper_cmd) {
    return upper_cmd == "AUTH" || upper_cmd == "PING";
}

} // namespace kv
} // namespace zero

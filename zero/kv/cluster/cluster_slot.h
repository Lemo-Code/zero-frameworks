/**
 * @file cluster_slot.h
 * @brief Redis Cluster 哈希槽计算（CRC16 + hash tag）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_CLUSTER_CLUSTER_SLOT_H__
#define __ZERO_KV_CLUSTER_CLUSTER_SLOT_H__

#include <cstdint>
#include <string>

namespace zero {
namespace kv {

static const int kClusterSlotCount = 16384;

/**
 * @brief 计算 key 对应的哈希槽（0-16383）
 * @param key Redis key
 * @return slot 编号
 *
 * 支持 hash tag 语法：key{tag} 只取 tag 部分计算 CRC16
 */
int keyToSlot(const std::string& key);

/**
 * @brief 标准 CRC16 算法（Redis 使用 xmodem 多项式）
 */
uint16_t crc16(const std::string& data);

} // namespace kv
} // namespace zero

#endif

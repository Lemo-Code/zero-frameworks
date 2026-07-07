/**
 * @file cluster_migrate.h
 * @brief MIGRATE 命令 + 槽迁移辅助
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_CLUSTER_CLUSTER_MIGRATE_H__
#define __ZERO_KV_CLUSTER_CLUSTER_MIGRATE_H__

#include "zero/kv/resp.h"
#include "zero/kv/kv_context.h"
#include "zero/kv/store/kv_store.h"
#include <string>
#include <vector>

namespace zero {
namespace kv {

/**
 * @brief 执行 MIGRATE 命令
 * @return RESP 响应
 */
RespValue handleMigrate(KvContext& ctx, const RespValue& req, KvStore::ptr store);

} // namespace kv
} // namespace zero

#endif

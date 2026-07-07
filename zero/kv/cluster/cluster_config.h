/**
 * @file cluster_config.h
 * @brief Redis Cluster nodes.conf 持久化
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_CLUSTER_CLUSTER_CONFIG_H__
#define __ZERO_KV_CLUSTER_CLUSTER_CONFIG_H__

#include <memory>
#include <string>

namespace zero {
namespace kv {

class ClusterManager;

class ClusterConfig {
public:
    typedef std::shared_ptr<ClusterConfig> ptr;

    explicit ClusterConfig(const std::string& path);

    // 加载 nodes.conf 到 ClusterManager；返回是否成功
    bool load(ClusterManager* mgr) const;

    // 将 ClusterManager 当前拓扑写入 nodes.conf；返回是否成功
    bool save(const ClusterManager* mgr) const;

    std::string path() const { return m_path; }

private:
    std::string m_path;
};

} // namespace kv
} // namespace zero

#endif

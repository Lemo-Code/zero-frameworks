/**
 * @file kv_node_service.cc
 * @brief RPC 模块 - kv_node_service
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_node_service.h"

#include "zero/kv/kv_server.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/store/kv_store.h"
#include "zero/core/log/log.h"

#include <random>

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("rpc");

namespace {

std::string generateNodeId() {
    static const char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id(40, '\0');
    for (int i = 0; i < 40; ++i) id[i] = kHex[dist(gen)];
    return id;
}

const std::string& getOrCreateNodeId() {
    static std::string s_nodeId = generateNodeId();
    return s_nodeId;
}

} // namespace

std::function<void(const RpcEnvelope& request, RpcEnvelope& response)>
MakeKvNodeHandler(std::weak_ptr<kv::KvServer> server) {
    return [server](const RpcEnvelope& request, RpcEnvelope& response) {
        auto svr = server.lock();
        if (!svr) {
            ZERO_LOG_WARN(g_logger) << "KvNodeService: server expired";
            return;
        }

        switch (request.body_case()) {
        case RpcEnvelope::kHealthCheckReq: {
            auto* resp = response.mutable_health_check_resp();
            resp->set_ok(true);
            resp->set_node_id(getOrCreateNodeId());

            auto repl = svr->getReplication();
            if (repl) {
                resp->set_replication_id(repl->replId());
                resp->set_replication_offset(repl->replOffset());
                resp->set_connected_replicas(repl->connectedSlaves());
                resp->set_role(repl->role() == kv::ReplicationManager::Role::Master
                               ? NodeRole::MASTER : NodeRole::REPLICA);
            } else {
                resp->set_role(NodeRole::MASTER);
            }
            break;
        }

        case RpcEnvelope::kReplicaOfReq: {
            const auto& req = request.replica_of_req();
            auto* resp = response.mutable_replica_of_resp();

            auto repl = svr->getReplication();
            if (!repl) {
                resp->set_accepted(false);
                resp->set_error("no replication manager");
                break;
            }

            try {
                if (req.master_host().empty()) {
                    repl->promoteToMaster();
                    ZERO_LOG_INFO(g_logger) << "KvNodeService: promoted to master term="
                        << req.term();
                } else {
                    repl->setSlaveOf(req.master_host(), req.master_port());
                    ZERO_LOG_INFO(g_logger) << "KvNodeService: became replica of "
                        << req.master_host() << ":" << req.master_port()
                        << " term=" << req.term();
                }
                resp->set_accepted(true);
            } catch (const std::exception& e) {
                resp->set_accepted(false);
                resp->set_error(e.what());
            }
            break;
        }

        case RpcEnvelope::kGetNodeStatusReq: {
            auto* resp = response.mutable_get_node_status_resp();
            resp->set_node_id(getOrCreateNodeId());

            auto repl = svr->getReplication();
            if (repl) {
                resp->set_replication_id(repl->replId());
                resp->set_replication_offset(repl->replOffset());
                resp->set_connected_replicas(repl->connectedSlaves());
                resp->set_master_host(repl->masterHost());
                resp->set_master_port(repl->masterPort());
                resp->set_master_link_status(repl->masterLinkStatus());
                resp->set_role(repl->role() == kv::ReplicationManager::Role::Master
                               ? NodeRole::MASTER : NodeRole::REPLICA);
            } else {
                resp->set_role(NodeRole::MASTER);
            }
            break;
        }

        default:
            ZERO_LOG_WARN(g_logger) << "KvNodeService: unknown body_case "
                << request.body_case();
            break;
        }
    };
}

} // namespace rpc
} // namespace zero

/**
 * @file sentinel_service.cc
 * @brief RPC 模块 - sentinel_service
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "sentinel_service.h"
#include "sentinel_manager.h"

#include "zero/core/log/log.h"

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("rpc");

std::function<void(const RpcEnvelope& request, RpcEnvelope& response)>
MakeSentinelHandler(std::shared_ptr<SentinelManager> manager) {
    return [manager](const RpcEnvelope& request, RpcEnvelope& response) {
        if (!manager) {
            ZERO_LOG_WARN(g_logger) << "SentinelService: manager is null";
            return;
        }

        switch (request.body_case()) {
        case RpcEnvelope::kRequestVoteReq: {
            const auto& req = request.request_vote_req();
            auto* resp = response.mutable_request_vote_resp();
            bool granted = manager->handleRequestVote(req.term(), req.candidate_id());
            resp->set_term(manager->currentTerm());
            resp->set_vote_granted(granted);
            break;
        }

        case RpcEnvelope::kHeartbeatReq: {
            const auto& req = request.heartbeat_req();
            auto* resp = response.mutable_heartbeat_resp();
            bool success = manager->handleHeartbeat(req.term(), req.leader_id(),
                req.has_current_master() ? &req.current_master() : nullptr);
            resp->set_term(manager->currentTerm());
            resp->set_success(success);
            break;
        }

        case RpcEnvelope::kGetMasterReq: {
            auto* resp = response.mutable_get_master_resp();
            auto master = manager->currentMaster();
            resp->set_found(master.valid);
            if (master.valid) {
                resp->mutable_master()->set_host(master.host);
                resp->mutable_master()->set_port(master.port);
            }
            resp->set_term(manager->currentTerm());
            break;
        }

        case RpcEnvelope::kWatchMasterReq: {
            // v1: WatchMaster server-streaming not supported.
            // Clients should poll GetMaster instead.
            auto* resp = response.mutable_get_master_resp();
            auto master = manager->currentMaster();
            resp->set_found(master.valid);
            if (master.valid) {
                resp->mutable_master()->set_host(master.host);
                resp->mutable_master()->set_port(master.port);
            }
            resp->set_term(manager->currentTerm());
            break;
        }

        default:
            ZERO_LOG_WARN(g_logger) << "SentinelService: unknown body_case "
                << request.body_case();
            break;
        }
    };
}

} // namespace rpc
} // namespace zero

/**
 * @file sentinel_server.cc
 * @brief KV node with sentinel mode — runs both RESP server and RPC/sentinel server
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/kv_server.h"
#include "zero/rpc/kv_node_service.h"
#include "zero/rpc/rpc_server.h"
#include "zero/rpc/sentinel_manager.h"
#include "zero/rpc/sentinel_service.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();
static const int kWorkers = 4;

// RESP port
static int g_port = 6379;

// RPC / sentinel flags
static bool g_sentinel = false;
static int g_rpcPort = 0;
static std::string g_sentinelId;
static std::string g_peers;      // comma-separated "host:port,host:port"
static std::string g_monitor;    // comma-separated "host:port,host:port"

static std::string g_rdb = "./dump.rdb";
static std::string g_aof = "./appendonly.aof";
static int g_autoSaveSec = 60;
static bool g_aofEnabled = false;

// Parse comma-separated "host:port,..." into vector of pairs
static std::vector<std::pair<std::string, int>> parseNodeList(const std::string& input) {
    std::vector<std::pair<std::string, int>> result;
    if (input.empty()) return result;

    std::istringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t colon = token.find(':');
        if (colon == std::string::npos) continue;
        std::string host = token.substr(0, colon);
        int port = std::stoi(token.substr(colon + 1));
        result.push_back({host, port});
    }
    return result;
}

void run() {
    g_logger->setLevel(zero::LogLevel::INFO);

    // ---- 1. Create KV store and server (RESP) ----
    zero::kv::KvStore::ptr store(new zero::kv::KvStore);
    store->setRdbPath(g_rdb);

    zero::kv::KvServer::ptr kvServer(new zero::kv::KvServer(store));
    kvServer->setAutoSaveSec(g_autoSaveSec);
    if (kvServer->getAof()) {
        kvServer->getAof()->setPath(g_aof);
        kvServer->getAof()->setEnabled(g_aofEnabled);
    }

    zero::Address::ptr respAddr =
        zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_port));
    while (!kvServer->bind(respAddr)) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    kvServer->start();
    ZERO_LOG_INFO(g_logger) << "RESP server listening on 0.0.0.0:" << g_port;

    // ---- 2. Start RPC server (KvNodeService) ----
    zero::rpc::RpcServer::ptr rpcServer;
    zero::rpc::SentinelManager::ptr sentinel;

    if (g_rpcPort > 0) {
        // Create KvNodeService handler
        auto kvHandler = zero::rpc::MakeKvNodeHandler(
            std::weak_ptr<zero::kv::KvServer>(kvServer));

        rpcServer.reset(new zero::rpc::RpcServer(kvHandler));

        zero::Address::ptr rpcAddr =
            zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_rpcPort));
        while (!rpcServer->bind(rpcAddr)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        rpcServer->start();
        ZERO_LOG_INFO(g_logger) << "RPC (KvNodeService) listening on 0.0.0.0:" << g_rpcPort;

        // ---- 3. Start Sentinel (if enabled) ----
        if (g_sentinel) {
            sentinel.reset(new zero::rpc::SentinelManager);

            if (g_sentinelId.empty()) {
                g_sentinelId = "sentinel-" + std::to_string(g_rpcPort);
            }
            sentinel->setNodeId(g_sentinelId);
            sentinel->setBindAddr("127.0.0.1", g_rpcPort);

            // Set sentinel peers
            auto peers = parseNodeList(g_peers);
            sentinel->setSentinelPeers(peers);

            // Set monitored KV nodes
            auto nodes = parseNodeList(g_monitor);
            // Also add ourselves if we're a KV node
            nodes.push_back({"127.0.0.1", g_port});
            sentinel->setMonitoredNodes(nodes);

            // Set failover callback — bridges gRPC thread to fiber world
            sentinel->setFailoverCallback(
                [kvServer](bool promoteToMaster,
                           const std::string& newMasterHost, int newMasterPort) {
                    auto repl = kvServer->getReplication();
                    if (!repl) return;

                    if (promoteToMaster) {
                        ZERO_LOG_INFO(g_logger) << "Failover: promoting self to master";
                        repl->promoteToMaster();
                    } else if (!newMasterHost.empty()) {
                        ZERO_LOG_INFO(g_logger) << "Failover: becoming replica of "
                            << newMasterHost << ":" << newMasterPort;
                        repl->setSlaveOf(newMasterHost, newMasterPort);
                    }
                });

            sentinel->start();
            ZERO_LOG_INFO(g_logger) << "Sentinel started, nodeId=" << g_sentinelId
                << " peers=" << peers.size() << " monitored=" << nodes.size();
        }
    }

    ZERO_LOG_INFO(g_logger) << "Server ready: RESP=" << g_port
        << " RPC=" << (g_rpcPort > 0 ? std::to_string(g_rpcPort) : "off")
        << " Sentinel=" << (g_sentinel ? "on" : "off");

    // Keep main thread alive while IOManager runs
    // The IOManager threads are already running from the scheduler
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-rdb") && i + 1 < argc) {
            g_rdb = argv[++i];
        } else if (!strcmp(argv[i], "-aof") && i + 1 < argc) {
            g_aof = argv[++i];
        } else if (!strcmp(argv[i], "--aof")) {
            g_aofEnabled = true;
        } else if (!strcmp(argv[i], "--save") && i + 1 < argc) {
            g_autoSaveSec = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--rpc") && i + 1 < argc) {
            g_rpcPort = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sentinel")) {
            g_sentinel = true;
        } else if (!strcmp(argv[i], "--sentinel-id") && i + 1 < argc) {
            g_sentinelId = argv[++i];
        } else if (!strcmp(argv[i], "--peers") && i + 1 < argc) {
            g_peers = argv[++i];
        } else if (!strcmp(argv[i], "--monitor") && i + 1 < argc) {
            g_monitor = argv[++i];
        }
    }

    if (g_rpcPort <= 0 && g_sentinel) {
        std::cerr << "Error: --rpc <port> is required when --sentinel is enabled" << std::endl;
        return 1;
    }

    zero::set_hook_enable(true);
    zero::IOManager iom(kWorkers, true, "sentinel");
    iom.schedule(run);
    return 0;
}

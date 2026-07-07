/**
 * @file kv_server.cc
 * @brief KV 服务器启动示例
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/kv/kv_server.h"
#include "zero/kv/admin/kv_admin_servlet.h"
#include "zero/http/http_server.h"
#include "zero/rpc/kv_node_service.h"
#include "zero/rpc/rpc_server.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"
#include <cstring>
#include <string>

static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();
static int kWorkers = 4;
static int g_port = 6379;
static int g_httpPort = 0;
static int g_rpcPort = 0;
static std::string g_rdb = "./dump.rdb";
static std::string g_aof = "./appendonly.aof";
static int g_autoSaveSec = 60;
static bool g_aofEnabled = false;

void run() {
    g_logger->setLevel(zero::LogLevel::ERROR);
    zero::kv::KvStore::ptr store(new zero::kv::KvStore);
    store->setRdbPath(g_rdb);

    zero::kv::KvServer::ptr server(new zero::kv::KvServer(store));
    server->setAutoSaveSec(g_autoSaveSec);
    if(server->getAof()) {
        server->getAof()->setPath(g_aof);
        server->getAof()->setEnabled(g_aofEnabled);
    }

    zero::Address::ptr addr = zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_port));
    while(!server->bind(addr)) {
        sleep(2);
    }
    server->start();
    ZERO_LOG_INFO(g_logger) << "redis_server listen 0.0.0.0:" << g_port
        << " rdb=" << g_rdb << " aof=" << (g_aofEnabled ? g_aof : "off")
        << " autosave=" << g_autoSaveSec << "s";

    if(g_httpPort > 0) {
        zero::http::HttpServer::ptr admin(new zero::http::HttpServer(true));
        admin->setName("zero-redis-admin");
        zero::kv::registerKvAdminServlets(admin, store, server->getReplication());
        zero::Address::ptr http_addr =
            zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_httpPort));
        while(!admin->bind(http_addr)) {
            sleep(2);
        }
        admin->start();
        ZERO_LOG_INFO(g_logger) << "redis admin http listen 0.0.0.0:" << g_httpPort
            << " (/redis/info /redis/ping /redis/save /redis/role /redis/stats)";
    }

    // RPC port for sentinel health checks / SLAVEOF
    zero::rpc::RpcServer::ptr rpcSrv;
    if(g_rpcPort > 0) {
        auto handler = zero::rpc::MakeKvNodeHandler(
            std::weak_ptr<zero::kv::KvServer>(server));
        rpcSrv.reset(new zero::rpc::RpcServer(handler));
        zero::Address::ptr rpc_addr =
            zero::Address::LookupAny("0.0.0.0:" + std::to_string(g_rpcPort));
        while(!rpcSrv->bind(rpc_addr)) {
            sleep(2);
        }
        rpcSrv->start();
        ZERO_LOG_INFO(g_logger) << "redis rpc listen 0.0.0.0:" << g_rpcPort;
    }
}

int main(int argc, char** argv) {
    for(int i = 1; i < argc; ++i) {
        if(!strcmp(argv[i], "-p") && i + 1 < argc) {
            g_port = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "-http") && i + 1 < argc) {
            g_httpPort = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "-rdb") && i + 1 < argc) {
            g_rdb = argv[++i];
        } else if(!strcmp(argv[i], "-aof") && i + 1 < argc) {
            g_aof = argv[++i];
        } else if(!strcmp(argv[i], "--aof")) {
            g_aofEnabled = true;
        } else if(!strcmp(argv[i], "-rpc") && i + 1 < argc) {
            g_rpcPort = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "--save") && i + 1 < argc) {
            g_autoSaveSec = atoi(argv[++i]);
        } else if(!strcmp(argv[i], "-w") && i + 1 < argc) {
            kWorkers = atoi(argv[++i]);
        }
    }
    zero::set_hook_enable(true);
    zero::IOManager iom(kWorkers, true, "redis");
    iom.schedule(run);
    return 0;
}

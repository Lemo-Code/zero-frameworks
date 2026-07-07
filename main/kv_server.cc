/**
 * @file kv_server.cc
 * @brief KV (Redis 兼容) 服务器最终版 - Spring Boot 风格启动
 * @details 完整的生产级 Redis 兼容 KV 服务器启动入口:
 *          1. YAML 配置文件加载
 *          2. 命令行参数覆盖
 *          3. 环境变量覆盖
 *          4. 支持 RDB/AOF 持久化
 *          5. 支持 HTTP 管理接口
 *          6. 支持 RPC (Sentinel 健康检查 / SLAVEOF)
 *          7. 优雅关闭 (SIGINT/SIGTERM, 自动 SAVE)
 *          8. 启动横幅和运行信息
 *
 * 用法:
 *          ./kv_server
 *          ./kv_server -c /path/to/conf
 *          ./kv_server --app.workers=6 --servers.kv.address=0.0.0.0:6380
 *          ./kv_server --servers.kv.aof_enabled=1 --servers.kv.http_admin_port=16379
 *
 * @author zero-framework
 * @date 2026-07-07
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/config/config.h"
#include "zero/core/concurrency/signal.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/io/address.h"
#include "zero/core/log/log.h"
#include "zero/core/log/log_config.h"
#include "zero/core/util/env.h"

#include "zero/kv/kv_server.h"
#include "zero/kv/kv_config.h"
#include "zero/kv/admin/kv_admin_servlet.h"

#include "zero/http/http_server.h"
#include "zero/rpc/rpc_server.h"
#include "zero/rpc/kv_node_service.h"

#include <cstring>
#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>

using namespace zero;

static Logger::ptr g_logger = ZERO_LOG_NAME("system");
static std::atomic<bool> g_running{true};

// 各组件实例
static kv::KvServer::ptr g_kvServer;
static http::HttpServer::ptr g_adminServer;
static rpc::RpcServer::ptr g_rpcServer;

// ============================================================================
// 注册配置项
// ============================================================================
static void registerConfigVars() {
    // 应用配置
    Config::Lookup<std::string>("app.name", "zero-kv-server", "应用名称");
    Config::Lookup<std::string>("app.version", "1.0.0", "应用版本");
    Config::Lookup<int>("app.workers", 6, "工作线程数");
    Config::Lookup<int>("app.daemon", 0, "守护进程模式");
    Config::Lookup<int>("app.hook_enable", 1, "启用 fiber I/O hook");
    Config::Lookup<std::string>("app.pid_file", "", "PID 文件路径");
    Config::Lookup<std::string>("app.config_path", "conf", "配置文件目录");

    // KV 服务器配置
    Config::Lookup<int>("servers.kv.enabled", 1, "启用 KV 服务器");
    Config::Lookup<std::string>("servers.kv.address", "0.0.0.0:6379", "监听地址");
    Config::Lookup<int>("servers.kv.timeout", 120000, "超时时间(ms)");
    Config::Lookup<std::string>("servers.kv.name", "kv-server", "服务器名称");
    Config::Lookup<std::string>("servers.kv.rdb_path", "./dump.rdb", "RDB 路径");
    Config::Lookup<std::string>("servers.kv.aof_path", "./appendonly.aof", "AOF 路径");
    Config::Lookup<int>("servers.kv.aof_enabled", 0, "启用 AOF");
    Config::Lookup<int>("servers.kv.autosave_sec", 60, "自动保存间隔(秒)");
    Config::Lookup<int>("servers.kv.http_admin_port", 0, "HTTP 管理端口 (0=禁用)");
    Config::Lookup<int>("servers.kv.rpc_port", 0, "RPC 端口 (0=禁用)");

    // 优雅关闭
    Config::Lookup<int>("shutdown.timeout", 30000, "优雅关闭超时(ms)");

    // 日志
    Config::Lookup<std::string>("logs.level", "INFO", "日志级别");

    // 配置热重载
    Config::Lookup<int>("config_reload.enabled", 1, "启用配置热重载");
}

// ============================================================================
// 配置读取辅助
// ============================================================================
static std::string cfgStr(const std::string& key, const std::string& def = "") {
    auto v = Config::Lookup<std::string>(key);
    return v ? v->getValue() : def;
}
static int cfgInt(const std::string& key, int def = 0) {
    auto v = Config::Lookup<int>(key);
    return v ? v->getValue() : def;
}

// ============================================================================
// Banner
// ============================================================================
static void printBanner() {
    struct utsname sysinfo;
    uname(&sysinfo);

    std::cout << "\n"
              << "  ┌──────────────────────────────────────────────────────────────┐\n"
              << "  │                                                              │\n"
              << "  │   ██╗  ██╗██╗   ██╗                                          │\n"
              << "  │   ██║ ██╔╝██║   ██║                                          │\n"
              << "  │   █████╔╝ ██║   ██║                                          │\n"
              << "  │   ██╔═██╗ ╚██╗ ██╔╝                                          │\n"
              << "  │   ██║  ██╗ ╚████╔╝                                           │\n"
              << "  │   ╚═╝  ╚═╝  ╚═══╝                                            │\n"
              << "  │                                                              │\n"
              << "  │   Zero KV Server  ::  Redis-Compatible Storage Engine        │\n"
              << "  └──────────────────────────────────────────────────────────────┘\n"
              << "\n"
              << "  Application:  " << cfgStr("app.name") << " v" << cfgStr("app.version") << "\n"
              << "  System:       " << sysinfo.sysname << " " << sysinfo.release
              << " (" << sysinfo.machine << ")\n"
              << "  PID:          " << getpid() << "\n"
              << "  Working Dir:  " << EnvMgr::GetInstance()->getCwd() << "\n\n";
}

// ============================================================================
// 启动所有服务
// ============================================================================
static void startAll() {
    std::string addrStr = cfgStr("servers.kv.address", "0.0.0.0:6379");
    std::string rdbPath = cfgStr("servers.kv.rdb_path", "./dump.rdb");
    std::string aofPath = cfgStr("servers.kv.aof_path", "./appendonly.aof");
    int aofEnabled = cfgInt("servers.kv.aof_enabled", 0);
    int autoSaveSec = cfgInt("servers.kv.autosave_sec", 60);
    int httpAdminPort = cfgInt("servers.kv.http_admin_port", 0);
    int rpcPort = cfgInt("servers.kv.rpc_port", 0);
    int timeout = cfgInt("servers.kv.timeout", 120000);
    std::string name = cfgStr("servers.kv.name", "kv-server");

    // ---- 1. 创建 KV Store ----
    auto store = std::make_shared<kv::KvStore>();
    store->setRdbPath(rdbPath);

    // ---- 2. 创建 KV Server ----
    g_kvServer.reset(new kv::KvServer(store));
    g_kvServer->setAutoSaveSec(autoSaveSec);
    g_kvServer->setRecvTimeout(timeout);

    if (g_kvServer->getAof()) {
        g_kvServer->getAof()->setPath(aofPath);
        g_kvServer->getAof()->setEnabled(aofEnabled != 0);
    }

    // ---- 3. 绑定并启动 KV Server ----
    auto addr = Address::LookupAny(addrStr);
    while (!g_kvServer->bind(addr)) {
        ZERO_LOG_ERROR(g_logger) << "KV bind failed: " << addrStr << ", retrying...";
        sleep(2);
    }
    g_kvServer->start();

    ZERO_LOG_INFO(g_logger) << "KV server listening on " << addrStr;

    // ---- 4. HTTP 管理接口 (可选) ----
    if (httpAdminPort > 0) {
        g_adminServer.reset(new http::HttpServer(true));
        g_adminServer->setName("kv-admin");
        kv::registerKvAdminServlets(g_adminServer, store, g_kvServer->getReplication());

        auto httpAddr = Address::LookupAny("0.0.0.0:" + std::to_string(httpAdminPort));
        while (!g_adminServer->bind(httpAddr)) {
            ZERO_LOG_ERROR(g_logger) << "KV admin HTTP bind failed, retrying...";
            sleep(2);
        }
        g_adminServer->start();
        ZERO_LOG_INFO(g_logger) << "KV admin HTTP listening on " << httpAdminPort;
    }

    // ---- 5. RPC 端口 (可选, Sentinel / SLAVEOF) ----
    if (rpcPort > 0) {
        auto handler = rpc::MakeKvNodeHandler(
            std::weak_ptr<kv::KvServer>(g_kvServer));
        g_rpcServer.reset(new rpc::RpcServer(handler));
        g_rpcServer->setName("kv-rpc");

        auto rpcAddr = Address::LookupAny("0.0.0.0:" + std::to_string(rpcPort));
        while (!g_rpcServer->bind(rpcAddr)) {
            ZERO_LOG_ERROR(g_logger) << "KV RPC bind failed, retrying...";
            sleep(2);
        }
        g_rpcServer->start();
        ZERO_LOG_INFO(g_logger) << "KV RPC listening on " << rpcPort;
    }

    // ---- 6. 注册优雅关闭 ----
    SignalManager::GetInstance()->registerServer("kv-server", []() {
        if (g_kvServer) g_kvServer->stop();
    });

    // ---- 7. 打印启动信息 ----
    std::cout << "\n"
              << "  ┌──────────────────────────────────────────────────────────────┐\n"
              << "  │  KV Server Ready                                             │\n"
              << "  ├──────────────────────────────────────────────────────────────┤\n"
              << "  │  Listen:    " << std::left << std::setw(50) << addrStr << "│\n"
              << "  │  Name:      " << std::left << std::setw(50) << name << "│\n"
              << "  │  Workers:   " << std::left << std::setw(50) << cfgInt("app.workers") << "│\n"
              << "  │  RDB:       " << std::left << std::setw(50) << rdbPath << "│\n"
              << "  │  AOF:       " << std::left << std::setw(50)
              << (aofEnabled ? aofPath : "OFF") << "│\n"
              << "  │  AutoSave:  " << std::left << std::setw(50)
              << (autoSaveSec > 0 ? std::to_string(autoSaveSec) + "s" : "OFF") << "│\n";

    if (httpAdminPort > 0) {
        std::cout << "  │  Admin:     " << std::left << std::setw(50)
                  << ("HTTP 0.0.0.0:" + std::to_string(httpAdminPort)) << "│\n";
    }
    if (rpcPort > 0) {
        std::cout << "  │  RPC:       " << std::left << std::setw(50)
                  << ("0.0.0.0:" + std::to_string(rpcPort)) << "│\n";
    }

    std::cout << "  └──────────────────────────────────────────────────────────────┘\n\n";

    if (httpAdminPort > 0) {
        std::cout << "  Admin Endpoints:\n"
                  << "    GET  /redis/info   - 服务器信息\n"
                  << "    GET  /redis/ping   - 连通性检查\n"
                  << "    POST /redis/save   - 触发 SAVE\n"
                  << "    GET  /redis/role   - 主从角色\n"
                  << "    GET  /redis/stats  - 运行统计\n\n";
    }

    std::cout << "  Press Ctrl+C to stop.\n" << std::endl;
}

// ============================================================================
// 优雅关闭
// ============================================================================
static void doShutdown() {
    if (!g_running.exchange(false)) return;

    std::cout << "\n  Shutting down KV server...\n";

    int shutdownTimeout = cfgInt("shutdown.timeout", 30000);

    // 先停止 RPC
    if (g_rpcServer) {
        std::cout << "    Stopping RPC...\n";
        g_rpcServer->stop();
    }

    // 停止 HTTP 管理接口
    if (g_adminServer) {
        std::cout << "    Stopping admin HTTP...\n";
        g_adminServer->stop();
    }

    // 停止 KV Server (会自动触发 SAVE)
    if (g_kvServer) {
        std::cout << "    Stopping KV server (SAVE triggered)...\n";
        g_kvServer->stop();
    }

    std::cout << "  KV server stopped. Goodbye!\n\n";
}

// ============================================================================
// 守护进程
// ============================================================================
static bool daemonize() {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid > 0) _exit(0);
    setsid();
    umask(0);
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    return true;
}

static void writePidFile(const std::string& path) {
    if (path.empty()) return;
    std::ofstream ofs(path);
    if (ofs.is_open()) ofs << getpid() << "\n";
}

// ============================================================================
// 使用帮助
// ============================================================================
static void usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -c <dir>             配置目录 (默认: conf)\n"
              << "  -p <profile>         激活配置 Profile\n"
              << "  -d                   守护进程模式\n"
              << "  --<key>=<value>      覆盖配置项\n"
              << "                       例如: --app.workers=6\n"
              << "                       例如: --servers.kv.address=0.0.0.0:6380\n"
              << "                       例如: --servers.kv.http_admin_port=16379\n"
              << "                       例如: --servers.kv.aof_enabled=1\n"
              << "  --help               显示帮助\n\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " -c /etc/zero/conf\n"
              << "  " << prog << " --app.workers=8 --servers.kv.aof_enabled=1\n"
              << "  " << prog << " --servers.kv.http_admin_port=16379 -d\n"
              << std::endl;
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
    // 1. 初始化环境
    EnvMgr::GetInstance()->init(argc, argv);

    std::string configPath = "conf";
    std::string profile;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            profile = argv[++i];
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]); return 0;
        }
    }

    // 2. 注册配置项 → 加载 YAML → 环境变量 → 命令行覆盖
    registerConfigVars();
    Config::LoadFromConfDir(configPath);
    Config::LoadFromEnv();
    Config::LoadFromCmdArgs(argc, argv);

    if (!profile.empty()) {
        Config::ActivateProfile(profile);
    }

    // 3. 守护进程
    if (cfgInt("app.daemon", 0) || EnvMgr::GetInstance()->has("d")) {
        if (!daemonize()) {
            std::cerr << "Daemonize failed!\n";
            return 2;
        }
    }

    writePidFile(cfgStr("app.pid_file", ""));

    // 4. 打印横幅
    printBanner();

    // 5. 启动配置热重载
    if (cfgInt("config_reload.enabled", 1)) {
        Config::StartWatch(configPath);
    }

    // 6. 日志级别
    std::string logLevel = cfgStr("logs.level", "INFO");
    g_logger->setLevel(LogLevel::FromString(logLevel));

    // 7. 信号处理
    int workers = cfgInt("app.workers", 6);
    int shutdownTimeout = cfgInt("shutdown.timeout", 30000);
    int hookEnabled = cfgInt("app.hook_enable", 1);

    if (hookEnabled) {
        set_hook_enable(true);
    }

    SignalManager::GetInstance()->setShutdownTimeout(shutdownTimeout);
    SignalManager::GetInstance()->registerHandler(SIGINT, doShutdown);
    SignalManager::GetInstance()->registerHandler(SIGTERM, doShutdown);
    SignalManager::GetInstance()->start();

    // 8. 启动 IOManager 和 KV 服务器
    std::cout << "  Configuration:\n"
              << "    Workers:      " << workers << "\n"
              << "    Config Dir:   " << configPath << "\n"
              << "    Hot Reload:   " << (cfgInt("config_reload.enabled", 1) ? "ON" : "OFF") << "\n"
              << "    Shutdown TO:  " << shutdownTimeout << "ms\n"
              << "    Fiber Hook:   " << (hookEnabled ? "ON" : "OFF") << "\n\n";

    IOManager iom(workers, true, "kv");
    iom.schedule(startAll);

    // 9. 等待关闭信号
    while (g_running.load()) {
        sleep(1);
    }

    // 10. 清理
    Config::StopWatch();
    SignalManager::GetInstance()->stop();

    return 0;
}

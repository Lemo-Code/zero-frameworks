/**
 * @file zero_app.cc
 * @brief Zero Framework 主应用入口 - Spring Boot 风格启动
 * @details 功能:
 *          1. 加载 YAML 配置文件 (conf/application.yml)
 *          2. 命令行参数覆盖 (--key=value)
 *          3. 环境变量覆盖 (ZERO_ 前缀)
 *          4. 优雅关闭 (SIGINT/SIGTERM)
 *          5. 支持 HTTP/KV/RPC/WS 多服务器
 *          6. 启动 Banner 和摘要信息
 *          7. 配置热重载 (inotify)
 *
 * 用法:
 *          ./zero_app                                           # 默认配置
 *          ./zero_app -c /path/to/conf                          # 指定配置目录
 *          ./zero_app --app.workers=8 --servers.http.port=9090  # 命令行覆盖
 *          ./zero_app -p dev                                    # 激活 profile
 *
 * @author zero-framework
 * @date 2026-07-07
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/config/config.h"
#include "zero/core/concurrency/signal.h"
#include "zero/core/io/hook.h"
#include "zero/core/io/iomanager.h"
#include "zero/core/log/log.h"
#include "zero/core/log/log_config.h"
#include "zero/core/util/env.h"

#include "zero/http/http_server.h"
#include "zero/http/servlet.h"
#include "zero/http/servlets/config_servlet.h"
#include "zero/http/servlets/status_servlet.h"
#include "zero/http/servlets/health_servlet.h"

#include "zero/kv/kv_server.h"
#include "zero/kv/kv_config.h"
#include "zero/kv/admin/kv_admin_servlet.h"

#include "zero/rpc/rpc_server.h"
#include "zero/rpc/rpc_config.h"
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

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// ============================================================================
// 全局状态
// ============================================================================
static std::atomic<bool> g_running{true};
static IOManager* g_iom = nullptr;

// 各服务器实例，用于优雅关闭
static http::HttpServer::ptr g_httpServer;
static kv::KvServer::ptr g_kvServer;
static rpc::RpcServer::ptr g_kvRpcServer;   // KV 的 RPC (Sentinel)
static rpc::RpcServer::ptr g_rpcServer;      // 独立的 RPC 服务器

// ============================================================================
// 配置项注册 - 在 LoadYaml 之前注册所有配置项及其默认值
// ============================================================================
static void registerConfigVars() {
    // ---- app 全局配置 ----
    Config::Lookup<std::string>("app.name", "zero-application", "应用名称");
    Config::Lookup<std::string>("app.version", "1.0.0", "应用版本");
    Config::Lookup<int>("app.workers", 6, "IOManager 工作线程数");
    Config::Lookup<int>("app.daemon", 0, "守护进程模式");
    Config::Lookup<int>("app.hook_enable", 1, "启用 fiber I/O hook");
    Config::Lookup<std::string>("app.pid_file", "", "PID 文件路径");
    Config::Lookup<std::string>("app.config_path", "conf", "配置文件目录");

    // ---- servers.http ----
    Config::Lookup<int>("servers.http.enabled", 1, "启用 HTTP 服务器");
    Config::Lookup<std::string>("servers.http.address", "0.0.0.0:8080", "HTTP 监听地址");
    Config::Lookup<int>("servers.http.keepalive", 1, "HTTP Keep-Alive");
    Config::Lookup<int>("servers.http.timeout", 120000, "HTTP 超时(ms)");
    Config::Lookup<std::string>("servers.http.name", "http-server", "HTTP 服务器名称");
    Config::Lookup<int>("servers.http.ssl", 0, "启用 SSL");
    Config::Lookup<std::string>("servers.http.cert_file", "", "SSL 证书路径");
    Config::Lookup<std::string>("servers.http.key_file", "", "SSL 私钥路径");

    // ---- servers.kv ----
    Config::Lookup<int>("servers.kv.enabled", 0, "启用 KV 服务器");
    Config::Lookup<std::string>("servers.kv.address", "0.0.0.0:6379", "KV 监听地址");
    Config::Lookup<int>("servers.kv.timeout", 120000, "KV 超时(ms)");
    Config::Lookup<std::string>("servers.kv.name", "kv-server", "KV 服务器名称");
    Config::Lookup<std::string>("servers.kv.rdb_path", "./dump.rdb", "RDB 持久化路径");
    Config::Lookup<std::string>("servers.kv.aof_path", "./appendonly.aof", "AOF 持久化路径");
    Config::Lookup<int>("servers.kv.aof_enabled", 0, "启用 AOF");
    Config::Lookup<int>("servers.kv.autosave_sec", 60, "自动保存间隔(秒)");
    Config::Lookup<int>("servers.kv.http_admin_port", 0, "KV HTTP 管理端口");
    Config::Lookup<int>("servers.kv.rpc_port", 0, "KV RPC 端口");

    // ---- servers.rpc ----
    Config::Lookup<int>("servers.rpc.enabled", 0, "启用 RPC 服务器");
    Config::Lookup<std::string>("servers.rpc.address", "0.0.0.0:9090", "RPC 监听地址");
    Config::Lookup<int>("servers.rpc.timeout", 120000, "RPC 超时(ms)");
    Config::Lookup<std::string>("servers.rpc.name", "rpc-server", "RPC 服务器名称");

    // ---- servers.ws ----
    Config::Lookup<int>("servers.ws.enabled", 0, "启用 WebSocket 服务器");
    Config::Lookup<std::string>("servers.ws.address", "0.0.0.0:8081", "WS 监听地址");
    Config::Lookup<int>("servers.ws.timeout", 120000, "WS 超时(ms)");
    Config::Lookup<std::string>("servers.ws.name", "ws-server", "WS 服务器名称");

    // ---- shutdown ----
    Config::Lookup<int>("shutdown.timeout", 30000, "优雅关闭超时(ms)");

    // ---- logs ----
    Config::Lookup<std::string>("logs.level", "INFO", "日志级别");

    // ---- performance ----
    Config::Lookup<int>("performance.cpu_affinity", 0, "CPU 亲和性绑定");
    Config::Lookup<int>("performance.connection_affinity", 1, "连接亲和性");

    // ---- config_reload ----
    Config::Lookup<int>("config_reload.enabled", 1, "启用配置热重载");
    Config::Lookup<int>("config_reload.debounce_ms", 300, "热重载防抖(ms)");
}

// ============================================================================
// 工具函数: 从配置获取值
// ============================================================================
static std::string cfgStr(const std::string& key, const std::string& def = "") {
    auto var = Config::Lookup<std::string>(key);
    return var ? var->getValue() : def;
}

static int cfgInt(const std::string& key, int def = 0) {
    auto var = Config::Lookup<int>(key);
    return var ? var->getValue() : def;
}

// ============================================================================
// 启动 Banner
// ============================================================================
static void printBanner() {
    struct utsname sysinfo;
    uname(&sysinfo);

    std::string appName = cfgStr("app.name", "zero-application");
    std::string appVer  = cfgStr("app.version", "1.0.0");

    std::cout << "\n"
              << "  ╔══════════════════════════════════════════════════════════════╗\n"
              << "  ║                                                              ║\n"
              << "  ║   ███████╗ ███████╗ ██████╗  ██████╗                         ║\n"
              << "  ║   ╚══███╔╝ ██╔════╝ ██╔══██╗ ██╔═══██╗                       ║\n"
              << "  ║     ███╔╝  █████╗   ██████╔╝ ██║   ██║                       ║\n"
              << "  ║    ███╔╝   ██╔══╝   ██╔══██╗ ██║   ██║                       ║\n"
              << "  ║   ███████╗ ███████╗ ██║  ██║ ╚██████╔╝                       ║\n"
              << "  ║   ╚══════╝ ╚══════╝ ╚═╝  ╚═╝  ╚═════╝                        ║\n"
              << "  ║                                                              ║\n"
              << "  ║   Zero Framework  ::  High-Performance C++ Server Engine      ║\n"
              << "  ╚══════════════════════════════════════════════════════════════╝\n"
              << "\n"
              << "  Application:  " << appName << " v" << appVer << "\n"
              << "  System:       " << sysinfo.sysname << " " << sysinfo.release
              << " (" << sysinfo.machine << ")\n"
              << "  PID:          " << getpid() << "\n"
              << "  Working Dir:  " << EnvMgr::GetInstance()->getCwd() << "\n\n";
}

// ============================================================================
// 打印启动摘要
// ============================================================================
static void printStartupSummary() {
    int workers = cfgInt("app.workers", 6);
    int hookEnabled = cfgInt("app.hook_enable", 1);

    std::cout << "  ────────────────────────────────────────────────────────────────\n";
    std::cout << "  Configuration Summary:\n";
    std::cout << "    Workers:      " << workers << " threads (fiber hook: "
              << (hookEnabled ? "ON" : "OFF") << ")\n";
    std::cout << "    Config Dir:   " << EnvMgr::GetInstance()->getConfigPath() << "\n";
    std::cout << "    Hot Reload:   " << (cfgInt("config_reload.enabled", 1) ? "ON" : "OFF") << "\n";
    std::cout << "    Shutdown TO:  " << cfgInt("shutdown.timeout", 30000) << "ms\n";
    std::cout << "  ────────────────────────────────────────────────────────────────\n";
    std::cout << "  Starting Servers:\n";
}

// ============================================================================
// 启动 HTTP 服务器
// ============================================================================
static void startHttpServer() {
    if (!cfgInt("servers.http.enabled", 1)) {
        std::cout << "    [HTTP]  Disabled\n";
        return;
    }

    std::string addrStr = cfgStr("servers.http.address", "0.0.0.0:8080");
    int keepalive = cfgInt("servers.http.keepalive", 1);
    int timeout = cfgInt("servers.http.timeout", 120000);
    std::string name = cfgStr("servers.http.name", "http-server");

    g_httpServer.reset(new http::HttpServer(keepalive != 0));
    g_httpServer->setName(name);

    // 注册内建 Servlet (健康检查等)
    g_httpServer->getServletDispatch()->addServlet("/healthz",
        std::make_shared<http::HealthzServlet>());

    // 绑定地址 (支持多个, 逗号分隔)
    std::vector<std::string> addrs;
    std::string addrCopy = addrStr;
    size_t pos = 0;
    while ((pos = addrCopy.find(',')) != std::string::npos) {
        addrs.push_back(addrCopy.substr(0, pos));
        addrCopy = addrCopy.substr(pos + 1);
    }
    if (!addrCopy.empty()) addrs.push_back(addrCopy);

    for (auto& a : addrs) {
        // trim
        size_t s = a.find_first_not_of(" \t");
        size_t e = a.find_last_not_of(" \t");
        if (s != std::string::npos && e != std::string::npos)
            a = a.substr(s, e - s + 1);

        auto addr = Address::LookupAny(a);
        while (!g_httpServer->bind(addr)) {
            ZERO_LOG_ERROR(g_logger) << "HTTP bind failed for " << a << ", retrying...";
            sleep(2);
        }
    }

    g_httpServer->start();
    std::cout << "    [HTTP] ✓ " << name << " listening on " << addrStr
              << " (keepalive=" << (keepalive ? "ON" : "OFF")
              << ", timeout=" << timeout << "ms)\n";

    ZERO_LOG_INFO(g_logger) << "HTTP server started: " << addrStr;
}

// ============================================================================
// 启动 KV 服务器
// ============================================================================
static void startKvServer() {
    if (!cfgInt("servers.kv.enabled", 0)) {
        std::cout << "    [KV]   Disabled\n";
        return;
    }

    std::string addrStr = cfgStr("servers.kv.address", "0.0.0.0:6379");
    std::string rdbPath = cfgStr("servers.kv.rdb_path", "./dump.rdb");
    std::string aofPath = cfgStr("servers.kv.aof_path", "./appendonly.aof");
    int aofEnabled = cfgInt("servers.kv.aof_enabled", 0);
    int autoSaveSec = cfgInt("servers.kv.autosave_sec", 60);
    int httpAdminPort = cfgInt("servers.kv.http_admin_port", 0);
    int rpcPort = cfgInt("servers.kv.rpc_port", 0);

    // 创建 KV Store 和 Server
    auto store = std::make_shared<kv::KvStore>();
    store->setRdbPath(rdbPath);

    g_kvServer.reset(new kv::KvServer(store));
    g_kvServer->setAutoSaveSec(autoSaveSec);
    if (g_kvServer->getAof()) {
        g_kvServer->getAof()->setPath(aofPath);
        g_kvServer->getAof()->setEnabled(aofEnabled != 0);
    }

    auto addr = Address::LookupAny(addrStr);
    while (!g_kvServer->bind(addr)) {
        ZERO_LOG_ERROR(g_logger) << "KV bind failed for " << addrStr << ", retrying...";
        sleep(2);
    }
    g_kvServer->start();

    std::cout << "    [KV]   ✓ listening on " << addrStr
              << " (rdb=" << rdbPath
              << ", aof=" << (aofEnabled ? "ON" : "OFF")
              << ", autosave=" << autoSaveSec << "s)\n";

    ZERO_LOG_INFO(g_logger) << "KV server started: " << addrStr;

    // HTTP 管理接口
    if (httpAdminPort > 0) {
        auto adminServer = std::make_shared<http::HttpServer>(true);
        adminServer->setName("kv-admin");
        kv::registerKvAdminServlets(adminServer, store, g_kvServer->getReplication());
        auto httpAddr = Address::LookupAny("0.0.0.0:" + std::to_string(httpAdminPort));
        while (!adminServer->bind(httpAddr)) {
            sleep(2);
        }
        adminServer->start();
        std::cout << "    [KV-Admin] ✓ HTTP admin on 0.0.0.0:" << httpAdminPort << "\n";
    }

    // RPC 端口 (Sentinel 健康检查 / SLAVEOF)
    if (rpcPort > 0) {
        auto handler = rpc::MakeKvNodeHandler(std::weak_ptr<kv::KvServer>(g_kvServer));
        g_kvRpcServer.reset(new rpc::RpcServer(handler));
        auto rpcAddr = Address::LookupAny("0.0.0.0:" + std::to_string(rpcPort));
        while (!g_kvRpcServer->bind(rpcAddr)) {
            sleep(2);
        }
        g_kvRpcServer->start();
        std::cout << "    [KV-RPC] ✓ RPC on 0.0.0.0:" << rpcPort << "\n";
    }
}

// ============================================================================
// 启动 RPC 服务器
// ============================================================================
static void startRpcServer() {
    if (!cfgInt("servers.rpc.enabled", 0)) {
        std::cout << "    [RPC]  Disabled\n";
        return;
    }

    // RPC 服务器: 使用通用处理器回显请求类型
    std::string addrStr = cfgStr("servers.rpc.address", "0.0.0.0:9090");
    std::string name = cfgStr("servers.rpc.name", "rpc-server");

    // 默认处理器: 回显 request_id
    auto defaultHandler = [](const rpc::RpcEnvelope& req, rpc::RpcEnvelope& rsp) {
        rsp.set_request_id(req.request_id());
    };
    g_rpcServer.reset(new rpc::RpcServer(defaultHandler));
    g_rpcServer->setName(name);

    auto addr = Address::LookupAny(addrStr);
    while (!g_rpcServer->bind(addr)) {
        ZERO_LOG_ERROR(g_logger) << "RPC bind failed for " << addrStr << ", retrying...";
        sleep(2);
    }
    g_rpcServer->start();
    std::cout << "    [RPC]  ✓ " << name << " listening on " << addrStr << "\n";

    ZERO_LOG_INFO(g_logger) << "RPC server started: " << addrStr;
}

// ============================================================================
// 优雅关闭
// ============================================================================
static void doGracefulShutdown() {
    std::cout << "\n  Shutting down...\n";
    g_running = false;

    int shutdownTimeout = cfgInt("shutdown.timeout", 30000);
    SignalManager::GetInstance()->setShutdownTimeout(shutdownTimeout);

    // 按顺序停止: RPC → HTTP Admin → KV → HTTP
    if (g_rpcServer) {
        std::cout << "    Stopping RPC server...\n";
        g_rpcServer->stop();
    }
    if (g_kvRpcServer) {
        std::cout << "    Stopping KV RPC server...\n";
        g_kvRpcServer->stop();
    }

    if (g_kvServer) {
        std::cout << "    Stopping KV server...\n";
        g_kvServer->stop();
    }

    if (g_httpServer) {
        std::cout << "    Stopping HTTP server...\n";
        g_httpServer->stop();
    }

    // 停止 IOManager 调度
    if (g_iom) {
        g_iom->stop();
    }

    std::cout << "  Zero Framework stopped. Goodbye!\n\n";
}

// ============================================================================
// 守护进程化
// ============================================================================
static bool daemonize() {
    pid_t pid = fork();
    if (pid < 0)  return false;
    if (pid > 0)  _exit(0);  // 父进程退出

    // 子进程
    setsid();
    umask(0);

    // 重定向 stdin/stdout/stderr
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
    return true;
}

// ============================================================================
// 写入 PID 文件
// ============================================================================
static void writePidFile(const std::string& path) {
    if (path.empty()) return;
    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << getpid() << "\n";
    }
}

// ============================================================================
// 命令行使用说明
// ============================================================================
static void usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -c <dir>             配置目录 (默认: conf)\n"
              << "  -p <profile>         激活配置 Profile (dev/test/prod)\n"
              << "  --<key>=<value>      覆盖任意配置项\n"
              << "                       例如: --app.workers=8\n"
              << "                       例如: --servers.http.port=9090\n"
              << "  -d                   守护进程模式\n"
              << "  --help               显示此帮助\n\n"
              << "Environment Variables:\n"
              << "  ZERO_APP_WORKERS=8   等效于 --app.workers=8\n"
              << "  (环境变量名转小写, '_' → '.')\n\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " -c /etc/zero/conf\n"
              << "  " << prog << " -p prod --app.workers=16\n"
              << "  " << prog << " --servers.kv.enabled=1 --servers.http.port=9090\n"
              << std::endl;
}

// ============================================================================
// main - 应用入口
// ============================================================================
int main(int argc, char** argv) {
    // 1. 初始化环境管理器
    EnvMgr::GetInstance()->init(argc, argv);

    // 2. 解析基本命令行参数 (-c, -p, -d, --help)
    std::string configPath = "conf";
    std::string profile;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            profile = argv[++i];
        } else if (!strcmp(argv[i], "-d")) {
            // daemon mode handled later
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        }
    }

    // 3. 注册所有配置项 (带默认值)
    registerConfigVars();

    // 4. 加载 YAML 配置文件
    Config::LoadFromConfDir(configPath);

    // 5. 环境变量覆盖 (ZERO_ 前缀的变量)
    Config::LoadFromEnv();

    // 6. 命令行参数覆盖 (--key=value)
    Config::LoadFromCmdArgs(argc, argv);

    // 7. 激活 Profile (如果指定)
    if (!profile.empty()) {
        Config::ActivateProfile(profile);
    }

    // 8. 守护进程模式
    if (cfgInt("app.daemon", 0) || EnvMgr::GetInstance()->has("d")) {
        if (!daemonize()) {
            std::cerr << "Daemonize failed!\n";
            return 2;
        }
    }

    // 9. 写入 PID 文件
    writePidFile(cfgStr("app.pid_file", ""));

    // 10. 打印 Banner
    printBanner();

    // 11. 启动配置热重载 (inotify)
    if (cfgInt("config_reload.enabled", 1)) {
        Config::StartWatch(configPath);
    }

    // 12. 获取配置值
    int workers = cfgInt("app.workers", 6);
    int hookEnabled = cfgInt("app.hook_enable", 1);
    int shutdownTimeout = cfgInt("shutdown.timeout", 30000);

    // 13. 设置 fiber I/O hook
    if (hookEnabled) {
        set_hook_enable(true);
    }

    // 14. 打印启动摘要
    printStartupSummary();

    // 15. 设置信号处理
    SignalManager::GetInstance()->setShutdownTimeout(shutdownTimeout);
    SignalManager::GetInstance()->registerHandler(SIGINT, []() {
        if (g_running.exchange(false)) {
            doGracefulShutdown();
        }
    });
    SignalManager::GetInstance()->registerHandler(SIGTERM, []() {
        if (g_running.exchange(false)) {
            doGracefulShutdown();
        }
    });
    SignalManager::GetInstance()->start();

    // 16. 创建 IOManager 并启动所有服务器
    IOManager iom(workers, true, "main");
    g_iom = &iom;

    iom.schedule([&]() {
        // 按顺序启动各服务器
        startHttpServer();
        startKvServer();
        startRpcServer();
        // WebSocket 服务器
        if (cfgInt("servers.ws.enabled", 0)) {
            std::cout << "    [WS]   WebSocket support available, use ws_bench_server\n";
        }

        std::cout << "  ────────────────────────────────────────────────────────────────\n";
        std::cout << "  ✓ All servers started. Application is ready.\n";
        std::cout << "  Press Ctrl+C to stop.\n\n";
    });

    // 17. 主循环等待
    while (g_running.load()) {
        sleep(1);
    }

    // 18. 清理
    Config::StopWatch();
    SignalManager::GetInstance()->stop();

    return 0;
}

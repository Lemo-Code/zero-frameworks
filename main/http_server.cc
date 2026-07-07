/**
 * @file http_server.cc
 * @brief HTTP 服务器最终版 - Spring Boot 风格启动
 * @details 完整的生产级 HTTP 服务器启动入口:
 *          1. YAML 配置文件加载
 *          2. 命令行参数覆盖
 *          3. 环境变量覆盖
 *          4. 优雅关闭 (SIGINT/SIGTERM)
 *          5. 启动横幅和摘要
 *          6. 配置热重载
 *
 * 用法:
 *          ./http_server
 *          ./http_server -c /path/to/conf
 *          ./http_server --app.workers=6 --servers.http.address=0.0.0.0:9090
 *          ./http_server --servers.http.keepalive=0
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

#include "zero/http/http_server.h"
#include "zero/http/servlet.h"
#include "zero/http/servlets/health_servlet.h"
#include "zero/http/servlets/status_servlet.h"
#include "zero/http/servlets/config_servlet.h"

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
static http::HttpServer::ptr g_server;

// ============================================================================
// 注册配置项
// ============================================================================
static void registerConfigVars() {
    // 应用配置
    Config::Lookup<std::string>("app.name", "zero-http-server", "应用名称");
    Config::Lookup<std::string>("app.version", "1.0.0", "应用版本");
    Config::Lookup<int>("app.workers", 6, "工作线程数");
    Config::Lookup<int>("app.daemon", 0, "守护进程模式");
    Config::Lookup<int>("app.hook_enable", 1, "启用 fiber I/O hook");
    Config::Lookup<std::string>("app.pid_file", "", "PID 文件路径");
    Config::Lookup<std::string>("app.config_path", "conf", "配置文件目录");

    // HTTP 服务器配置
    Config::Lookup<int>("servers.http.enabled", 1, "启用 HTTP 服务器");
    Config::Lookup<std::string>("servers.http.address", "0.0.0.0:8080", "监听地址");
    Config::Lookup<int>("servers.http.keepalive", 1, "HTTP Keep-Alive");
    Config::Lookup<int>("servers.http.timeout", 120000, "超时时间(ms)");
    Config::Lookup<std::string>("servers.http.name", "http-server", "服务器名称");
    Config::Lookup<int>("servers.http.ssl", 0, "启用 SSL");
    Config::Lookup<std::string>("servers.http.cert_file", "", "SSL 证书");
    Config::Lookup<std::string>("servers.http.key_file", "", "SSL 私钥");

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
              << "  │   ██╗  ██╗████████╗████████╗██████╗                           │\n"
              << "  │   ██║  ██║╚══██╔══╝╚══██╔══╝██╔══██╗                          │\n"
              << "  │   ███████║   ██║      ██║   ██████╔╝                          │\n"
              << "  │   ██╔══██║   ██║      ██║   ██╔═══╝                           │\n"
              << "  │   ██║  ██║   ██║      ██║   ██║                               │\n"
              << "  │   ╚═╝  ╚═╝   ╚═╝      ╚═╝   ╚═╝                               │\n"
              << "  │                                                              │\n"
              << "  │   Zero HTTP Server  ::  High-Performance HTTP Engine          │\n"
              << "  └──────────────────────────────────────────────────────────────┘\n"
              << "\n"
              << "  Application:  " << cfgStr("app.name") << " v" << cfgStr("app.version") << "\n"
              << "  System:       " << sysinfo.sysname << " " << sysinfo.release
              << " (" << sysinfo.machine << ")\n"
              << "  PID:          " << getpid() << "\n"
              << "  Working Dir:  " << EnvMgr::GetInstance()->getCwd() << "\n\n";
}

// ============================================================================
// 启动服务器
// ============================================================================
static void startServer() {
    std::string addrStr = cfgStr("servers.http.address", "0.0.0.0:8080");
    int keepalive = cfgInt("servers.http.keepalive", 1);
    int timeout = cfgInt("servers.http.timeout", 120000);
    std::string name = cfgStr("servers.http.name", "http-server");

    g_server.reset(new http::HttpServer(keepalive != 0));
    g_server->setName(name);
    g_server->setRecvTimeout(timeout);

    // 注册内建健康检查
    g_server->getServletDispatch()->addServlet(
        "/healthz", std::make_shared<http::HealthzServlet>());

    // 解析地址（支持逗号分隔多地址）
    std::vector<std::string> addrs;
    std::string remain = addrStr;
    size_t pos;
    while ((pos = remain.find(',')) != std::string::npos) {
        addrs.push_back(remain.substr(0, pos));
        remain = remain.substr(pos + 1);
    }
    if (!remain.empty()) addrs.push_back(remain);

    // 绑定
    for (auto& a : addrs) {
        size_t s = a.find_first_not_of(" \t");
        size_t e = a.find_last_not_of(" \t");
        if (s != std::string::npos && e != std::string::npos)
            a = a.substr(s, e - s + 1);

        auto addr = Address::LookupAny(a);
        while (!g_server->bind(addr)) {
            ZERO_LOG_ERROR(g_logger) << "HTTP bind failed: " << a << ", retrying...";
            sleep(2);
        }
    }

    if (g_server->getSocks().empty()) {
        std::cerr << "  ERROR: No addresses bound. Exiting.\n";
        exit(1);
    }

    g_server->start();

    // 注册到 SignalManager
    SignalManager::GetInstance()->registerServer("http-server", []() {
        if (g_server) g_server->stop();
    });

    std::cout << "\n"
              << "  ┌──────────────────────────────────────────────────────────────┐\n"
              << "  │  HTTP Server Ready                                           │\n"
              << "  ├──────────────────────────────────────────────────────────────┤\n"
              << "  │  Listen:    " << std::left << std::setw(50) << addrStr << "│\n"
              << "  │  Name:      " << std::left << std::setw(50) << name << "│\n"
              << "  │  Workers:   " << std::left << std::setw(50) << cfgInt("app.workers") << "│\n"
              << "  │  KeepAlive: " << std::left << std::setw(50)
              << (keepalive ? "ON" : "OFF") << "│\n"
              << "  │  Timeout:   " << std::left << std::setw(50)
              << (std::to_string(timeout) + "ms") << "│\n"
              << "  └──────────────────────────────────────────────────────────────┘\n\n"
              << "  Endpoints:\n"
              << "    GET  /healthz  - 健康检查\n"
              << "\n"
              << "  Press Ctrl+C to stop.\n\n";
}

// ============================================================================
// 优雅关闭
// ============================================================================
static void doShutdown() {
    if (!g_running.exchange(false)) return;
    std::cout << "\n  Shutting down HTTP server...\n";
    SignalManager::GetInstance()->gracefulShutdown(SIGTERM);
    std::cout << "  HTTP server stopped. Goodbye!\n\n";
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
              << "                       例如: --app.workers=8\n"
              << "                       例如: --servers.http.address=0.0.0.0:9090\n"
              << "  --help               显示帮助\n\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " --app.workers=8\n"
              << "  " << prog << " --servers.http.address=0.0.0.0:9090\n"
              << "  " << prog << " -p prod -d\n"
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

    // 8. 启动 IOManager 和 HTTP 服务器
    std::cout << "  Configuration:\n"
              << "    Workers:      " << workers << "\n"
              << "    Config Dir:   " << configPath << "\n"
              << "    Hot Reload:   " << (cfgInt("config_reload.enabled", 1) ? "ON" : "OFF") << "\n"
              << "    Shutdown TO:  " << shutdownTimeout << "ms\n"
              << "    Fiber Hook:   " << (hookEnabled ? "ON" : "OFF") << "\n\n";

    IOManager iom(workers, true, "http");
    iom.schedule(startServer);

    // 9. 等待关闭信号
    while (g_running.load()) {
        sleep(1);
    }

    // 10. 清理
    Config::StopWatch();
    SignalManager::GetInstance()->stop();

    return 0;
}

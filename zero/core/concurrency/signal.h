/**
 * @file signal.h
 * @brief 信号管理与优雅关闭
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_SIGNAL_H__
#define __ZERO_SIGNAL_H__

#include <memory>
#include <functional>
#include <vector>
#include <map>
#include <atomic>
#include <string>
#include <chrono>
#include "zero/core/base/noncopyable.h"
#include "mutex.h"

namespace zero {

/**
 * @brief 信号处理器
 */
class SignalManager : public Noncopyable {
public:
    typedef std::shared_ptr<SignalManager> ptr;
    typedef std::function<void()> SignalHandler;

    /**
     * @brief 获取全局单例
     */
    static SignalManager::ptr GetInstance();

    /**
     * @brief 注册信号处理器
     * @param[in] sig 信号编号 (SIGINT, SIGTERM, etc.)
     * @param[in] handler 处理回调
     * @param[in] append 是否追加到现有处理器（true）还是覆盖（false）
     */
    void registerHandler(int sig, SignalHandler handler, bool append = true);

    /**
     * @brief 注销信号处理器
     */
    void unregisterHandler(int sig);

    /**
     * @brief 启动信号监听（必须在主线程调用）
     */
    void start();

    /**
     * @brief 停止信号监听
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const { return m_running; }

    /**
     * @brief 设置优雅关闭超时（毫秒）
     */
    void setShutdownTimeout(uint64_t ms) { m_shutdownTimeout = ms; }

    /**
     * @brief 获取优雅关闭超时
     */
    uint64_t getShutdownTimeout() const { return m_shutdownTimeout; }

    /**
     * @brief 注册服务器用于优雅关闭
     * @param[in] name 服务器名称
     * @param[in] stopFunc 停止回调
     */
    void registerServer(const std::string& name, std::function<void()> stopFunc);

    /**
     * @brief 执行优雅关闭
     * @param[in] sig 触发信号
     */
    void gracefulShutdown(int sig);

    /**
     * @brief 等待所有服务器关闭完成
     * @param[in] timeoutMs 超时毫秒
     * @return 是否全部正常关闭
     */
    bool waitForShutdown(uint64_t timeoutMs);

    ~SignalManager();

private:
    SignalManager();

    static void onSignal(int sig);
    void handleSignal(int sig);
    void runEventLoop();

private:
    static SignalManager::ptr s_instance;
    std::map<int, std::vector<SignalHandler>> m_handlers;
    std::atomic<bool> m_running{false};
    int m_signalFds[2];  // self-pipe for async-signal-safe notification
    uint64_t m_shutdownTimeout = 30000;  // 30s default
    std::thread m_eventThread;

    struct ServerEntry {
        std::string name;
        std::function<void()> stopFunc;
        std::atomic<bool> stopped{false};
        
        ServerEntry() = default;
        ServerEntry(const ServerEntry& o) 
            : name(o.name), stopFunc(o.stopFunc), stopped(o.stopped.load()) {}
        ServerEntry& operator=(const ServerEntry& o) {
            name = o.name;
            stopFunc = o.stopFunc;
            stopped = o.stopped.load();
            return *this;
        }
    };
    std::vector<ServerEntry> m_servers;
    Mutex m_mutex;
};

/**
 * @brief 便捷的注册优雅关闭宏
 */
#define ZERO_REGISTER_SERVER_FOR_SHUTDOWN(name, serverPtr) \
    zero::SignalManager::GetInstance()->registerServer(name, [serverPtr]() { serverPtr->stop(); })

} // namespace zero

#endif // __ZERO_SIGNAL_H__

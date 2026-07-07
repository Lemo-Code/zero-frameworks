/**
 * @file signal.cc
 * @brief 信号管理实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "signal.h"
#include "zero/core/log/log.h"
#include "fiber.h"
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

SignalManager::ptr SignalManager::s_instance = nullptr;

SignalManager::ptr SignalManager::GetInstance() {
    if(!s_instance) {
        s_instance.reset(new SignalManager);
    }
    return s_instance;
}

SignalManager::SignalManager() {
    if(pipe(m_signalFds) != 0) {
        ZERO_LOG_FATAL(g_logger) << "SignalManager pipe creation failed: " << strerror(errno);
        throw std::runtime_error("pipe creation failed");
    }
}

SignalManager::~SignalManager() {
    stop();
    close(m_signalFds[0]);
    close(m_signalFds[1]);
}

void SignalManager::registerHandler(int sig, SignalHandler handler, bool append) {
    Mutex::Lock lock(m_mutex);
    if(!append) {
        m_handlers[sig].clear();
    }
    m_handlers[sig].push_back(handler);
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SignalManager::onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(sig, &sa, nullptr) != 0) {
        ZERO_LOG_ERROR(g_logger) << "sigaction failed for signal " << sig 
                                   << ": " << strerror(errno);
    }
    ZERO_LOG_INFO(g_logger) << "Registered handler for signal " << sig;
}

void SignalManager::unregisterHandler(int sig) {
    Mutex::Lock lock(m_mutex);
    m_handlers.erase(sig);
    signal(sig, SIG_DFL);
}

void SignalManager::registerServer(const std::string& name, std::function<void()> stopFunc) {
    Mutex::Lock lock(m_mutex);
    ServerEntry entry;
    entry.name = name;
    entry.stopFunc = stopFunc;
    entry.stopped = false;
    m_servers.push_back(entry);
    ZERO_LOG_INFO(g_logger) << "Registered server for graceful shutdown: " << name;
}

void SignalManager::gracefulShutdown(int sig) {
    ZERO_LOG_INFO(g_logger) << "Graceful shutdown initiated by signal " << sig;
    
    std::vector<ServerEntry> servers;
    {
        Mutex::Lock lock(m_mutex);
        servers = m_servers;
    }
    
    // 停止所有服务器（并行）
    for(auto& server : servers) {
        if(!server.stopped.exchange(true)) {
            ZERO_LOG_INFO(g_logger) << "Stopping server: " << server.name;
            try {
                server.stopFunc();
            } catch(...) {
                ZERO_LOG_ERROR(g_logger) << "Exception during stop of " << server.name;
            }
        }
    }
    
    // 等待关闭完成
    bool allStopped = waitForShutdown(m_shutdownTimeout);
    if(!allStopped) {
        ZERO_LOG_WARN(g_logger) << "Graceful shutdown timed out, forcing exit";
    }
    
    ZERO_LOG_INFO(g_logger) << "Graceful shutdown complete";
}

bool SignalManager::waitForShutdown(uint64_t timeoutMs) {
    auto start = std::chrono::steady_clock::now();
    while(true) {
        bool allStopped = true;
        {
            Mutex::Lock lock(m_mutex);
            for(auto& server : m_servers) {
                if(!server.stopped) {
                    allStopped = false;
                    break;
                }
            }
        }
        if(allStopped) {
            return true;
        }
        
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if((uint64_t)elapsed >= timeoutMs) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void SignalManager::onSignal(int sig) {
    auto instance = GetInstance();
    if(!instance || !instance->isRunning()) {
        return;
    }
    
    // 使用 self-pipe 技巧实现 async-signal-safe 通知
    char signum = static_cast<char>(sig);
    if(write(instance->m_signalFds[1], &signum, 1) != 1) {
        // 如果写入失败，直接处理（降级）
        instance->handleSignal(sig);
    }
}

void SignalManager::handleSignal(int sig) {
    ZERO_LOG_INFO(g_logger) << "Signal received: " << sig;
    
    std::vector<SignalHandler> handlers;
    {
        Mutex::Lock lock(m_mutex);
        auto it = m_handlers.find(sig);
        if(it != m_handlers.end()) {
            handlers = it->second;
        }
    }
    
    for(auto& handler : handlers) {
        try {
            handler();
        } catch(...) {
            ZERO_LOG_ERROR(g_logger) << "Exception in signal handler for signal " << sig;
        }
    }
}

void SignalManager::runEventLoop() {
    while(m_running) {
        char sig;
        ssize_t n = read(m_signalFds[0], &sig, 1);
        if(n == 1) {
            handleSignal(static_cast<int>(sig));
        } else if(n < 0 && errno != EAGAIN && errno != EINTR) {
            ZERO_LOG_ERROR(g_logger) << "SignalManager read error: " << strerror(errno);
            break;
        }
    }
}

void SignalManager::start() {
    if(m_running.exchange(true)) {
        return;
    }

    // 设置 pipe 为非阻塞
    int flags = fcntl(m_signalFds[0], F_GETFL, 0);
    fcntl(m_signalFds[0], F_SETFL, flags | O_NONBLOCK);

    flags = fcntl(m_signalFds[1], F_GETFL, 0);
    fcntl(m_signalFds[1], F_SETFL, flags | O_NONBLOCK);

    m_eventThread = std::thread(&SignalManager::runEventLoop, this);

    ZERO_LOG_INFO(g_logger) << "SignalManager started";
}

void SignalManager::stop() {
    if(!m_running.exchange(false)) {
        return;
    }

    // 唤醒事件循环
    char dummy = 0;
    if (write(m_signalFds[1], &dummy, 1) < 0) {
        // ignore write error
    }

    if(m_eventThread.joinable()) {
        m_eventThread.join();
    }

    ZERO_LOG_INFO(g_logger) << "SignalManager stopped";
}

} // namespace zero

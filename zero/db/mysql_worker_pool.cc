/**
 * @file mysql_worker_pool.cc
 * @brief MySQL 专用线程池实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "mysql_worker_pool.h"
#include "zero/core/concurrency/scheduler.h"
#include "zero/core/concurrency/fiber.h"
#include "zero/core/log/log.h"

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

MySQLWorkerPool::MySQLWorkerPool(size_t threads, Scheduler* scheduler)
    : m_scheduler(scheduler) {
    if(threads == 0) threads = 1;
    for(size_t i = 0; i < threads; ++i) {
        m_threads.emplace_back([this]() { run(); });
    }
    ZERO_LOG_INFO(g_logger) << "MySQLWorkerPool started with " << threads << " threads";
}

MySQLWorkerPool::ptr MySQLWorkerPool::create(size_t threads, Scheduler* scheduler) {
    return std::shared_ptr<MySQLWorkerPool>(new MySQLWorkerPool(threads, scheduler));
}

MySQLWorkerPool::~MySQLWorkerPool() {
    stop(true);
}

void MySQLWorkerPool::submit(std::function<void()> task, std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(!m_running) {
            ZERO_LOG_WARN(g_logger) << "MySQLWorkerPool already stopped, task dropped";
            return;
        }
        m_tasks.push({task, callback});
        ++m_pending;
    }
    m_cv.notify_one();
}

void MySQLWorkerPool::stop(bool waitFinish) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_waitFinish = waitFinish;
        m_running = false;
    }
    m_cv.notify_all();
    for(auto& t : m_threads) {
        if(t.joinable()) t.join();
    }
}

size_t MySQLWorkerPool::pendingCount() const {
    return m_pending.load();
}

void MySQLWorkerPool::run() {
    while(true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return !m_tasks.empty() || !m_running;
            });
            if(!m_running && (!m_waitFinish || m_tasks.empty())) {
                break;
            }
            if(m_tasks.empty()) {
                continue;
            }
            task = m_tasks.front();
            m_tasks.pop();
        }

        try {
            task.task();
        } catch(...) {
            ZERO_LOG_ERROR(g_logger) << "MySQLWorkerPool task exception";
        }

        --m_pending;

        if(task.callback) {
            if(m_scheduler) {
                m_scheduler->schedule(task.callback);
            } else {
                try {
                    task.callback();
                } catch(...) {
                    ZERO_LOG_ERROR(g_logger) << "MySQLWorkerPool callback exception";
                }
            }
        }
    }
}

} // namespace zero

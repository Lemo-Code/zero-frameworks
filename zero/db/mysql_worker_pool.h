/**
 * @file mysql_worker_pool.h
 * @brief MySQL 专用线程池
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_DB_MYSQL_WORKER_POOL_H__
#define __ZERO_DB_MYSQL_WORKER_POOL_H__

#include <memory>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>

namespace zero {

class Scheduler;

/**
 * @brief MySQL 阻塞任务线程池
 * 
 * 将 libmysqlclient 的阻塞 I/O 操作提交到本线程池执行，
 * 执行完成后回调由传入的 Scheduler 调度回协程。
 */
class MySQLWorkerPool {
public:
    typedef std::shared_ptr<MySQLWorkerPool> ptr;

    static MySQLWorkerPool::ptr create(size_t threads, Scheduler* scheduler);

    ~MySQLWorkerPool();

    /**
     * @brief 提交一个阻塞任务
     * @param task 阻塞执行的 SQL 操作
     * @param callback 任务完成后回调（会被调度回协程）
     */
    void submit(std::function<void()> task, std::function<void()> callback = nullptr);

    /**
     * @brief 停止线程池
     * @param waitFinish 是否等待队列中剩余任务执行完再停止
     */
    void stop(bool waitFinish = true);

    /**
     * @brief 返回当前等待任务数（近似值）
     */
    size_t pendingCount() const;

private:
    struct Task {
        std::function<void()> task;
        std::function<void()> callback;
    };

    MySQLWorkerPool(size_t threads, Scheduler* scheduler);
    void run();

    Scheduler* m_scheduler = nullptr;
    std::vector<std::thread> m_threads;
    std::queue<Task> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{true};
    std::atomic<bool> m_waitFinish{true};
    std::atomic<size_t> m_pending{0};
};

} // namespace zero

#endif // __ZERO_DB_MYSQL_WORKER_POOL_H__

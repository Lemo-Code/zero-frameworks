/**
 * @file log_qps_matrix.cc
 * @brief 日志系统QPS性能基准测试矩阵
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/log/log.h"
#include "zero/core/util/env.h"
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <unistd.h>

using namespace zero;

struct QPSResult {
    std::string test_name;
    int num_threads;
    size_t total_msgs;
    double elapsed_ms;
    double qps;
    bool is_async;
};

class QPSTimer {
public:
    QPSTimer() : m_start(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ms() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - m_start).count();
    }
private:
    std::chrono::high_resolution_clock::time_point m_start;
};

// 运行一次QPS测试
QPSResult runQPS(const std::string& name, int num_threads, size_t msgs_per_thread,
                  LogAppender::ptr appender, bool async) {
    auto logger = ZERO_LOG_NAME("qps_bench");
    logger->clearAppenders();
    logger->setLevel(LogLevel::DEBUG);

    LogAppender::ptr actual_appender = appender;
    if (async) {
        AsyncLogConfig cfg;
        cfg.queue_size = 256 * 1024 * 1024;  // 256MB
        cfg.batch_size = 1024;
        cfg.flush_interval_ms = 1;
        cfg.overflow_policy = AsyncOverflowPolicy::BLOCK;
        actual_appender = std::make_shared<AsyncAppenderWrapper>(appender, cfg);
    }
    logger->addAppender(actual_appender);

    std::atomic<bool> ready{false};
    std::atomic<size_t> total_written{0};
    std::atomic<bool> error_occurred{false};

    QPSTimer timer;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            auto thread_logger = ZERO_LOG_NAME("qps_worker_" + std::to_string(t));
            thread_logger->clearAppenders();
            thread_logger->addAppender(actual_appender);
            thread_logger->setLevel(LogLevel::DEBUG);

            // 等待发令
            while (!ready.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            for (size_t i = 0; i < msgs_per_thread; ++i) {
                ZERO_LOG_DEBUG(thread_logger) << "QPS bench message " << i;
                total_written.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // 开始计时
    ready.store(true, std::memory_order_release);

    for (auto& th : threads) {
        th.join();
    }

    double elapsed = timer.elapsed_ms();

    // 如果是异步，等待落盘
    if (async) {
        actual_appender->flush();
    }

    QPSResult result;
    result.test_name = name;
    result.num_threads = num_threads;
    result.total_msgs = total_written.load();
    result.elapsed_ms = elapsed;
    result.qps = (result.total_msgs / elapsed) * 1000.0;
    result.is_async = async;

    logger->clearAppenders();
    return result;
}

void printResults(const std::vector<QPSResult>& results) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                           Log QPS Performance Benchmark Matrix                             ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ " << std::left << std::setw(35) << "Test"
              << std::setw(8) << "Threads"
              << std::setw(12) << "Mode"
              << std::setw(14) << "Messages"
              << std::setw(12) << "Elapsed(ms)"
              << std::setw(15) << "QPS(msg/s)"
              << "║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════════════════════╣\n";

    double best_qps = 0;
    for (auto& r : results) {
        std::cout << "║ " << std::left << std::setw(35) << r.test_name
                  << std::setw(8) << r.num_threads
                  << std::setw(12) << (r.is_async ? "ASYNC" : "SYNC")
                  << std::setw(14) << r.total_msgs
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.elapsed_ms
                  << std::setw(15) << std::fixed << std::setprecision(0) << r.qps
                  << "║\n";
        if (r.qps > best_qps) best_qps = r.qps;
    }

    std::cout << "╠══════════════════════════════════════════════════════════════════════════════════════════════╣\n";
    std::cout << "║ Best QPS: " << std::left << std::setw(77)
              << std::fixed << std::setprecision(0) << best_qps << " ║\n";

    bool target_met = (best_qps >= 5000000);
    std::cout << "║ Target (5,000,000): " << std::left << std::setw(71)
              << (target_met ? "ACHIEVED ✓" : "NOT MET ✗ (check system performance)") << " ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════════════════════╝\n";

    // 保存CSV
    std::ofstream csv("logs/log_qps_matrix.csv");
    csv << "Test,Threads,Mode,Messages,Elapsed(ms),QPS(msg/s)\n";
    for (auto& r : results) {
        csv << r.test_name << ","
            << r.num_threads << ","
            << (r.is_async ? "ASYNC" : "SYNC") << ","
            << r.total_msgs << ","
            << std::fixed << std::setprecision(2) << r.elapsed_ms << ","
            << std::fixed << std::setprecision(0) << r.qps << "\n";
    }
    csv.close();
    std::cout << "\nResults saved to logs/log_qps_matrix.csv" << std::endl;
}

int main(int argc, char** argv) {
    zero::EnvMgr::GetInstance()->init(argc, argv);

    std::vector<QPSResult> results;

    const size_t MSG_COUNT = 100000;     // 每个线程的消息数
    const int THREAD_COUNTS[] = {1, 2, 4, 8};
    const int SYNC_THREADS[]  = {1, 2};  // 同步测试线程数少一些，避免锁竞争过大

    std::cout << "\nRunning Log QPS Benchmark Matrix...\n";
    std::cout << "Messages per thread: " << MSG_COUNT << "\n\n";

    // ===== 1. NullAppender (最理想情况，测量框架开销) =====
    std::cout << "Testing NullAppender (framework overhead)...\n";
    for (int n : THREAD_COUNTS) {
        // 同步NullAppender
        auto null_app = std::make_shared<NullLogAppender>();
        results.push_back(runQPS("NullAppender-Sync", n, MSG_COUNT, null_app, false));
        std::cout << "  " << n << " threads sync:  "
                  << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";

        // 异步NullAppender
        auto null_app2 = std::make_shared<NullLogAppender>();
        results.push_back(runQPS("NullAppender-Async", n, MSG_COUNT, null_app2, true));
        std::cout << "  " << n << " threads async: "
                  << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";
    }

    // ===== 2. MemoryAppender (内存写入，测试序列化开销) =====
    std::cout << "\nTesting MemoryAppender (in-memory, serialization cost)...\n";
    for (int n : THREAD_COUNTS) {
        auto mem_app = std::make_shared<MemoryLogAppender>(MSG_COUNT * n * 4);
        results.push_back(runQPS("MemoryAppender-Async", n, MSG_COUNT, mem_app, true));
        std::cout << "  " << n << " threads async: "
                  << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";
    }

    // ===== 3. FileAppender (磁盘写入，测量实际I/O吞吐) =====
    std::cout << "\nTesting FileAppender (disk I/O)...\n";
    {
        std::string tmpfile = "/tmp/log_qps_bench_" + std::to_string(getpid()) + ".log";

        // 同步文件 - 少量线程
        for (int n : SYNC_THREADS) {
            auto file_app1 = std::make_shared<FileLogAppender>(tmpfile, false);
            results.push_back(runQPS("FileAppender-Sync", n, MSG_COUNT / 10, file_app1, false));
            std::cout << "  " << n << " threads sync:  "
                      << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";
            unlink(tmpfile.c_str());
        }

        // 异步文件 - 多线程
        for (int n : THREAD_COUNTS) {
            auto file_app2 = std::make_shared<FileLogAppender>(tmpfile, false);
            results.push_back(runQPS("FileAppender-Async", n, MSG_COUNT, file_app2, true));
            std::cout << "  " << n << " threads async: "
                      << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";
            unlink(tmpfile.c_str());
        }
    }

    // ===== 4. 极限压力测试 - 16线程异步 =====
    std::cout << "\nRunning extreme stress test (16 threads async)...\n";
    {
        auto null_app = std::make_shared<NullLogAppender>();
        results.push_back(runQPS("Stress-16T-Async", 16, MSG_COUNT, null_app, true));
        std::cout << "  16 threads async (extreme): "
                  << std::fixed << std::setprecision(0) << results.back().qps << " msg/s\n";
    }

    // 打印汇总结果
    printResults(results);

    return 0;
}

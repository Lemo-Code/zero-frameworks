/**
 * @file test_log_system.cc
 * @brief 日志系统压力测试和功能验证
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/log/log.h"
#include "zero/core/config/config.h"
#include "zero/core/util/env.h"
#include <gtest/gtest.h>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <iostream>
#include <iomanip>
#include <unistd.h>

using namespace zero;

static Logger::ptr g_test_logger = ZERO_LOG_NAME("test");

// 辅助工具
class Timer {
public:
    Timer() : m_start(std::chrono::high_resolution_clock::now()) {}
    double elapsed_ms() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - m_start).count();
    }
    double elapsed_us() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(now - m_start).count();
    }
private:
    std::chrono::high_resolution_clock::time_point m_start;
};

static void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(70, '=') << std::endl;
}

static void printPass(bool pass) {
    (void)pass;
    // gtest handles pass/fail reporting
}

// ======================== 测试1: 各级别日志 ========================
TEST(LogSystem, LogLevels) {
    // 抑制大部分输出 - 使用NullAppender
    auto test_logger = ZERO_LOG_NAME("test_levels");
    test_logger->clearAppenders();
    auto null_app = std::make_shared<NullLogAppender>();
    test_logger->addAppender(null_app);

    test_logger->setLevel(LogLevel::TRACE);

    ZERO_LOG_TRACE(test_logger) << "trace";
    ZERO_LOG_DEBUG(test_logger) << "debug";
    ZERO_LOG_INFO(test_logger) << "info";
    ZERO_LOG_NOTICE(test_logger) << "notice";
    ZERO_LOG_WARN(test_logger) << "warn";
    ZERO_LOG_ERROR(test_logger) << "error";
    ZERO_LOG_CRITICAL(test_logger) << "critical";
    ZERO_LOG_ALERT(test_logger) << "alert";
    ZERO_LOG_FATAL(test_logger) << "fatal";
    ZERO_LOG_EMERGENCY(test_logger) << "emergency";

    // If we reach here without exception, the test passes
    SUCCEED();
}

// ======================== 测试2: 格式化模式 ========================
TEST(LogSystem, Formatters) {
    auto test_logger = ZERO_LOG_NAME("test_fmts");
    test_logger->clearAppenders();
    auto mem_app = std::make_shared<MemoryLogAppender>(100);
    test_logger->addAppender(mem_app);

    // 测试各种格式化模式
    std::vector<std::string> patterns = {
        "%d{%Y-%m-%d %H:%M:%S} %p %m%n",
        "%Y%P%y %c:%l %m%n",
        "%d %t %N %F %p %c %f:%l %m%n",
        "%h %i %a %S %s %m%n",
        "%D{%H:%M:%S} %P [%c] %m %X%n",
        "%e %E %m%n"
    };

    for (auto& pat : patterns) {
        LogFormatter fmt(pat);
        EXPECT_FALSE(fmt.isError()) << "Pattern error: " << pat;
        ZERO_LOG_INFO(test_logger) << "test message with pattern: " << pat;
    }

    // 测试结构化格式
    LogFormatter json_fmt("json");
    LogFormatter xml_fmt("xml");
    LogFormatter ltsv_fmt("ltsv");
    LogFormatter cef_fmt("cef");
    (void)json_fmt; (void)xml_fmt; (void)ltsv_fmt; (void)cef_fmt;
}

// ======================== 测试3: Appender创建 ========================
TEST(LogSystem, Appenders) {
    auto a1 = std::make_shared<StdoutLogAppender>();
    auto a2 = std::make_shared<StderrLogAppender>();
    auto a3 = std::make_shared<ColorStdoutLogAppender>();
    auto a4 = std::make_shared<FileLogAppender>("/tmp/test_file.log");
    auto a5 = std::make_shared<RotatingFileLogAppender>("/tmp/test_rot.log", 1024*1024, 3);
    auto a6 = std::make_shared<TimeRotatingFileLogAppender>("/tmp/test_time.log");
    auto a7 = std::make_shared<MemoryLogAppender>(100);
    auto a8 = std::make_shared<NullLogAppender>();

    // 验证类型名
    EXPECT_EQ(a1->getTypeName(), "StdoutLogAppender");
    EXPECT_EQ(a3->getTypeName(), "ColorStdoutLogAppender");
    EXPECT_EQ(a4->getTypeName(), "FileLogAppender");
    EXPECT_EQ(a7->getTypeName(), "MemoryLogAppender");
    EXPECT_EQ(a8->getTypeName(), "NullLogAppender");

    // 清理
    a4->close();
    unlink("/tmp/test_file.log");
    unlink("/tmp/test_rot.log");
    unlink("/tmp/test_time.log");
    (void)a2; (void)a5; (void)a6;
}

// ======================== 测试4: 过滤器 ========================
TEST(LogSystem, Filters) {
    auto test_logger = ZERO_LOG_NAME("test_filters");
    test_logger->clearAppenders();
    auto mem_app = std::make_shared<MemoryLogAppender>(100);
    test_logger->addAppender(mem_app);
    test_logger->setLevel(LogLevel::TRACE);

    // LevelFilter
    {
        mem_app->clear();
        auto filter = std::make_shared<LevelFilter>(LogLevel::WARN);
        test_logger->addFilter(filter);

        ZERO_LOG_INFO(test_logger) << "should be filtered";
        ZERO_LOG_ERROR(test_logger) << "should pass";

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        size_t count = mem_app->size();
        EXPECT_GE(count, 1u);  // 至少ERROR通过了
        test_logger->clearFilters();
    }

    // RateLimitFilter
    {
        mem_app->clear();
        auto filter = std::make_shared<RateLimitFilter>(5);
        test_logger->addFilter(filter);

        for (int i = 0; i < 100; ++i) {
            ZERO_LOG_INFO(test_logger) << "rate test " << i;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        size_t count = mem_app->size();
        EXPECT_LT(count, 100u);  // 应该被限速了
        test_logger->clearFilters();
    }
}

// ======================== 测试5: MDC/NDC ========================
TEST(LogSystem, Context) {
    // MDC测试
    MDC::put("test_key", "test_value");
    EXPECT_EQ(MDC::get("test_key"), "test_value");
    EXPECT_TRUE(MDC::contains("test_key"));
    MDC::remove("test_key");
    EXPECT_FALSE(MDC::contains("test_key"));

    // NDC测试
    EXPECT_TRUE(NDC::empty());
    NDC::push("level1");
    EXPECT_EQ(NDC::depth(), 1u);
    EXPECT_EQ(NDC::peek(), "level1");
    NDC::push("level2");
    EXPECT_EQ(NDC::depth(), 2u);
    EXPECT_EQ(NDC::getFormatted(" -> "), "level1 -> level2");
    NDC::pop();
    EXPECT_EQ(NDC::depth(), 1u);
    NDC::clear();
    EXPECT_TRUE(NDC::empty());

    // RAII守卫测试
    {
        MDCScope mdc_scope("scope_key", "scope_value");
        EXPECT_EQ(MDC::get("scope_key"), "scope_value");
        {
            NDCScope ndc_scope("scoped_operation");
            EXPECT_EQ(NDC::depth(), 1u);
        }
        EXPECT_TRUE(NDC::empty());
    }
    EXPECT_FALSE(MDC::contains("scope_key"));
}

// ======================== 测试6: 并发写入QPS ========================
TEST(LogSystem, Qps) {
    auto qps_logger = ZERO_LOG_NAME("test_qps");
    qps_logger->clearAppenders();
    auto null_app = std::make_shared<NullLogAppender>();
    qps_logger->addAppender(null_app);
    qps_logger->setLevel(LogLevel::DEBUG);

    const size_t NUM_THREADS = 8;
    const size_t MSG_PER_THREAD = 50000;
    std::atomic<size_t> total_written{0};

    Timer timer;

    std::vector<std::thread> threads;
    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            auto logger = ZERO_LOG_NAME("qps_" + std::to_string(t));
            logger->clearAppenders();
            logger->addAppender(null_app);
            logger->setLevel(LogLevel::DEBUG);

            for (size_t i = 0; i < MSG_PER_THREAD; ++i) {
                ZERO_LOG_DEBUG(logger) << "QPS test message " << i;
                total_written.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    double elapsed = timer.elapsed_ms();
    size_t total = total_written.load();
    double qps = (total / elapsed) * 1000.0;

    std::cout << "  Threads: " << NUM_THREADS << std::endl;
    std::cout << "  Messages per thread: " << MSG_PER_THREAD << std::endl;
    std::cout << "  Total messages: " << total << std::endl;
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
    std::cout << "  QPS: " << std::fixed << std::setprecision(0) << qps << " msg/s" << std::endl;

    EXPECT_GT(qps, 1000000.0) << "QPS below 1M - check system performance";
}

// ======================== 测试7: 环形缓冲区 ========================
TEST(LogSystem, RingBuffer) {
    const size_t CAPACITY = 1024 * 1024;  // 1MB

    LogRingBuffer buffer(CAPACITY);

    // 测试写入和读取
    LogEntryBuilder builder;
    const char* data = builder.build(
        1234567890, 1, 100, 12345, 0,
        "test message", "test_logger", "test_file.cc", "test_thread"
    );

    ASSERT_NE(data, nullptr);

    // 写入
    EXPECT_TRUE(buffer.tryWrite(data, builder.getEntrySize()));

    // 读取
    LogBufferMessage msg;
    ASSERT_TRUE(buffer.readOne(msg));
    EXPECT_EQ(msg.message, "test message");
    EXPECT_EQ(msg.logger_name, "test_logger");
    EXPECT_EQ(msg.file_name, "test_file.cc");
    EXPECT_EQ(msg.thread_name, "test_thread");

    // 批量测试
    const size_t N = 10000;
    for (size_t i = 0; i < N; ++i) {
        std::string msg_text = "batch_message_" + std::to_string(i);
        const char* d = builder.build(
            i, 2, 200, 1, 2, msg_text, "batch_logger", "batch.cc", "worker"
        );
        ASSERT_TRUE(buffer.tryWrite(d, builder.getEntrySize())) << "Write failed at index " << i;
    }

    std::vector<LogBufferMessage> batch;
    size_t read_count = buffer.readBatch(batch, 512);
    EXPECT_GT(read_count, 0u);
}

// ======================== 测试8: 配置系统 ========================
TEST(LogSystem, ConfigSystem) {
    // 测试Lookup/Set/Has
    auto var = Config::Lookup<int>("test.int.value", 42, "test int config");
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getValue(), 42);

    Config::Set<int>("test.int.value", 100);
    EXPECT_EQ(var->getValue(), 100);

    EXPECT_TRUE(Config::Has("test.int.value"));
    EXPECT_FALSE(Config::Has("nonexistent.key"));

    // 测试Listeners
    bool callback_called = false;
    uint64_t key = var->addListener([&](const int& old_v, const int& new_v) {
        (void)old_v; (void)new_v;
        callback_called = true;
    });
    Config::Set<int>("test.int.value", 200);
    EXPECT_TRUE(callback_called);
    var->delListener(key);

    // 测试Set
    Config::Set<std::string>("test.string.value", "hello world");
    auto str_var = Config::Lookup<std::string>("test.string.value");
    ASSERT_NE(str_var, nullptr);
    EXPECT_EQ(str_var->getValue(), "hello world");

    // 测试Dump
    std::string dump = Config::DumpToYaml();
    EXPECT_FALSE(dump.empty());

    // 测试Size/GetAllNames
    size_t count = Config::Size();
    auto names = Config::GetAllNames();
    EXPECT_GT(count, 0u);
    EXPECT_EQ(names.size(), count);
}

// ======================== 测试9: 条件/限频宏 ========================
TEST(LogSystem, Macros) {
    auto macro_logger = ZERO_LOG_NAME("test_macros");
    macro_logger->clearAppenders();
    auto mem_app = std::make_shared<MemoryLogAppender>(100);
    macro_logger->addAppender(mem_app);
    macro_logger->setLevel(LogLevel::DEBUG);

    // ZERO_LOG_IF
    mem_app->clear();
    ZERO_LOG_IF(true, macro_logger, LogLevel::INFO) << "condition true";
    ZERO_LOG_IF(false, macro_logger, LogLevel::INFO) << "condition false";
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mem_app->size(), 1u);  // 只有条件为true的会写入

    // ZERO_LOG_EVERY_N
    mem_app->clear();
    for (int i = 0; i < 100; ++i) {
        ZERO_LOG_EVERY_N(10, macro_logger, LogLevel::INFO) << "every 10th: " << i;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // 100次迭代, 每10次输出一次, 应输出约10条
    size_t every_n_count = mem_app->size();
    EXPECT_GE(every_n_count, 5u);
    EXPECT_LE(every_n_count, 15u);
}

// ======================== 测试10: 降级控制 ========================
TEST(LogSystem, Degrade) {
    DegradeManager dm;

    // 默认应禁用降级
    EXPECT_TRUE(dm.isDisabled());

    // 所有级别应不降级
    for (int l = LogLevel::TRACE; l <= LogLevel::EMERGENCY; ++l) {
        auto level = static_cast<LogLevel::Level>(l);
        // 跳过无效值
        if (l == 2 || l == 3 || l == 4 || l == 6 || l == 7 || l == 8 || l == 9 ||
            l == 11 || l == 12 || l == 13 || l == 14 || l == 16 || l == 17 || l == 18 || l == 19 ||
            l == 21 || l == 22 || l == 23 || l == 24 || l == 25 || l == 26 || l == 27 || l == 28 || l == 29 ||
            l == 31 || l == 32 || l == 33 || l == 34 || l == 35 || l == 36 || l == 37 || l == 38 || l == 39 ||
            l == 41 || l == 42 || l == 43 || l == 44 || l == 46 || l == 47 || l == 48 || l == 49 ||
            l == 51 || l == 52 || l == 53 || l == 54) continue;
        EXPECT_FALSE(dm.shouldDegrade(level, 0.95));  // 即使高水位也不降级
    }
}

/**
 * @file log_demo.cc
 * @brief 日志系统完整功能演示
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/log/log.h"
#include "zero/core/config/config.h"
#include "zero/core/util/env.h"
#include "zero/core/io/iomanager.h"
#include <unistd.h>
#include <thread>
#include <chrono>
#include <vector>

// 全局logger
static zero::Logger::ptr g_logger = ZERO_LOG_ROOT();

// 辅助: 打印分隔线
static void printSection(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(60, '=') << std::endl;
}

/**
 * @brief 1. 演示所有10个日志级别
 */
void demo_levels() {
    printSection("1. All 10 Log Levels");
    ZERO_LOG_TRACE(g_logger)    << "TRACE - 最详细的追踪信息";
    ZERO_LOG_DEBUG(g_logger)    << "DEBUG - 调试信息";
    ZERO_LOG_INFO(g_logger)     << "INFO  - 一般信息";
    ZERO_LOG_NOTICE(g_logger)   << "NOTICE - 需要注意的普通条件";
    ZERO_LOG_WARN(g_logger)     << "WARN  - 警告信息";
    ZERO_LOG_ERROR(g_logger)    << "ERROR - 错误信息";
    ZERO_LOG_CRITICAL(g_logger) << "CRITICAL - 严重错误";
    ZERO_LOG_ALERT(g_logger)    << "ALERT - 必须立即采取行动";
    ZERO_LOG_FATAL(g_logger)    << "FATAL - 致命错误";
    ZERO_LOG_EMERGENCY(g_logger)<< "EMERGENCY - 系统不可用";
}

/**
 * @brief 2. 演示格式化输出 (printf风格)
 */
void demo_format() {
    printSection("2. Formatted Log Output");
    int request_id = 12345;
    double latency = 3.14159;
    const char* endpoint = "/api/v1/users";

    ZERO_LOG_FMT_INFO(g_logger, "Request #%d processed, latency=%.2fms, endpoint=%s",
                       request_id, latency, endpoint);

    for (int i = 0; i < 3; ++i) {
        ZERO_LOG_FMT_DEBUG(g_logger, "Processing batch %d/3", i + 1);
    }
}

/**
 * @brief 3. 演示多种格式化器
 */
void demo_formatters() {
    printSection("3. Multiple Formatters");

    // 默认格式
    auto fmt_default = zero::LogFormatters::Default();
    g_logger->setFormatter(fmt_default);
    ZERO_LOG_INFO(g_logger) << "Default formatter output";

    // 简单格式
    auto fmt_simple = zero::LogFormatters::Simple();
    g_logger->setFormatter(fmt_simple);
    ZERO_LOG_INFO(g_logger) << "Simple formatter output";

    // 详细格式
    auto fmt_verbose = zero::LogFormatters::Verbose();
    g_logger->setFormatter(fmt_verbose);
    ZERO_LOG_INFO(g_logger) << "Verbose formatter output";

    // 恢复默认格式
    g_logger->setFormatter(fmt_default);
}

/**
 * @brief 4. 演示结构化输出 (JSON/XML/LTSV/CEF)
 */
void demo_structured() {
    printSection("4. Structured Output Formats");

    // 创建不同的格式化器来演示
    auto json_fmt = zero::LogFormatters::Json();
    auto xml_fmt  = std::make_shared<zero::LogFormatter>("xml");
    auto ltsv_fmt = zero::LogFormatters::Ltsv();
    auto cef_fmt  = zero::LogFormatters::Cef();

    std::cout << "--- JSON Format ---" << std::endl;
    auto test_logger1 = ZERO_LOG_NAME("structured_test");
    test_logger1->setFormatter(json_fmt);
    ZERO_LOG_INFO(test_logger1) << "Structured JSON log message";

    std::cout << "--- XML Format ---" << std::endl;
    test_logger1->setFormatter(xml_fmt);
    ZERO_LOG_INFO(test_logger1) << "Structured XML log message";

    std::cout << "--- LTSV Format ---" << std::endl;
    test_logger1->setFormatter(ltsv_fmt);
    ZERO_LOG_INFO(test_logger1) << "Structured LTSV log message";

    std::cout << "--- CEF Format ---" << std::endl;
    test_logger1->setFormatter(cef_fmt);
    ZERO_LOG_INFO(test_logger1) << "Structured CEF log message";
}

/**
 * @brief 5. 演示文件Appender (同步+异步)
 */
void demo_file_appender() {
    printSection("5. File Appender (Sync + Async)");

    auto file_logger = ZERO_LOG_NAME("file_demo");

    // 同步文件Appender
    auto sync_appender = std::make_shared<zero::FileLogAppender>("logs/demo_sync.log");
    sync_appender->setLevel(zero::LogLevel::DEBUG);
    file_logger->addAppender(sync_appender);

    // 异步文件Appender
    auto file_app = std::make_shared<zero::FileLogAppender>("logs/demo_async.log");
    file_app->setLevel(zero::LogLevel::DEBUG);

    zero::AsyncLogConfig async_cfg;
    async_cfg.queue_size = 64 * 1024 * 1024;
    async_cfg.batch_size = 256;
    async_cfg.flush_interval_ms = 5;
    async_cfg.overflow_policy = zero::AsyncOverflowPolicy::BLOCK;

    auto async_appender = std::make_shared<zero::AsyncAppenderWrapper>(file_app, async_cfg);
    file_logger->addAppender(async_appender);

    for (int i = 0; i < 5; ++i) {
        ZERO_LOG_INFO(file_logger) << "File appender test message #" << (i + 1)
                                     << " (both sync and async)";
    }

    // 等待异步写入完成
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    async_appender->flush();
    std::cout << "File logs written to logs/demo_sync.log and logs/demo_async.log" << std::endl;
}

/**
 * @brief 6. 演示滚动文件Appender
 */
void demo_rotating_appender() {
    printSection("6. Rotating File Appender");

    auto rot_logger = ZERO_LOG_NAME("rotating_demo");

    // 大小滚动
    auto rot_app = std::make_shared<zero::RotatingFileLogAppender>(
        "logs/demo_rotating.log",
        1024 * 1024,  // 1MB
        3,             // 保留3个备份
        false          // 不压缩
    );
    rot_app->setFormatter(zero::LogFormatters::Default());
    rot_logger->addAppender(rot_app);

    for (int i = 0; i < 5; ++i) {
        ZERO_LOG_INFO(rot_logger) << "Rotating file test #" << (i + 1);
    }
    std::cout << "Rotating file logs written to logs/demo_rotating.log" << std::endl;
}

/**
 * @brief 7. 演示MDC/NDC上下文
 */
void demo_context() {
    printSection("7. MDC / NDC Diagnostic Context");

    auto ctx_logger = ZERO_LOG_NAME("context_demo");

    // 设置MDC
    ZERO_LOG_MDC("request_id", "req-abc-123");
    ZERO_LOG_MDC("user_id", "user-456");
    ZERO_LOG_MDC("session_id", "sess-xyz-789");

    // 使用NDC
    {
        ZERO_LOG_NDC_SCOPE("handle_request");
        ZERO_LOG_INFO(ctx_logger) << "Entering request handler";

        {
            ZERO_LOG_NDC_SCOPE("validate_params");
            ZERO_LOG_INFO(ctx_logger) << "Validating request parameters";
        }

        {
            ZERO_LOG_NDC_SCOPE("query_database");
            ZERO_LOG_INFO(ctx_logger) << "Executing database query";
        }

        ZERO_LOG_INFO(ctx_logger) << "Request handling complete";
    }

    // 清理MDC
    ZERO_LOG_MDC_REMOVE("request_id");
    ZERO_LOG_MDC_REMOVE("user_id");
    ZERO_LOG_MDC_REMOVE("session_id");
}

/**
 * @brief 8. 演示Trace/Span分布式追踪
 */
void demo_tracing() {
    printSection("8. Trace / Span Distributed Tracing");

    auto trace_logger = ZERO_LOG_NAME("trace_demo");
    auto fmt = zero::LogFormatters::Verbose();  // Verbose格式包含Trace/Span ID
    trace_logger->setFormatter(fmt);

    // 创建Trace上下文
    zero::TraceContext trace_ctx;
    zero::TraceContext::setCurrent(&trace_ctx);

    ZERO_LOG_INFO(trace_logger) << "Root span started";

    {
        ZERO_LOG_SPAN_SCOPE("sub_operation_1");
        ZERO_LOG_INFO(trace_logger) << "Sub-operation 1 executing";
    }

    {
        ZERO_LOG_SPAN_SCOPE("sub_operation_2");
        ZERO_LOG_INFO(trace_logger) << "Sub-operation 2 executing";
    }

    ZERO_LOG_INFO(trace_logger) << "Root span ended";
    zero::TraceContext::setCurrent(nullptr);
}

/**
 * @brief 9. 演示日志过滤器
 */
void demo_filters() {
    printSection("9. Log Filters");

    auto filter_logger = ZERO_LOG_NAME("filter_demo");

    // 限速过滤器 - 每秒最多10条
    auto rate_filter = std::make_shared<zero::RateLimitFilter>(10);
    filter_logger->addFilter(rate_filter);

    // 写入20条日志，但只有10条会通过
    std::cout << "Writing 20 messages with rate limit (10/sec), should see ~10:" << std::endl;
    for (int i = 0; i < 20; ++i) {
        ZERO_LOG_INFO(filter_logger) << "Rate limited message #" << (i + 1);
    }

    std::cout << "Accepted: " << rate_filter->getAcceptedCount()
              << ", Denied: " << rate_filter->getDeniedCount() << std::endl;

    filter_logger->clearFilters();
}

/**
 * @brief 10. 演示日志统计
 */
void demo_stats() {
    printSection("10. Log Statistics");

    auto stats_logger = ZERO_LOG_NAME("stats_demo");

    // 写入一些日志让统计有意义
    for (int i = 0; i < 100; ++i) {
        ZERO_LOG_DEBUG(stats_logger) << "Stats test DEBUG message #" << i;
        if (i % 10 == 0) ZERO_LOG_INFO(stats_logger) << "Stats test INFO message #" << i;
        if (i % 30 == 0) ZERO_LOG_WARN(stats_logger) << "Stats test WARN message #" << i;
        if (i % 50 == 0) ZERO_LOG_ERROR(stats_logger) << "Stats test ERROR message #" << i;
    }

    // 打印统计报告
    std::cout << zero::LogStatsManager::GetInstance().getReport() << std::endl;
}

/**
 * @brief 11. 演示彩色输出
 */
void demo_colors() {
    printSection("11. Terminal Color Output");

    auto color_logger = ZERO_LOG_NAME("color_demo");

    // 创建带颜色的Appender
    auto color_app = std::make_shared<zero::ColorStdoutLogAppender>();
    color_app->setLevel(zero::LogLevel::TRACE);
    color_logger->clearAppenders();
    color_logger->addAppender(color_app);

    // 打印所有级别 - 检查是否在终端中
    if (isatty(STDOUT_FILENO)) {
        std::cout << "Terminal detected - showing colored output:" << std::endl;
    } else {
        std::cout << "Not a terminal - colors may not display correctly:" << std::endl;
    }

    ZERO_LOG_TRACE(color_logger)    << "TRACE - Grey/Dim";
    ZERO_LOG_DEBUG(color_logger)    << "DEBUG - Cyan";
    ZERO_LOG_INFO(color_logger)     << "INFO - Green";
    ZERO_LOG_NOTICE(color_logger)   << "NOTICE - Blue";
    ZERO_LOG_WARN(color_logger)     << "WARN - Yellow Bold";
    ZERO_LOG_ERROR(color_logger)    << "ERROR - Red Bold";
    ZERO_LOG_CRITICAL(color_logger) << "CRITICAL - Magenta Bold";
    ZERO_LOG_ALERT(color_logger)    << "ALERT - White on Red";
    ZERO_LOG_FATAL(color_logger)    << "FATAL - Red Bold";
    ZERO_LOG_EMERGENCY(color_logger)<< "EMERGENCY - White on Red Bold";

    // 恢复root logger的appender
    color_logger->clearAppenders();
}

/**
 * @brief 12. 演示内存Appender
 */
void demo_memory_appender() {
    printSection("12. In-Memory Log Buffer");

    auto mem_logger = ZERO_LOG_NAME("memory_demo");
    mem_logger->clearAppenders();

    auto mem_app = std::make_shared<zero::MemoryLogAppender>(1000);
    mem_app->setLevel(zero::LogLevel::DEBUG);
    mem_logger->addAppender(mem_app);

    for (int i = 0; i < 50; ++i) {
        ZERO_LOG_INFO(mem_logger) << "Memory log message #" << i;
        if (i % 7 == 0) ZERO_LOG_WARN(mem_logger) << "Memory warning #" << i;
        if (i % 13 == 0) ZERO_LOG_ERROR(mem_logger) << "Memory error #" << i;
    }

    // 读取缓存
    std::cout << "Total cached logs: " << mem_app->size() << std::endl;
    std::cout << "--- Recent 10 logs ---" << std::endl;
    auto recent = mem_app->getRecent(10);
    for (auto& entry : recent) {
        std::cout << "  [" << zero::LogLevel::ToString(entry.level)
                  << "] " << entry.message << std::endl;
    }

    // 按级别过滤
    auto errors = mem_app->getByLevel(zero::LogLevel::WARN);
    std::cout << "--- WARN+ logs (" << errors.size() << " total) ---" << std::endl;
    for (auto& entry : errors) {
        std::cout << "  [" << zero::LogLevel::ToString(entry.level)
                  << "] " << entry.message << std::endl;
    }
}

/**
 * @brief 13. 演示YAML配置导出
 */
void demo_config_export() {
    printSection("13. Configuration Export (YAML)");

    std::string yaml = zero::LoggerMgr::GetInstance()->toYamlString();
    std::cout << "Current logger configuration:" << std::endl;
    std::cout << yaml << std::endl;
}

/**
 * @brief 14. 演示快速配置
 */
void demo_quick_config() {
    printSection("14. Quick Configuration API");

    // 编程方式配置Logger
    auto api_logger = ZERO_LOG_NAME("api_config_demo");
    api_logger->clearAppenders();
    api_logger->setLevel(zero::LogLevel::INFO);

    // 使用便利函数创建格式化器
    api_logger->setFormatter(zero::LogFormatters::Development());

    // 添加异步文件Appender
    auto file_app = std::make_shared<zero::FileLogAppender>("logs/api_demo.log");

    zero::AsyncLogConfig cfg;
    cfg.queue_size = 128 * 1024 * 1024;  // 128MB队列
    cfg.batch_size = 512;                // 批量512条
    cfg.flush_interval_ms = 10;          // 10ms刷新

    auto async_app = std::make_shared<zero::AsyncAppenderWrapper>(file_app, cfg);
    api_logger->addAppender(async_app);

    ZERO_LOG_INFO(api_logger) << "Configured programmatically with 128MB async queue";
    ZERO_LOG_INFO(api_logger) << "Batch size: 512, Flush interval: 10ms";

    std::cout << "Async config: queue=" << cfg.queue_size
              << " batch=" << cfg.batch_size
              << " flush_interval=" << cfg.flush_interval_ms << "ms" << std::endl;
}

int main(int argc, char** argv) {
    // 初始化环境
    zero::EnvMgr::GetInstance()->init(argc, argv);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║          Log System Comprehensive Demo                   ║\n";
    std::cout << "║          Features: 10 Levels, 15+ Appenders               ║\n";
    std::cout << "║          Sync/Async Modes, 30+ Formatters                  ║\n";
    std::cout << "║          MDC/NDC/Trace Context, Filters, Stats            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    demo_levels();
    demo_format();
    demo_formatters();
    demo_structured();
    demo_file_appender();
    demo_rotating_appender();
    demo_context();
    demo_tracing();
    demo_filters();
    demo_stats();
    demo_colors();
    demo_memory_appender();
    demo_config_export();
    demo_quick_config();

    printSection("Demo Complete!");
    std::cout << "Check logs/ directory for output files." << std::endl;
    std::cout << "Total logs written: "
              << zero::LogStatsManager::GetInstance().getCounters().total_logs.load()
              << std::endl;

    return 0;
}

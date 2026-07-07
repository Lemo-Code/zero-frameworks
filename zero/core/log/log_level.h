/**
 * @file log_level.h
 * @brief 扩展的日志级别定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_LEVEL_H__
#define __ZERO_LOG_LEVEL_H__

#include <string>
#include <stdint.h>

namespace zero {

/**
 * @brief 日志级别类，兼容原有枚举设计并扩展更多级别
 * @details 级别值设计为兼容原有代码:
 *          UNKNOW=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, FATAL=5 (原有)
 *          新增: TRACE=10, NOTICE=20, CRITICAL=40, ALERT=45, EMERGENCY=50
 *          级别值留有间隔，便于未来插入
 */
class LogLevel {
public:
    /**
     * @brief 日志级别枚举
     */
    enum Level {
        /// 未知级别
        UNKNOW    = 0,
        /// TRACE 级别 - 最详细的追踪信息
        TRACE     = 1,
        /// DEBUG 级别 - 调试信息
        DEBUG     = 5,
        /// INFO 级别 - 一般信息
        INFO      = 10,
        /// NOTICE 级别 - 需要注意的普通但重要的条件
        NOTICE    = 15,
        /// WARN 级别 - 警告信息
        WARN      = 20,
        /// ERROR 级别 - 错误信息
        ERROR     = 30,
        /// CRITICAL 级别 - 严重错误
        CRITICAL  = 40,
        /// ALERT 级别 - 必须立即采取行动
        ALERT     = 45,
        /// FATAL 级别 - 致命错误，系统可能无法继续运行
        FATAL     = 50,
        /// EMERGENCY 级别 - 系统不可用
        EMERGENCY = 55
    };

    /**
     * @brief 将日志级别转成文本输出
     * @param[in] level 日志级别
     */
    static const char* ToString(LogLevel::Level level);

    /**
     * @brief 将日志级别转成简短文本（用于紧凑输出）
     * @param[in] level 日志级别
     */
    static const char* ToShortString(LogLevel::Level level);

    /**
     * @brief 将文本转换成日志级别
     * @param[in] str 日志级别文本 (不区分大小写)
     */
    static LogLevel::Level FromString(const std::string& str);

    /**
     * @brief 返回syslog优先级映射
     * @param[in] level 日志级别
     */
    static int ToSyslogPriority(LogLevel::Level level);

    /**
     * @brief 返回ANSI颜色码
     * @param[in] level 日志级别
     */
    static const char* GetColorCode(LogLevel::Level level);

    /**
     * @brief 获取级别名称的最大长度
     */
    static int GetMaxLevelNameLength() { return 9; }  // EMERGENCY
};

/**
 * @brief 日志级别配置描述符
 */
struct LogLevelConfig {
    LogLevel::Level level = LogLevel::DEBUG;
    bool enabled = true;
    int rate_limit_per_sec = 0;  // 0表示不限速
    int sample_rate = 100;       // 采样率 1-100
};

} // namespace zero

#endif // __ZERO_LOG_LEVEL_H__

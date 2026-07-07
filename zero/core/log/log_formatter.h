/**
 * @file log_formatter.h
 * @brief 扩展的日志格式化器
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_FORMATTER_H__
#define __ZERO_LOG_FORMATTER_H__

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <ostream>
#include "log_level.h"
#include "log_event.h"

namespace zero {

class Logger;

/**
 * @brief 日志格式化器
 */
class LogFormatter {
public:
    typedef std::shared_ptr<LogFormatter> ptr;

    /**
     * @brief 输出格式枚举
     */
    enum class OutputFormat {
        PATTERN = 0,  /// 模式字符串
        JSON    = 1,  /// JSON格式
        XML     = 2,  /// XML格式
        LTSV    = 3,  /// LTSV (Labeled Tab-separated Values)
        CEF     = 4   /// CEF (Common Event Format) - 兼容SIEM
    };

    /**
     * @brief 构造函数
     * @param[in] pattern 格式模板，或"json"/"xml"/"ltsv"/"cef"以使用结构化格式
     * @details
     *  模式字符串支持的格式说明符:
     *  %d{format}  日期时间, format 为 strftime 格式，默认 "%Y-%m-%d %H:%M:%S"
     *  %D{format}  微秒级日期时间
     *  %t          线程id (十进制)
     *  %N          线程名称
     *  %F          协程id
     *  %p          日志级别 (完整名称)
     *  %P          日志级别 (缩写)
     *  %c          Logger名称
     *  %f          源文件名
     *  %l          源文件行号
     *  %m          日志消息体
     *  %n          换行符
     *  %T          制表符
     *  %r          启动后经过的毫秒数
     *  %R          启动后经过的微秒数
     *  %C          函数名
     *  %i          进程ID (PID)
     *  %h          主机名
     *  %a          应用名称
     *  %x{key}     MDC指定键的值
     *  %X          完整MDC (key=value格式)
     *  %J          MDC的JSON格式
     *  %e          NDC完整栈
     *  %E          NDC深度
     *  %S          Trace ID
     *  %s          Span ID
     *  %k{key}     用户标签值
     *  %K          所有标签 (JSON)
     *  %Y          日志级别对应的ANSI颜色开始
     *  %y          ANSI颜色结束(重置)
     *  %b          纯文本模式开始
     *  %B{key}     自定义扩展值
     *  %%          百分号本身
     *
     *  预定义快捷格式:
     *  "json"  -> JSON结构化输出
     *  "xml"   -> XML结构化输出
     *  "ltsv"  -> LTSV输出
     *  "cef"   -> CEF输出
     */
    LogFormatter(const std::string& pattern = "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n");

    /**
     * @brief 格式化日志到字符串
     */
    std::string format(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 格式化日志到输出流 (更高效，避免中间字符串)
     */
    std::ostream& format(std::ostream& ofs, std::shared_ptr<Logger> logger,
                          LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 格式化日志为JSON字符串
     */
    std::string formatJson(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 格式化日志为XML字符串
     */
    std::string formatXml(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 格式化日志为LTSV字符串
     */
    std::string formatLtsv(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 格式化日志为CEF字符串
     */
    std::string formatCef(std::shared_ptr<Logger> logger, LogLevel::Level level, LogEvent::ptr event);

    /**
     * @brief 设置输出格式
     */
    void setOutputFormat(OutputFormat fmt) { m_output_format = fmt; }

    /**
     * @brief 获取当前输出格式
     */
    OutputFormat getOutputFormat() const { return m_output_format; }

    /**
     * @brief 是否有解析错误
     */
    bool isError() const { return m_error; }

    /**
     * @brief 获取格式模板
     */
    const std::string& getPattern() const { return m_pattern; }

    /**
     * @brief 设置格式模板
     */
    void setPattern(const std::string& pattern);

public:
    /**
     * @brief 日志格式项(抽象基类)
     */
    class FormatItem {
    public:
        typedef std::shared_ptr<FormatItem> ptr;
        virtual ~FormatItem() {}
        /**
         * @brief 格式化到输出流
         */
        virtual void format(std::ostream& os, std::shared_ptr<Logger> logger,
                            LogLevel::Level level, LogEvent::ptr event) = 0;

        /**
         * @brief 格式化到字符串
         */
        virtual std::string format(std::shared_ptr<Logger> logger,
                                   LogLevel::Level level, LogEvent::ptr event) {
            return "";
        }
    };

    /**
     * @brief 初始化 - 解析模式字符串
     */
    void init();

    /**
     * @brief 注册自定义格式化项
     */
    void registerFormatItem(const std::string& key,
                            std::function<FormatItem::ptr(const std::string&)> factory);

private:
    /// 格式模板字符串
    std::string m_pattern;
    /// 解析后的格式项列表
    std::vector<FormatItem::ptr> m_items;
    /// 是否有解析错误
    bool m_error = false;
    /// 输出格式
    OutputFormat m_output_format = OutputFormat::PATTERN;
};

/**
 * @brief 格式项工厂 - 管理所有格式说明符
 */
class FormatItemFactory {
public:
    typedef std::function<LogFormatter::FormatItem::ptr(const std::string& fmt)> Creator;

    /**
     * @brief 获取单例
     */
    static FormatItemFactory& GetInstance();

    /**
     * @brief 注册格式项创建器
     */
    void registerCreator(const std::string& key, Creator creator);

    /**
     * @brief 创建格式项
     */
    LogFormatter::FormatItem::ptr create(const std::string& key, const std::string& fmt);

    /**
     * @brief 检查格式项是否存在
     */
    bool hasCreator(const std::string& key) const;

    /**
     * @brief 获取所有已注册的格式键
     */
    std::vector<std::string> getRegisteredKeys() const;

private:
    FormatItemFactory();
    std::map<std::string, Creator> m_creators;
};

// ======================== 便捷函数 ========================

/**
 * @brief 获取预定义的常用格式化器
 */
namespace LogFormatters {
    /// 默认格式
    LogFormatter::ptr Default();
    /// 简单格式 (只有时间和消息)
    LogFormatter::ptr Simple();
    /// 详细格式 (包含所有上下文)
    LogFormatter::ptr Verbose();
    /// JSON格式
    LogFormatter::ptr Json();
    /// 生产环境格式 (精简)
    LogFormatter::ptr Production();
    /// 开发环境格式 (带颜色和源码位置)
    LogFormatter::ptr Development();
    /// Syslog格式
    LogFormatter::ptr Syslog();
    /// CEF格式
    LogFormatter::ptr Cef();
    /// LTSV格式
    LogFormatter::ptr Ltsv();
}

} // namespace zero

#endif // __ZERO_LOG_FORMATTER_H__

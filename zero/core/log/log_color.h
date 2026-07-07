/**
 * @file log_color.h
 * @brief 终端彩色输出支持
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_LOG_COLOR_H__
#define __ZERO_LOG_COLOR_H__

#include <string>
#include <map>
#include <memory>
#include <stdint.h>
#include "log_level.h"
#include "zero/core/concurrency/mutex.h"

namespace zero {

/**
 * @brief ANSI颜色属性定义
 */
struct ColorAttribute {
    // ANSI基本色 (0-7)
    enum BasicColor {
        BLACK   = 0,
        RED     = 1,
        GREEN   = 2,
        YELLOW  = 3,
        BLUE    = 4,
        MAGENTA = 5,
        CYAN    = 6,
        WHITE   = 7,
        DEFAULT = 9
    };

    // 文字样式
    enum TextStyle {
        NORMAL      = 0,
        BOLD        = 1,
        DIM         = 2,
        ITALIC      = 3,
        UNDERLINE   = 4,
        BLINK       = 5,
        RAPID_BLINK = 6,
        REVERSE     = 7,
        HIDDEN      = 8,
        STRIKE      = 9
    };

    int foreground = DEFAULT;     // 前景色 (-1: 默认, 0-7: 基本色, 8-255: 256色)
    int background = DEFAULT;     // 背景色
    int text_style = NORMAL;      // 文字样式

    /// 是否使用256色模式
    bool use_256_color = false;

    /// True Color RGB值 (仅在use_true_color为true时有效)
    bool use_true_color = false;
    uint8_t fg_r = 255, fg_g = 255, fg_b = 255;
    uint8_t bg_r = 0,   bg_g = 0,   bg_b = 0;

    /**
     * @brief 生成ANSI转义序列
     */
    std::string toEscapeSequence() const;

    /**
     * @brief 获取重置序列
     */
    static std::string ResetSequence() { return "\033[0m"; }
};

/**
 * @brief 彩色输出配置接口
 */
class LogColorConfig {
public:
    typedef std::shared_ptr<LogColorConfig> ptr;

    virtual ~LogColorConfig() {}

    /**
     * @brief 获取指定级别的颜色属性
     */
    virtual ColorAttribute getColor(LogLevel::Level level) const = 0;

    /**
     * @brief 获取指定级别的ANSI开始序列
     */
    std::string getColorBegin(LogLevel::Level level) const;

    /**
     * @brief 获取ANSI重置序列
     */
    std::string getColorEnd() const;

    /**
     * @brief 包装彩色文本
     */
    std::string colorize(const std::string& text, LogLevel::Level level) const;
};

/**
 * @brief 预定义配色方案: Default
 * @details 兼容原LogLevel::GetColorCode的配色
 */
class DefaultColorConfig : public LogColorConfig {
public:
    typedef std::shared_ptr<DefaultColorConfig> ptr;

    DefaultColorConfig();

    ColorAttribute getColor(LogLevel::Level level) const override;

    /**
     * @brief 设置指定级别的颜色
     */
    void setColor(LogLevel::Level level, const ColorAttribute& attr);

private:
    std::map<LogLevel::Level, ColorAttribute> m_color_map;
};

/**
 * @brief 预定义配色方案: Monokai (暗色主题)
 */
class MonokaiColorConfig : public LogColorConfig {
public:
    typedef std::shared_ptr<MonokaiColorConfig> ptr;

    MonokaiColorConfig();
    ColorAttribute getColor(LogLevel::Level level) const override;

private:
    std::map<LogLevel::Level, ColorAttribute> m_color_map;
};

/**
 * @brief 预定义配色方案: Solarized
 */
class SolarizedColorConfig : public LogColorConfig {
public:
    typedef std::shared_ptr<SolarizedColorConfig> ptr;

    SolarizedColorConfig();
    ColorAttribute getColor(LogLevel::Level level) const override;

private:
    std::map<LogLevel::Level, ColorAttribute> m_color_map;
};

/**
 * @brief 自定义配色方案 (可完全配置)
 */
class CustomColorConfig : public LogColorConfig {
public:
    typedef std::shared_ptr<CustomColorConfig> ptr;

    CustomColorConfig();

    ColorAttribute getColor(LogLevel::Level level) const override;

    /**
     * @brief 设置级别配色
     */
    void setColor(LogLevel::Level level, const ColorAttribute& attr);
    void setForeground(LogLevel::Level level, int fg);
    void setBackground(LogLevel::Level level, int bg);
    void setTextStyle(LogLevel::Level level, int style);

    /**
     * @brief 从配置字符串解析颜色
     * @details 格式: "level:fg,bg,style;level:fg,bg,style"
     *          例如: "DEBUG:cyan,default,bold;ERROR:red,default,bold;WARN:yellow"
     */
    bool parseFromString(const std::string& config_str);

private:
    std::map<LogLevel::Level, ColorAttribute> m_color_map;
};

/**
 * @brief 颜色管理器 (单例)
 */
class ColorManager {
public:
    typedef Spinlock MutexType;

    /**
     * @brief 获取当前配色方案
     */
    LogColorConfig::ptr getConfig() const;

    /**
     * @brief 设置配色方案
     */
    void setConfig(LogColorConfig::ptr config);

    /**
     * @brief 按名称设置配色方案 (default/monokai/solarized/custom)
     */
    bool setConfigByName(const std::string& name);

    /**
     * @brief 获取单例
     */
    static ColorManager& GetInstance();

private:
    ColorManager();
    mutable MutexType m_mutex;
    LogColorConfig::ptr m_config;
};

/**
 * @brief 便捷函数: 获取带颜色的文本
 */
std::string ColorizeText(const std::string& text, LogLevel::Level level);
std::string ColorizeBegin(LogLevel::Level level);
std::string ColorizeEnd();

} // namespace zero

#endif // __ZERO_LOG_COLOR_H__

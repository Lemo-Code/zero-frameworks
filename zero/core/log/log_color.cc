/**
 * @file log_color.cc
 * @brief 终端彩色输出实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_color.h"
#include <sstream>
#include <string.h>
#include <algorithm>

namespace zero {

// ======================== ColorAttribute ========================

std::string ColorAttribute::toEscapeSequence() const {
    std::stringstream ss;
    ss << "\033[";

    if (use_true_color) {
        // True Color (24-bit): \033[38;2;R;G;Bm \033[48;2;R;G;Bm
        ss << text_style << ";";
        ss << "38;2;" << (int)fg_r << ";" << (int)fg_g << ";" << (int)fg_b << ";";
        ss << "48;2;" << (int)bg_r << ";" << (int)bg_g << ";" << (int)bg_b;
    } else if (use_256_color) {
        // 256 color: \033[38;5;Nm \033[48;5;Nm
        ss << text_style << ";";
        ss << "38;5;" << foreground << ";";
        ss << "48;5;" << background;
    } else {
        // 基本8色: \033[style;fg+30;bg+40m
        ss << text_style << ";";
        if (foreground >= 0) {
            ss << (30 + foreground);
        }
        if (background >= 0) {
            ss << ";" << (40 + background);
        }
    }
    ss << "m";
    return ss.str();
}

// ======================== LogColorConfig base ========================

std::string LogColorConfig::getColorBegin(LogLevel::Level level) const {
    return getColor(level).toEscapeSequence();
}

std::string LogColorConfig::getColorEnd() const {
    return ColorAttribute::ResetSequence();
}

std::string LogColorConfig::colorize(const std::string& text, LogLevel::Level level) const {
    return getColorBegin(level) + text + getColorEnd();
}

// ======================== DefaultColorConfig ========================

DefaultColorConfig::DefaultColorConfig() {
    // TRACE - 灰色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::BLACK;
        attr.text_style = ColorAttribute::DIM;
        m_color_map[LogLevel::TRACE] = attr;
    }
    // DEBUG - 青色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::CYAN;
        m_color_map[LogLevel::DEBUG] = attr;
    }
    // INFO - 绿色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::GREEN;
        m_color_map[LogLevel::INFO] = attr;
    }
    // NOTICE - 蓝色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::BLUE;
        m_color_map[LogLevel::NOTICE] = attr;
    }
    // WARN - 黄色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::YELLOW;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::WARN] = attr;
    }
    // ERROR - 红色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::RED;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::ERROR] = attr;
    }
    // CRITICAL - 品红
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::MAGENTA;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::CRITICAL] = attr;
    }
    // ALERT - 红色背景
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::WHITE;
        attr.background = ColorAttribute::RED;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::ALERT] = attr;
    }
    // FATAL - 粗体红色
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::RED;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::FATAL] = attr;
    }
    // EMERGENCY - 白字红底粗体
    {
        ColorAttribute attr;
        attr.foreground = ColorAttribute::WHITE;
        attr.background = ColorAttribute::RED;
        attr.text_style = ColorAttribute::BOLD;
        m_color_map[LogLevel::EMERGENCY] = attr;
    }
}

ColorAttribute DefaultColorConfig::getColor(LogLevel::Level level) const {
    auto it = m_color_map.find(level);
    if (it != m_color_map.end()) {
        return it->second;
    }
    return ColorAttribute();
}

void DefaultColorConfig::setColor(LogLevel::Level level, const ColorAttribute& attr) {
    m_color_map[level] = attr;
}

// ======================== MonokaiColorConfig ========================

MonokaiColorConfig::MonokaiColorConfig() {
    auto set = [this](LogLevel::Level lv, int fg, int style = 0) {
        ColorAttribute attr;
        attr.foreground = fg;
        attr.text_style = style;
        attr.use_256_color = true;
        m_color_map[lv] = attr;
    };
    // Monokai配色: 使用256色调色板
    set(LogLevel::TRACE,     245);  // 浅灰
    set(LogLevel::DEBUG,     81);   // 亮青
    set(LogLevel::INFO,      118);  // 亮绿
    set(LogLevel::NOTICE,    75);   // 亮蓝
    set(LogLevel::WARN,      228, ColorAttribute::BOLD);  // 金黄
    set(LogLevel::ERROR,     197, ColorAttribute::BOLD);  // 亮红
    set(LogLevel::CRITICAL,  201, ColorAttribute::BOLD);  // 品红
    set(LogLevel::ALERT,     15, ColorAttribute::BOLD);   // 白
    set(LogLevel::FATAL,     196, ColorAttribute::BOLD);  // 纯红
    set(LogLevel::EMERGENCY, 226, ColorAttribute::BOLD);  // 亮黄
}

ColorAttribute MonokaiColorConfig::getColor(LogLevel::Level level) const {
    auto it = m_color_map.find(level);
    return (it != m_color_map.end()) ? it->second : ColorAttribute();
}

// ======================== SolarizedColorConfig ========================

SolarizedColorConfig::SolarizedColorConfig() {
    auto set = [this](LogLevel::Level lv, int fg, int style = 0) {
        ColorAttribute attr;
        attr.foreground = fg;
        attr.text_style = style;
        attr.use_256_color = true;
        m_color_map[lv] = attr;
    };
    // Solarized配色
    set(LogLevel::TRACE,     102);  // base0
    set(LogLevel::DEBUG,     37);   // cyan
    set(LogLevel::INFO,      64);   // green
    set(LogLevel::NOTICE,    61);   // blue
    set(LogLevel::WARN,      136, ColorAttribute::BOLD);  // yellow
    set(LogLevel::ERROR,     160, ColorAttribute::BOLD);  // red
    set(LogLevel::CRITICAL,  125, ColorAttribute::BOLD);  // magenta
    set(LogLevel::ALERT,     166, ColorAttribute::BOLD);  // orange
    set(LogLevel::FATAL,     124, ColorAttribute::BOLD);  // red bold
    set(LogLevel::EMERGENCY, 52,  ColorAttribute::BOLD);  // dark red
}

ColorAttribute SolarizedColorConfig::getColor(LogLevel::Level level) const {
    auto it = m_color_map.find(level);
    return (it != m_color_map.end()) ? it->second : ColorAttribute();
}

// ======================== CustomColorConfig ========================

CustomColorConfig::CustomColorConfig() {
    // 初始化为默认配置
    DefaultColorConfig def;
    for (int i = LogLevel::TRACE; i <= LogLevel::EMERGENCY; ++i) {
        // 跳过间隔中的无效级别
        if (i == 2 || i == 3 || i == 4 || i == 6 || i == 7 || i == 8 || i == 9 ||
            i == 11 || i == 12 || i == 13 || i == 14 || i == 16 || i == 17 || i == 18 || i == 19 ||
            i == 21 || i == 22 || i == 23 || i == 24 || i == 25 || i == 26 || i == 27 || i == 28 || i == 29 ||
            i == 31 || i == 32 || i == 33 || i == 34 || i == 35 || i == 36 || i == 37 || i == 38 || i == 39 ||
            i == 41 || i == 42 || i == 43 || i == 44 || i == 46 || i == 47 || i == 48 || i == 49 ||
            i == 51 || i == 52 || i == 53 || i == 54) {
            continue;
        }
        m_color_map[(LogLevel::Level)i] = def.getColor((LogLevel::Level)i);
    }
}

ColorAttribute CustomColorConfig::getColor(LogLevel::Level level) const {
    auto it = m_color_map.find(level);
    return (it != m_color_map.end()) ? it->second : ColorAttribute();
}

void CustomColorConfig::setColor(LogLevel::Level level, const ColorAttribute& attr) {
    m_color_map[level] = attr;
}

void CustomColorConfig::setForeground(LogLevel::Level level, int fg) {
    m_color_map[level].foreground = fg;
}

void CustomColorConfig::setBackground(LogLevel::Level level, int bg) {
    m_color_map[level].background = bg;
}

void CustomColorConfig::setTextStyle(LogLevel::Level level, int style) {
    m_color_map[level].text_style = style;
}

bool CustomColorConfig::parseFromString(const std::string& config_str) {
    // 格式: LEVEL:fg,bg,style[;LEVEL:fg,bg,style]...
    // 例如: "DEBUG:cyan,default,bold;ERROR:red,default,bold"
    // 简化实现：解析分号分隔的条目
    std::string str = config_str;
    size_t pos = 0;
    while (pos < str.size()) {
        size_t semi = str.find(';', pos);
        std::string entry = (semi == std::string::npos)
                          ? str.substr(pos)
                          : str.substr(pos, semi - pos);

        // 解析 LEVEL:fg,bg,style
        size_t colon = entry.find(':');
        if (colon == std::string::npos) {
            pos = (semi == std::string::npos) ? str.size() : semi + 1;
            continue;
        }

        std::string level_name = entry.substr(0, colon);
        (void)LogLevel::FromString(level_name);

        std::string attrs = entry.substr(colon + 1);
        // 简单解析逗号分隔的属性，这里省略完整实现
        // 实际使用时可通过更完整的解析器处理

        pos = (semi == std::string::npos) ? str.size() : semi + 1;
    }
    return true;
}

// ======================== ColorManager ========================

ColorManager& ColorManager::GetInstance() {
    static ColorManager instance;
    return instance;
}

ColorManager::ColorManager() {
    m_config.reset(new DefaultColorConfig());
}

LogColorConfig::ptr ColorManager::getConfig() const {
    MutexType::Lock lock(m_mutex);
    return m_config;
}

void ColorManager::setConfig(LogColorConfig::ptr config) {
    MutexType::Lock lock(m_mutex);
    if (config) {
        m_config = config;
    }
}

bool ColorManager::setConfigByName(const std::string& name) {
    if (name == "default") {
        setConfig(std::make_shared<DefaultColorConfig>());
    } else if (name == "monokai") {
        setConfig(std::make_shared<MonokaiColorConfig>());
    } else if (name == "solarized") {
        setConfig(std::make_shared<SolarizedColorConfig>());
    } else if (name == "custom") {
        setConfig(std::make_shared<CustomColorConfig>());
    } else {
        return false;
    }
    return true;
}

// ======================== 便捷函数 ========================

std::string ColorizeText(const std::string& text, LogLevel::Level level) {
    return ColorManager::GetInstance().getConfig()->colorize(text, level);
}

std::string ColorizeBegin(LogLevel::Level level) {
    return ColorManager::GetInstance().getConfig()->getColorBegin(level);
}

std::string ColorizeEnd() {
    return ColorAttribute::ResetSequence();
}

} // namespace zero

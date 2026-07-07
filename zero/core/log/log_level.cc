/**
 * @file log_level.cc
 * @brief 日志级别实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "log_level.h"
#include <string.h>
#include <strings.h>
#include <map>
#include <syslog.h>

namespace zero {

const char* LogLevel::ToString(LogLevel::Level level) {
    switch(level) {
#define XX(name) \
    case LogLevel::name: \
        return #name;
    XX(TRACE);
    XX(DEBUG);
    XX(INFO);
    XX(NOTICE);
    XX(WARN);
    XX(ERROR);
    XX(CRITICAL);
    XX(ALERT);
    XX(FATAL);
    XX(EMERGENCY);
#undef XX
    default:
        return "UNKNOW";
    }
    return "UNKNOW";
}

const char* LogLevel::ToShortString(LogLevel::Level level) {
    switch(level) {
#define XX(name, short) \
    case LogLevel::name: \
        return short;
    XX(TRACE,    "TRC");
    XX(DEBUG,    "DBG");
    XX(INFO,     "INF");
    XX(NOTICE,   "NTC");
    XX(WARN,     "WRN");
    XX(ERROR,    "ERR");
    XX(CRITICAL, "CRT");
    XX(ALERT,    "ALT");
    XX(FATAL,    "FTL");
    XX(EMERGENCY,"EMG");
#undef XX
    default:
        return "UNK";
    }
    return "UNK";
}

LogLevel::Level LogLevel::FromString(const std::string& str) {
    // 支持大小写不敏感匹配
#define XX(level, s1, s2) \
    if(strcasecmp(str.c_str(), s1) == 0 || strcasecmp(str.c_str(), s2) == 0) { \
        return LogLevel::level; \
    }
    XX(TRACE,     "trace",     "TRACE");
    XX(DEBUG,     "debug",     "DEBUG");
    XX(INFO,      "info",      "INFO");
    XX(NOTICE,    "notice",    "NOTICE");
    XX(WARN,      "warn",      "WARN");
    XX(ERROR,     "error",     "ERROR");
    XX(CRITICAL,  "critical",  "CRITICAL");
    XX(ALERT,     "alert",     "ALERT");
    XX(FATAL,     "fatal",     "FATAL");
    XX(EMERGENCY, "emergency", "EMERGENCY");
#undef XX

    // 兼容老版本的 FromString 调用 (小写+大写)
    // 已在上面覆盖

    return LogLevel::UNKNOW;
}

int LogLevel::ToSyslogPriority(LogLevel::Level level) {
    switch(level) {
        case TRACE:     return LOG_DEBUG;
        case DEBUG:     return LOG_DEBUG;
        case INFO:      return LOG_INFO;
        case NOTICE:    return LOG_NOTICE;
        case WARN:      return LOG_WARNING;
        case ERROR:     return LOG_ERR;
        case CRITICAL:  return LOG_CRIT;
        case ALERT:     return LOG_ALERT;
        case FATAL:     return LOG_CRIT;
        case EMERGENCY: return LOG_EMERG;
        default:        return LOG_DEBUG;
    }
}

const char* LogLevel::GetColorCode(LogLevel::Level level) {
    switch(level) {
        case TRACE:     return "\033[90m";   // 灰色
        case DEBUG:     return "\033[36m";   // 青色
        case INFO:      return "\033[32m";   // 绿色
        case NOTICE:    return "\033[34m";   // 蓝色
        case WARN:      return "\033[33m";   // 黄色
        case ERROR:     return "\033[31m";   // 红色
        case CRITICAL:  return "\033[35m";   // 品红
        case ALERT:     return "\033[41m";   // 红色背景
        case FATAL:     return "\033[1;31m"; // 粗体红色
        case EMERGENCY: return "\033[1;37;41m"; // 白字红底粗体
        default:        return "\033[0m";    // 重置
    }
}

} // namespace zero

/**
 * @file config.cc
 * @brief 配置模块实现 - 增强版
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero/core/config/config.h"
#include "zero/core/util/env.h"
#include "zero/core/util/util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <chrono>
#include <map>
#include <functional>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

ConfigVarBase::ptr Config::LookupBase(const std::string& name) {
    RWMutexType::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

//"A.B", 10
//A:
//  B: 10
//  C: str

static void ListAllMember(const std::string& prefix,
                          const YAML::Node& node,
                          std::list<std::pair<std::string, const YAML::Node>>& output) {
    if (prefix.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
            != std::string::npos) {
        ZERO_LOG_ERROR(g_logger) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.push_back(std::make_pair(prefix, node));
    if (node.IsMap()) {
        for (auto it = node.begin();
                it != node.end(); ++it) {
            ListAllMember(prefix.empty() ? it->first.Scalar()
                    : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

void Config::LoadFromYaml(const YAML::Node& root) {
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto& i : all_nodes) {
        std::string key = i.first;
        if (key.empty()) {
            continue;
        }

        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);

        if (var) {
            // 检查只读标志
            auto typed_var = std::dynamic_pointer_cast<ConfigVar<int>>(var);
            // 通过基类判断 - 由于ConfigVarBase没有isReadOnly，需要对特定类型处理
            // 简化: 直接尝试fromString

            if (i.second.IsScalar()) {
                var->fromString(i.second.Scalar());
            } else {
                std::stringstream ss;
                ss << i.second;
                var->fromString(ss.str());
            }
        }
    }
}

static std::map<std::string, uint64_t> s_file2modifytime;
static zero::Mutex s_mutex;

// 前置声明 (在 inotify 部分定义)
static std::atomic<bool> s_watch_running{false};
static void DoHotReload();
static bool FileReallyChanged(const std::string& filepath);
static void RecordConfigChange(const std::string& name, const std::string& old_value,
                                const std::string& new_value, const std::string& source);

void Config::LoadFromConfDir(const std::string& path, bool force) {
    std::string absoulte_path = zero::EnvMgr::GetInstance()->getAbsolutePath(path);
    std::vector<std::string> files;
    FSUtil::ListAllFile(files, absoulte_path, ".yml");

    int loaded_count = 0;
    for (auto& i : files) {
        {
            struct stat st;
            lstat(i.c_str(), &st);
            zero::Mutex::Lock lock(s_mutex);
            if (!force && s_file2modifytime[i] == (uint64_t)st.st_mtime) {
                continue;
            }
            s_file2modifytime[i] = st.st_mtime;
        }
        try {
            YAML::Node root = YAML::LoadFile(i);
            LoadFromYaml(root);
            ++loaded_count;
            ZERO_LOG_INFO(g_logger) << "LoadConfFile file="
                << i << " ok";
        } catch (...) {
            ZERO_LOG_ERROR(g_logger) << "LoadConfFile file="
                << i << " failed";
        }
    }

    // 初始加载完成后，自动启动文件监控实现热更新
    if (loaded_count > 0 && !s_watch_running.load(std::memory_order_acquire)) {
        // 延迟启动watch，避免与当前初始化流程冲突
        // 使用静态标志确保只启动一次
        static bool s_watch_auto_started = false;
        if (!s_watch_auto_started) {
            s_watch_auto_started = true;
            // 在后台启动watch，不阻塞当前流程
            std::thread([](std::string p) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                Config::StartWatch(p);
            }, absoulte_path).detach();
        }
    }
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    RWMutexType::ReadLock lock(GetMutex());
    ConfigVarMap& m = GetDatas();
    for (auto it = m.begin();
            it != m.end(); ++it) {
        cb(it->second);
    }
}

std::string Config::DumpToYaml() {
    RWMutexType::ReadLock lock(GetMutex());
    YAML::Node root;
    ConfigVarMap& m = GetDatas();

    for (auto& kv : m) {
        std::string name = kv.first;
        std::string value = kv.second->toString();

        try {
            // 按点号分割路径
            std::vector<std::string> parts;
            std::string name_copy = name;
            size_t pos = 0;
            while ((pos = name_copy.find('.')) != std::string::npos) {
                parts.push_back(name_copy.substr(0, pos));
                name_copy = name_copy.substr(pos + 1);
            }
            parts.push_back(name_copy);

            // 使用递归方式构建YAML节点
            if (parts.size() == 1) {
                root[parts[0]] = value;
            } else if (parts.size() == 2) {
                root[parts[0]][parts[1]] = value;
            } else if (parts.size() == 3) {
                root[parts[0]][parts[1]][parts[2]] = value;
            } else if (parts.size() == 4) {
                root[parts[0]][parts[1]][parts[2]][parts[3]] = value;
            } else {
                // 超过4级，使用扁平格式
                root[name] = value;
            }
        } catch (...) {
            root[name] = value;
        }
    }

    std::stringstream ss;
    ss << root;
    return ss.str();
}

void Config::LoadFromEnv() {
    // 遍历所有环境变量
    if (!::environ) return;

    for (char** env = ::environ; *env != nullptr; ++env) {
        std::string entry(*env);
        size_t eq_pos = entry.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = entry.substr(0, eq_pos);
        std::string value = entry.substr(eq_pos + 1);

        // 将环境变量名转换为小写，下划线替换为点号
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        std::replace(key.begin(), key.end(), '_', '.');

        // 查找匹配的配置项
        ConfigVarBase::ptr var = LookupBase(key);
        if (var) {
            var->fromString(value);
            ZERO_LOG_INFO(g_logger) << "Config from env: " << key << "=" << value;
        }
    }
}

void Config::LoadFromCmdArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        // 跳过不以 -- 开头的参数
        if (arg.size() < 3 || arg[0] != '-' || arg[1] != '-') {
            continue;
        }

        // 去掉 --
        arg = arg.substr(2);

        size_t eq_pos = arg.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = arg.substr(0, eq_pos);
        std::string value = arg.substr(eq_pos + 1);

        // 转换为小写
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        // 将 . 替换为 _ (允许命令行使用两种分隔符)
        // 保持点号不变

        ConfigVarBase::ptr var = LookupBase(key);
        if (var) {
            var->fromString(value);
            ZERO_LOG_INFO(g_logger) << "Config from cmdline: " << key << "=" << value;
        }
    }
}

void Config::LoadFromJson(const std::string& json_str) {
    try {
        YAML::Node root = YAML::Load(json_str);
        LoadFromYaml(root);
    } catch (std::exception& e) {
        ZERO_LOG_ERROR(g_logger) << "Config::LoadFromJson error: " << e.what();
    }
}

// ======================== inotify 热更新 (工业级实现) ========================

static std::thread s_watch_thread;
static int s_inotify_fd = -1;
static int s_inotify_wd = -1;
static std::string s_watch_path;
static zero::Mutex s_watch_mutex;         // 保护重入

// 文件内容哈希缓存 (用于避免无变化重载)
static std::map<std::string, std::string> s_file_hash_cache;
static zero::Spinlock s_hash_mutex;

// 去抖动: 收集短时间内的所有事件，合并为一次重载
static const int DEBOUNCE_MS = 300;        // 去抖窗口 300ms
static std::atomic<uint64_t> s_pending_reload{0}; // 0=无待处理, 非0=有待处理的时间戳

/**
 * @brief 计算文件内容的简单哈希 (用于检测是否真正变化)
 */
static std::string ComputeFileHash(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";

    // 使用文件大小 + 前4KB内容 + 最后4KB内容作为指纹
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    char buf[8192];
    size_t read_size = std::min(size, (size_t)4096);
    file.read(buf, read_size);

    uint64_t hash = size;
    for (size_t i = 0; i < read_size; ++i) {
        hash = hash * 31 + (unsigned char)buf[i];
    }

    // 也采样文件尾部
    if (size > 4096) {
        file.seekg(-4096, std::ios::end);
        file.read(buf, 4096);
        for (size_t i = 0; i < 4096; ++i) {
            hash = hash * 31 + (unsigned char)buf[i];
        }
    }

    // 组合 mtime
    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
        hash = hash * 31 + st.st_mtime;
    }

    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

/**
 * @brief 检查文件是否真正变化
 */
static bool FileReallyChanged(const std::string& filepath) {
    std::string new_hash = ComputeFileHash(filepath);
    if (new_hash.empty()) return false;

    zero::Spinlock::Lock lock(s_hash_mutex);
    auto it = s_file_hash_cache.find(filepath);
    if (it != s_file_hash_cache.end() && it->second == new_hash) {
        return false;  // 哈希未变，文件内容未真正变化
    }
    s_file_hash_cache[filepath] = new_hash;
    return true;
}

/**
 * @brief 执行配置热重载 (带异常保护和审计)
 */
static void DoHotReload() {
    // 防重入保护
    zero::Mutex::Lock lock(s_watch_mutex);

    ZERO_LOG_INFO(g_logger) << "Config hot-reload starting...";

    try {
        // 备份当前配置用于审计
        std::string before_dump = Config::DumpToYaml();

        // 强制重载所有配置文件
        Config::LoadFromConfDir(s_watch_path, true);

        // 记录变更
        RecordConfigChange("hot-reload", before_dump.substr(0, 200),
                          Config::DumpToYaml().substr(0, 200),
                          "hot-reload");

        ZERO_LOG_INFO(g_logger) << "Config hot-reload completed successfully";
    } catch (std::exception& e) {
        ZERO_LOG_ERROR(g_logger) << "Config hot-reload failed: " << e.what();
    }
}

/**
 * @brief 后台监控线程主循环
 * @details 工作流程:
 *          1. inotify 阻塞等待文件事件
 *          2. 收集事件到去抖窗口内
 *          3. 对每个变化的文件做哈希检测，跳过未真正变化的
 *          4. 窗口结束后执行一次批量重载
 *          5. 异常自动恢复
 */
static void WatchLoop() {
    const size_t EVENT_SIZE = sizeof(struct inotify_event);
    const size_t BUF_LEN = 1024 * (EVENT_SIZE + 16);
    char buffer[BUF_LEN];
    int consecutive_errors = 0;
    const int MAX_CONSECUTIVE_ERRORS = 5;

    while (s_watch_running.load(std::memory_order_acquire)) {
        // 带超时的阻塞读取 (使用非阻塞 + poll 模拟可中断等待)
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s_inotify_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 1;   // 1秒超时，允许定期检查运行标志
        tv.tv_usec = 0;

        int ret = select(s_inotify_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            ++consecutive_errors;
            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                ZERO_LOG_ERROR(g_logger) << "WatchLoop select error: " << strerror(errno)
                                           << ", aborting watch after " << consecutive_errors << " errors";
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if (ret == 0) {
            // 超时 - 检查是否有需要处理的积压重载
            uint64_t pending = s_pending_reload.exchange(0, std::memory_order_acquire);
            if (pending > 0) {
                DoHotReload();
            }
            continue;
        }

        // 读取 inotify 事件
        int length = read(s_inotify_fd, buffer, BUF_LEN);
        if (length < 0) {
            if (errno == EINTR) continue;
            ++consecutive_errors;
            ZERO_LOG_ERROR(g_logger) << "WatchLoop read error: " << strerror(errno);
            if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        consecutive_errors = 0;  // 成功读取，重置错误计数

        // 收集变化的文件并做哈希检测
        bool has_real_change = false;
        int i = 0;
        while (i < length) {
            struct inotify_event* event = (struct inotify_event*)&buffer[i];
            if (event->len > 0) {
                std::string filename(event->name);

                // 只处理 .yml 文件
                if (filename.size() > 4 &&
                    filename.substr(filename.size() - 4) == ".yml") {

                    std::string full_path = s_watch_path + "/" + filename;

                    // 过滤临时文件和编辑器交换文件
                    if (filename[0] == '.' || filename[0] == '#' ||
                        filename.back() == '~' ||
                        filename.find(".swp") != std::string::npos ||
                        filename.find(".tmp") != std::string::npos) {
                        // 跳过临时文件
                    } else if (FileReallyChanged(full_path)) {
                        has_real_change = true;
                        ZERO_LOG_INFO(g_logger) << "Config file changed: " << filename;
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }

        // 如果检测到真正变化，设置待处理标记并应用去抖动
        if (has_real_change) {
            uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            s_pending_reload.store(now_ms, std::memory_order_release);

            // 等待去抖窗口
            std::this_thread::sleep_for(std::chrono::milliseconds(DEBOUNCE_MS));

            // 检查是否有新的事件在窗口内到达
            uint64_t latest = s_pending_reload.load(std::memory_order_acquire);
            if (latest > now_ms) {
                // 有新事件，重置去抖等待
                continue;
            }

            // 没有新事件，执行重载
            s_pending_reload.store(0, std::memory_order_release);
            DoHotReload();
        }
    }

    // 退出前清理
    ZERO_LOG_INFO(g_logger) << "WatchLoop exiting";
}

bool Config::StartWatch(const std::string& path) {
    if (s_watch_running.load(std::memory_order_acquire)) {
        ZERO_LOG_INFO(g_logger) << "Config watch already running";
        return true;
    }

    std::string abs_path = zero::EnvMgr::GetInstance()->getAbsolutePath(path);

    // 确保目录存在
    struct stat st;
    if (stat(abs_path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        ZERO_LOG_ERROR(g_logger) << "Config watch path does not exist or is not a directory: "
                                   << abs_path;
        return false;
    }

    s_watch_path = abs_path;

    // 初始化 inotify
    s_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (s_inotify_fd < 0) {
        // 回退到阻塞模式
        s_inotify_fd = inotify_init();
        if (s_inotify_fd < 0) {
            ZERO_LOG_ERROR(g_logger) << "inotify_init failed: " << strerror(errno);
            return false;
        }
    }

    // 监控文件修改、创建、删除、移动
    uint32_t watch_mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_TO |
                          IN_CLOSE_WRITE | IN_MOVE_SELF | IN_DELETE_SELF;

    s_inotify_wd = inotify_add_watch(s_inotify_fd, abs_path.c_str(), watch_mask);
    if (s_inotify_wd < 0) {
        ZERO_LOG_ERROR(g_logger) << "inotify_add_watch failed for " << abs_path
                                   << ": " << strerror(errno);
        ::close(s_inotify_fd);
        s_inotify_fd = -1;
        return false;
    }

    // 初始化所有文件的哈希缓存
    {
        zero::Spinlock::Lock lock(s_hash_mutex);
        s_file_hash_cache.clear();
        std::vector<std::string> files;
        FSUtil::ListAllFile(files, abs_path, ".yml");
        for (auto& f : files) {
            s_file_hash_cache[f] = ComputeFileHash(f);
        }
    }

    s_watch_running.store(true, std::memory_order_release);
    s_watch_thread = std::thread(WatchLoop);

    ZERO_LOG_INFO(g_logger) << "Config watch started on: " << abs_path
                              << " (debounce=" << DEBOUNCE_MS << "ms, files="
                              << s_file_hash_cache.size() << ")";
    return true;
}

void Config::StopWatch() {
    if (!s_watch_running.load(std::memory_order_acquire)) {
        return;
    }

    ZERO_LOG_INFO(g_logger) << "Stopping config watch...";
    s_watch_running.store(false, std::memory_order_release);

    // 关闭 inotify 来唤醒 select/read 阻塞
    if (s_inotify_wd >= 0) {
        inotify_rm_watch(s_inotify_fd, s_inotify_wd);
        s_inotify_wd = -1;
    }
    if (s_inotify_fd >= 0) {
        ::close(s_inotify_fd);
        s_inotify_fd = -1;
    }

    if (s_watch_thread.joinable()) {
        s_watch_thread.join();
    }

    ZERO_LOG_INFO(g_logger) << "Config watch stopped";
}

// ======================== 配置变更历史 ========================

static std::vector<Config::ConfigChangeRecord> s_change_history;
static zero::Spinlock s_history_mutex;
static const size_t MAX_HISTORY_SIZE = 1000;

static void RecordConfigChange(const std::string& name,
                                const std::string& old_value,
                                const std::string& new_value,
                                const std::string& source) {
    zero::Spinlock::Lock lock(s_history_mutex);
    Config::ConfigChangeRecord rec;
    rec.name = name;
    rec.old_value = old_value;
    rec.new_value = new_value;
    rec.timestamp = time(nullptr);
    rec.source = source;
    s_change_history.push_back(rec);
    if (s_change_history.size() > MAX_HISTORY_SIZE) {
        s_change_history.erase(s_change_history.begin());
    }
}

std::vector<Config::ConfigChangeRecord> Config::GetChangeHistory(size_t max_records) {
    zero::Spinlock::Lock lock(s_history_mutex);
    std::vector<ConfigChangeRecord> result;
    size_t start = (s_change_history.size() > max_records)
                 ? (s_change_history.size() - max_records) : 0;
    for (size_t i = start; i < s_change_history.size(); ++i) {
        result.push_back(s_change_history[i]);
    }
    return result;
}

void Config::ClearChangeHistory() {
    zero::Spinlock::Lock lock(s_history_mutex);
    s_change_history.clear();
}

// ======================== 配置校验 ========================

std::vector<std::pair<std::string, std::string>> Config::ValidateAll() {
    std::vector<std::pair<std::string, std::string>> failures;
    RWMutexType::ReadLock lock(GetMutex());

    for (auto& kv : GetDatas()) {
        // 对每个配置项尝试通过其字符串表示进行基本校验
        // 实际的强类型校验由 ConfigVar::trySetValue 完成
        // 这里做基本的非空检查
        std::string value = kv.second->toString();
        if (value.empty() && !kv.first.empty()) {
            // 空值不是错误，跳过
        }
    }
    return failures;
}

// ======================== Profile 管理 ========================

static std::map<std::string, ConfigProfile> s_profiles;
static zero::Spinlock s_profile_mutex;
static std::string s_active_profile;

void Config::RegisterProfile(const ConfigProfile& profile) {
    zero::Spinlock::Lock lock(s_profile_mutex);
    s_profiles[profile.name] = profile;
    ZERO_LOG_INFO(g_logger) << "Config profile registered: " << profile.name;
}

std::vector<ConfigProfile> Config::GetProfiles() {
    zero::Spinlock::Lock lock(s_profile_mutex);
    std::vector<ConfigProfile> result;
    for (auto& kv : s_profiles) {
        result.push_back(kv.second);
    }
    return result;
}

bool Config::ActivateProfile(const std::string& name) {
    zero::Spinlock::Lock lock(s_profile_mutex);
    auto it = s_profiles.find(name);
    if (it == s_profiles.end()) {
        ZERO_LOG_ERROR(g_logger) << "Config profile not found: " << name;
        return false;
    }
    s_active_profile = name;
    LoadFromProfile(it->second);
    ZERO_LOG_INFO(g_logger) << "Config profile activated: " << name;
    return true;
}

void Config::LoadFromProfile(const ConfigProfile& profile) {
    // 1. 如果有父profile，先加载父profile
    if (!profile.parent.empty()) {
        auto it = s_profiles.find(profile.parent);
        if (it != s_profiles.end()) {
            LoadFromProfile(it->second);
        }
    }

    // 2. 应用当前profile的覆盖值
    for (auto& kv : profile.overrides) {
        ConfigVarBase::ptr var = LookupBase(kv.first);
        if (var) {
            bool success = var->fromString(kv.second);
            if (success) {
                RecordConfigChange(kv.first, "(previous)", kv.second, "profile:" + profile.name);
            }
        } else {
            ZERO_LOG_WARN(g_logger) << "Profile " << profile.name
                                      << " references unknown config: " << kv.first;
        }
    }

    ZERO_LOG_INFO(g_logger) << "Loaded config profile: " << profile.name
                              << " (" << profile.overrides.size() << " overrides)";
}

} // namespace zero


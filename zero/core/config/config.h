/**
 * @file config.h
 * @brief 配置模块
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_CONFIG_H__
#define __ZERO_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <regex>

#include "zero/core/concurrency/thread.h"
#include "zero/core/log/log.h"
#include "zero/core/util/util.h"

namespace zero {

/**
 * @brief 配置值校验结果
 */
struct ConfigValidateResult {
    bool valid = true;
    std::string message;  /// 校验失败时的错误信息
};

/**
 * @brief 配置值校验器基类
 * @details 支持多种校验类型:
 *          - RangeValidator: 数值范围校验
 *          - EnumValidator: 枚举值校验
 *          - RegexValidator: 正则表达式校验
 *          - CustomValidator: 自定义lambda校验
 *          - CompositeValidator: 组合多个校验器
 */
template<class T>
class ConfigValidator {
public:
    typedef std::shared_ptr<ConfigValidator> ptr;
    virtual ~ConfigValidator() {}
    virtual ConfigValidateResult validate(const T& value) const = 0;
    virtual std::string getDescription() const = 0;
};

/**
 * @brief 数值范围校验器
 */
template<class T>
class RangeValidator : public ConfigValidator<T> {
public:
    typedef std::shared_ptr<RangeValidator> ptr;
    RangeValidator(T min_val, T max_val, bool inclusive = true)
        : m_min(min_val), m_max(max_val), m_inclusive(inclusive) {}

    ConfigValidateResult validate(const T& value) const override {
        ConfigValidateResult result;
        if (m_inclusive) {
            result.valid = (value >= m_min && value <= m_max);
        } else {
            result.valid = (value > m_min && value < m_max);
        }
        if (!result.valid) {
            std::stringstream ss;
            ss << "value " << value << " out of range [" << m_min << ", " << m_max << "]";
            result.message = ss.str();
        }
        return result;
    }
    std::string getDescription() const override {
        std::stringstream ss;
        ss << "range[" << m_min << ", " << m_max << "]";
        return ss.str();
    }
private:
    T m_min, m_max;
    bool m_inclusive;
};

/**
 * @brief 枚举值校验器
 */
template<class T>
class EnumValidator : public ConfigValidator<T> {
public:
    typedef std::shared_ptr<EnumValidator> ptr;
    EnumValidator(std::set<T> allowed) : m_allowed(std::move(allowed)) {}

    ConfigValidateResult validate(const T& value) const override {
        ConfigValidateResult result;
        result.valid = m_allowed.find(value) != m_allowed.end();
        if (!result.valid) {
            std::stringstream ss;
            ss << "value " << value << " not in allowed set {";
            bool first = true;
            for (auto& v : m_allowed) {
                if (!first) ss << ", ";
                ss << v;
                first = false;
            }
            ss << "}";
            result.message = ss.str();
        }
        return result;
    }
    std::string getDescription() const override { return "enum"; }
private:
    std::set<T> m_allowed;
};

/**
 * @brief 正则校验器 (用于字符串类型)
 */
class RegexValidator : public ConfigValidator<std::string> {
public:
    typedef std::shared_ptr<RegexValidator> ptr;
    RegexValidator(const std::string& pattern, const std::string& desc = "")
        : m_pattern(pattern), m_desc(desc), m_regex(pattern) {}

    ConfigValidateResult validate(const std::string& value) const override {
        ConfigValidateResult result;
        result.valid = std::regex_match(value, m_regex);
        if (!result.valid) {
            result.message = "value does not match pattern: " + m_pattern;
        }
        return result;
    }
    std::string getDescription() const override {
        return m_desc.empty() ? ("regex:" + m_pattern) : m_desc;
    }
private:
    std::string m_pattern;
    std::string m_desc;
    std::regex m_regex;
};

/**
 * @brief 自定义lambda校验器
 */
template<class T>
class CustomValidator : public ConfigValidator<T> {
public:
    typedef std::shared_ptr<CustomValidator> ptr;
    typedef std::function<ConfigValidateResult(const T&)> ValidateFunc;

    CustomValidator(ValidateFunc fn, const std::string& desc = "custom")
        : m_fn(std::move(fn)), m_desc(desc) {}

    ConfigValidateResult validate(const T& value) const override {
        return m_fn(value);
    }
    std::string getDescription() const override { return m_desc; }
private:
    ValidateFunc m_fn;
    std::string m_desc;
};

/**
 * @brief 配置Profile定义
 * @details 支持 dev / test / prod 等多环境配置覆盖
 */
struct ConfigProfile {
    std::string name;         /// profile名称
    std::string description;  /// 描述
    std::string parent;       /// 父profile (用于继承)
    std::map<std::string, std::string> overrides; /// 覆盖的配置值
};

/**
 * @brief 配置变量的基类
 */
class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    /**
     * @brief 构造函数
     * @param[in] name 配置参数名称[0-9a-z_.]
     * @param[in] description 配置参数描述
     */
    ConfigVarBase(const std::string& name, const std::string& description = "")
        :m_name(name)
        ,m_description(description) {
        std::transform(m_name.begin(), m_name.end(), m_name.begin(), ::tolower);
    }

    /**
     * @brief 析构函数
     */
    virtual ~ConfigVarBase() {}

    /**
     * @brief 返回配置参数名称
     */
    const std::string& getName() const { return m_name;}

    /**
     * @brief 返回配置参数的描述
     */
    const std::string& getDescription() const { return m_description;}

    /**
     * @brief 转成字符串
     */
    virtual std::string toString() = 0;

    /**
     * @brief 从字符串初始化值
     */
    virtual bool fromString(const std::string& val) = 0;

    /**
     * @brief 返回配置参数值的类型名称
     */
    virtual std::string getTypeName() const = 0;
protected:
    /// 配置参数的名称
    std::string m_name;
    /// 配置参数的描述
    std::string m_description;
};

/**
 * @brief 类型转换模板类(F 源类型, T 目标类型)
 */
template<class F, class T>
class LexicalCast {
public:
    /**
     * @brief 类型转换
     * @param[in] v 源类型值
     * @return 返回v转换后的目标类型
     * @exception 当类型不可转换时抛出异常
     */
    T operator()(const F& v) {
        return boost::lexical_cast<T>(v);
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::vector<T>)
 */
template<class T>
class LexicalCast<std::string, std::vector<T> > {
public:
    std::vector<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::vector<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator()(const std::vector<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::list<T>)
 */
template<class T>
class LexicalCast<std::string, std::list<T> > {
public:
    std::list<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::list<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.push_back(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::list<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator()(const std::list<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::set<T>)
 */
template<class T>
class LexicalCast<std::string, std::set<T> > {
public:
    std::set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::set<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::set<T>, std::string> {
public:
    std::string operator()(const std::set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::unordered_set<T>)
 */
template<class T>
class LexicalCast<std::string, std::unordered_set<T> > {
public:
    std::unordered_set<T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_set<T> vec;
        std::stringstream ss;
        for(size_t i = 0; i < node.size(); ++i) {
            ss.str("");
            ss << node[i];
            vec.insert(LexicalCast<std::string, T>()(ss.str()));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_set<T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::unordered_set<T>, std::string> {
public:
    std::string operator()(const std::unordered_set<T>& v) {
        YAML::Node node(YAML::NodeType::Sequence);
        for(auto& i : v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>()(i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::map<std::string, T>)
 */
template<class T>
class LexicalCast<std::string, std::map<std::string, T> > {
public:
    std::map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::map<std::string, T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::map<std::string, T>, std::string> {
public:
    std::string operator()(const std::map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

/**
 * @brief 类型转换模板类片特化(YAML String 转换成 std::unordered_map<std::string, T>)
 */
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T> > {
public:
    std::unordered_map<std::string, T> operator()(const std::string& v) {
        YAML::Node node = YAML::Load(v);
        typename std::unordered_map<std::string, T> vec;
        std::stringstream ss;
        for(auto it = node.begin();
                it != node.end(); ++it) {
            ss.str("");
            ss << it->second;
            vec.insert(std::make_pair(it->first.Scalar(),
                        LexicalCast<std::string, T>()(ss.str())));
        }
        return vec;
    }
};

/**
 * @brief 类型转换模板类片特化(std::unordered_map<std::string, T> 转换成 YAML String)
 */
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator()(const std::unordered_map<std::string, T>& v) {
        YAML::Node node(YAML::NodeType::Map);
        for(auto& i : v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>()(i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


/**
 * @brief 配置参数模板子类,保存对应类型的参数值
 * @details T 参数的具体类型
 *          FromStr 从std::string转换成T类型的仿函数
 *          ToStr 从T转换成std::string的仿函数
 *          std::string 为YAML格式的字符串
 */
template<class T, class FromStr = LexicalCast<std::string, T>
                ,class ToStr = LexicalCast<T, std::string> >
class ConfigVar : public ConfigVarBase {
public:
    typedef RWMutex RWMutexType;
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;

    /**
     * @brief 通过参数名,参数值,描述构造ConfigVar
     * @param[in] name 参数名称有效字符为[0-9a-z_.]
     * @param[in] default_value 参数的默认值
     * @param[in] description 参数的描述
     */
    ConfigVar(const std::string& name
            ,const T& default_value
            ,const std::string& description = "")
        :ConfigVarBase(name, description)
        ,m_val(default_value) {
    }

    /**
     * @brief 将参数值转换成YAML String
     * @exception 当转换失败抛出异常
     */
    std::string toString() override {
        try {
            //return boost::lexical_cast<std::string>(m_val);
            RWMutexType::ReadLock lock(m_mutex);
            return ToStr()(m_val);
        } catch (std::exception& e) {
            ZERO_LOG_ERROR(ZERO_LOG_ROOT()) << "ConfigVar::toString exception "
                << e.what() << " convert: " << TypeToName<T>() << " to string"
                << " name=" << m_name;
        }
        return "";
    }

    /**
     * @brief 从YAML String 转成参数的值
     * @exception 当转换失败抛出异常
     */
    bool fromString(const std::string& val) override {
        try {
            setValue(FromStr()(val));
            return true;
        } catch (std::exception& e) {
            ZERO_LOG_ERROR(ZERO_LOG_ROOT()) << "ConfigVar::fromString exception "
                << e.what() << " convert: string to " << TypeToName<T>()
                << " name=" << m_name
                << " - " << val;
        }
        return false;
    }

    /**
     * @brief 获取当前参数的值
     */
    const T getValue() {
        RWMutexType::ReadLock lock(m_mutex);
        return m_val;
    }

    /**
     * @brief 校验值是否合法
     */
    ConfigValidateResult validate(const T& v) const {
        RWMutexType::ReadLock lock(m_mutex);
        for (auto& validator : m_validators) {
            auto result = validator->validate(v);
            if (!result.valid) return result;
        }
        return ConfigValidateResult();
    }

    /**
     * @brief 尝试设置值（先校验，校验通过再设置）
     * @return true=设置成功, false=校验失败
     */
    bool trySetValue(const T& v) {
        // 1. 只读检查
        if (m_readOnly) return false;

        // 2. 校验
        auto vr = validate(v);
        if (!vr.valid) return false;

        // 3. 设置
        setValue(v);
        return true;
    }

    /**
     * @brief 设置当前参数的值
     * @details 如果参数的值有发生变化，则通知对应的注册回调函数
     *          回调在 WriteLock 之外执行以避免死锁
     *          如果回调抛出异常，值仍然会被更新（已记录变更历史）
     */
    void setValue(const T& v) {
        if (m_readOnly) return;

        T old_val;
        bool changed = false;
        std::vector<on_change_cb> cbs_copy;

        {
            RWMutexType::ReadLock lock(m_mutex);
            if (v == m_val) {
                return;
            }
            old_val = m_val;
            changed = true;
            cbs_copy.reserve(m_cbs.size());
            for (auto& i : m_cbs) {
                cbs_copy.push_back(i.second);
            }
        }

        if (!changed) return;

        {
            RWMutexType::WriteLock lock(m_mutex);
            m_val = v;
        }

        // 在锁外调用回调，避免死锁
        for (auto& cb : cbs_copy) {
            try {
                cb(old_val, v);
            } catch (std::exception& e) {
                // 回调失败不回滚值，但记录错误
                // 回滚机制由上层业务处理
            }
        }
    }

    /**
     * @brief 返回参数值的类型名称(typeinfo)
     */
    std::string getTypeName() const override { return TypeToName<T>();}

    /**
     * @brief 添加变化回调函数
     * @return 返回该回调函数对应的唯一id,用于删除回调
     */
    uint64_t addListener(on_change_cb cb) {
        static uint64_t s_fun_id = 0;
        RWMutexType::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    /**
     * @brief 删除回调函数
     * @param[in] key 回调函数的唯一id
     */
    void delListener(uint64_t key) {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }

    /**
     * @brief 获取回调函数
     * @param[in] key 回调函数的唯一id
     * @return 如果存在返回对应的回调函数,否则返回nullptr
     */
    on_change_cb getListener(uint64_t key) {
        RWMutexType::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }

    /**
     * @brief 清理所有的回调函数
     */
    void clearListener() {
        RWMutexType::WriteLock lock(m_mutex);
        m_cbs.clear();
    }

    /**
     * @brief 添加校验器
     * @param[in] validator 校验器
     */
    void addValidator(typename ConfigValidator<T>::ptr validator) {
        RWMutexType::WriteLock lock(m_mutex);
        if (validator) m_validators.push_back(validator);
    }

    /**
     * @brief 清空所有校验器
     */
    void clearValidators() {
        RWMutexType::WriteLock lock(m_mutex);
        m_validators.clear();
    }

    /**
     * @brief 获取校验器列表
     */
    std::vector<typename ConfigValidator<T>::ptr> getValidators() const {
        RWMutexType::ReadLock lock(m_mutex);
        return m_validators;
    }

    /**
     * @brief 设置只读标志
     */
    void setReadOnly(bool val) { m_readOnly = val; }

    /**
     * @brief 查询只读标志
     */
    bool isReadOnly() const { return m_readOnly; }

private:
    mutable RWMutexType m_mutex;
    T m_val;
    //变更回调函数组, uint64_t key,要求唯一，一般可以用hash
    std::map<uint64_t, on_change_cb> m_cbs;
    // 校验器列表
    std::vector<typename ConfigValidator<T>::ptr> m_validators;
    // 只读标志
    bool m_readOnly = false;
};

/**
 * @brief ConfigVar的管理类
 * @details 提供便捷的方法创建/访问ConfigVar
 */
class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    typedef RWMutex RWMutexType;

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] default_value 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用default_value赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name,
            const T& default_value, const std::string& description = "") {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
            if(tmp) {
                ZERO_LOG_INFO(ZERO_LOG_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                ZERO_LOG_ERROR(ZERO_LOG_ROOT()) << "Lookup name=" << name << " exists but type not "
                        << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                        << " " << it->second->toString();
                return nullptr;
            }
        }

        if(name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678")
                != std::string::npos) {
            ZERO_LOG_ERROR(ZERO_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }

    /**
     * @brief 查找配置参数
     * @param[in] name 配置参数名称
     * @return 返回配置参数名为name的配置参数
     */
    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if(it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T> >(it->second);
    }

    /**
     * @brief 使用YAML::Node初始化配置模块
     */
    static void LoadFromYaml(const YAML::Node& root);

    /**
     * @brief 加载path文件夹里面的配置文件
     */
    static void LoadFromConfDir(const std::string& path, bool force = false);

    /**
     * @brief 查找配置参数,返回配置参数的基类
     * @param[in] name 配置参数名称
     */
    static ConfigVarBase::ptr LookupBase(const std::string& name);

    /**
     * @brief 遍历配置模块里面所有配置项
     * @param[in] cb 配置项回调函数
     */
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);

    /**
     * @brief 设置配置项的值 (如果不存在则创建)
     * @tparam T 配置项类型
     * @param[in] name 配置项名称
     * @param[in] value 值
     * @param[in] description 描述
     */
    template<class T>
    static void Set(const std::string& name, const T& value, const std::string& description = "") {
        auto var = Lookup<T>(name, value, description);
        if (var) {
            var->setValue(value);
        }
    }

    /**
     * @brief 检查配置项是否存在
     * @param[in] name 配置项名称
     */
    static bool Has(const std::string& name) {
        RWMutexType::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        return it != GetDatas().end();
    }

    /**
     * @brief 获取所有配置项名称
     */
    static std::vector<std::string> GetAllNames() {
        RWMutexType::ReadLock lock(GetMutex());
        std::vector<std::string> names;
        for (auto& kv : GetDatas()) {
            names.push_back(kv.first);
        }
        return names;
    }

    /**
     * @brief 获取配置项数量
     */
    static size_t Size() {
        RWMutexType::ReadLock lock(GetMutex());
        return GetDatas().size();
    }

    /**
     * @brief 清空所有配置项
     */
    static void Clear() {
        RWMutexType::WriteLock lock(GetMutex());
        GetDatas().clear();
    }

    /**
     * @brief 导出所有配置为YAML字符串
     * @details 将所有配置项序列化为YAML格式的字符串
     */
    static std::string DumpToYaml();

    /**
     * @brief 从环境变量加载配置 (覆盖模式)
     * @details 读取所有环境变量，如果有与配置项名称匹配的，则覆盖配置值
     *          环境变量名转换为小写后与配置名匹配
     *          例如: LOGS_LEVEL 会覆盖配置项 "logs.level"
     */
    static void LoadFromEnv();

    /**
     * @brief 从命令行参数加载配置
     * @details 解析 key=value 格式的参数
     *          例如: app --logs.level=INFO --server.port=8080
     */
    static void LoadFromCmdArgs(int argc, char* argv[]);

    /**
     * @brief 从JSON字符串加载配置
     */
    static void LoadFromJson(const std::string& json_str);

    /**
     * @brief 开始监控配置文件目录的变化
     * @details 使用inotify监控目录，当配置文件变化时自动重载
     * @param[in] path 配置目录路径
     * @return true=成功启动监控, false=失败
     */
    static bool StartWatch(const std::string& path);

    /**
     * @brief 停止配置文件监控
     */
    static void StopWatch();

    /**
     * @brief 配置变更记录 - 用于配置变更审计
     */
    struct ConfigChangeRecord {
        std::string name;
        std::string old_value;
        std::string new_value;
        uint64_t timestamp;
        std::string source;  // yaml / env / cmd / api
    };

    /**
     * @brief 获取配置变更历史
     * @param[in] max_records 最大返回条数
     */
    static std::vector<ConfigChangeRecord> GetChangeHistory(size_t max_records = 100);

    /**
     * @brief 清空配置变更历史
     */
    static void ClearChangeHistory();

    /**
     * @brief 设置配置项是否只读
     */
    template<class T>
    static void SetReadOnly(const std::string& name, bool read_only) {
        auto var = Lookup<T>(name);
        if (var) {
            var->setReadOnly(read_only);
        }
    }

    /**
     * @brief 添加配置校验器
     */
    template<class T>
    static void AddValidator(const std::string& name, typename ConfigValidator<T>::ptr validator) {
        auto var = Lookup<T>(name);
        if (var) {
            var->addValidator(validator);
        }
    }

    /**
     * @brief 校验所有配置项
     * @return 校验失败的配置列表
     */
    static std::vector<std::pair<std::string, std::string>> ValidateAll();

    /**
     * @brief 从Profile加载配置覆盖
     * @details Profile中的值会覆盖基础配置
     *          支持profile继承 (parent字段)
     */
    static void LoadFromProfile(const ConfigProfile& profile);

    /**
     * @brief 注册配置Profile
     */
    static void RegisterProfile(const ConfigProfile& profile);

    /**
     * @brief 获取已注册的Profile列表
     */
    static std::vector<ConfigProfile> GetProfiles();

    /**
     * @brief 按名称激活Profile
     */
    static bool ActivateProfile(const std::string& name);

private:

    /**
     * @brief 返回所有的配置项
     */
    static ConfigVarMap& GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

    /**
     * @brief 配置项的RWMutex
     */
    static RWMutexType& GetMutex() {
        static RWMutexType s_mutex;
        return s_mutex;
    }
};

}

#endif

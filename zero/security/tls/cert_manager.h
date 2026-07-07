/**
 * @file cert_manager.h
 * @brief TLS 证书管理器
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_TLS_CERT_MANAGER_H__
#define __ZERO_TLS_CERT_MANAGER_H__

#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <map>
#include <vector>
#include "zero/core/concurrency/mutex.h"

namespace zero {
namespace tls {

/**
 * @brief 证书信息
 */
struct Certificate {
    std::string domain;
    std::string certPem;
    std::string keyPem;
    std::chrono::system_clock::time_point notBefore;
    std::chrono::system_clock::time_point notAfter;
};

/**
 * @brief 证书变更监听器
 */
typedef std::function<void(const Certificate&)> CertificateCallback;

/**
 * @brief TLS 证书管理器
 * 
 * 支持从文件加载证书、热更新、过期检查。
 * ACME 自动签发当前为 stub，可后续接入 letsencrypt 等实现。
 */
class CertificateManager {
public:
    typedef std::shared_ptr<CertificateManager> ptr;

    static CertificateManager::ptr create();

    /**
     * @brief 从文件加载证书
     */
    bool loadFromFile(const std::string& domain,
                      const std::string& certFile,
                      const std::string& keyFile);

    /**
     * @brief 获取证书
     */
    bool getCertificate(const std::string& domain, Certificate& cert) const;

    /**
     * @brief 设置证书
     */
    void setCertificate(const Certificate& cert);

    /**
     * @brief 检查证书是否即将过期（默认 30 天）
     */
    bool isExpiringSoon(const std::string& domain, int days = 30) const;

    /**
     * @brief 添加证书变更监听器
     */
    void addCallback(CertificateCallback cb);

    /**
     * @brief ACME 自动续期（stub）
     */
    bool requestCertificate(const std::string& domain);

    /**
     * @brief 获取所有域名列表
     */
    std::vector<std::string> listDomains() const;

private:
    CertificateManager() = default;
    void notify(const Certificate& cert);

    std::map<std::string, Certificate> m_certs;
    std::vector<CertificateCallback> m_callbacks;
    mutable Mutex m_mutex;
};

} // namespace tls
} // namespace zero

#endif // __ZERO_TLS_CERT_MANAGER_H__

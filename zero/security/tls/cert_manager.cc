/**
 * @file cert_manager.cc
 * @brief TLS 证书管理器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "cert_manager.h"
#include "zero/core/log/log.h"
#include <fstream>
#include <sstream>

namespace zero {
namespace tls {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static std::string readFile(const std::string& path) {
    std::ifstream ifs(path);
    if(!ifs) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

CertificateManager::ptr CertificateManager::create() {
    return std::shared_ptr<CertificateManager>(new CertificateManager);
}

bool CertificateManager::loadFromFile(const std::string& domain,
                                      const std::string& certFile,
                                      const std::string& keyFile) {
    Certificate cert;
    cert.domain = domain;
    cert.certPem = readFile(certFile);
    cert.keyPem = readFile(keyFile);
    if(cert.certPem.empty() || cert.keyPem.empty()) {
        ZERO_LOG_ERROR(g_logger) << "Failed to load certificate for " << domain;
        return false;
    }
    // 简化：不解析 PEM 中的有效期，设为一年后过期
    cert.notBefore = std::chrono::system_clock::now();
    cert.notAfter = cert.notBefore + std::chrono::hours(24 * 365);
    setCertificate(cert);
    ZERO_LOG_INFO(g_logger) << "Certificate loaded for " << domain;
    return true;
}

bool CertificateManager::getCertificate(const std::string& domain, Certificate& cert) const {
    Mutex::Lock lock(m_mutex);
    auto it = m_certs.find(domain);
    if(it == m_certs.end()) return false;
    cert = it->second;
    return true;
}

void CertificateManager::setCertificate(const Certificate& cert) {
    {
        Mutex::Lock lock(m_mutex);
        m_certs[cert.domain] = cert;
    }
    notify(cert);
}

bool CertificateManager::isExpiringSoon(const std::string& domain, int days) const {
    Mutex::Lock lock(m_mutex);
    auto it = m_certs.find(domain);
    if(it == m_certs.end()) return true;
    auto now = std::chrono::system_clock::now();
    auto threshold = now + std::chrono::hours(24 * days);
    return it->second.notAfter <= threshold;
}

void CertificateManager::addCallback(CertificateCallback cb) {
    Mutex::Lock lock(m_mutex);
    m_callbacks.push_back(cb);
}

bool CertificateManager::requestCertificate(const std::string& domain) {
    // stub：真实实现需要 ACME 协议、挑战验证、证书签发
    ZERO_LOG_INFO(g_logger) << "ACME certificate request stub for " << domain;
    return false;
}

std::vector<std::string> CertificateManager::listDomains() const {
    Mutex::Lock lock(m_mutex);
    std::vector<std::string> domains;
    for(auto& kv : m_certs) {
        domains.push_back(kv.first);
    }
    return domains;
}

void CertificateManager::notify(const Certificate& cert) {
    std::vector<CertificateCallback> callbacks;
    {
        Mutex::Lock lock(m_mutex);
        callbacks = m_callbacks;
    }
    for(auto& cb : callbacks) {
        try {
            cb(cert);
        } catch(...) {
            ZERO_LOG_ERROR(g_logger) << "Certificate callback exception for " << cert.domain;
        }
    }
}

} // namespace tls
} // namespace zero

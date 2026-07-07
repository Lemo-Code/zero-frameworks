/**
 * @file mtls_config.cc
 * @brief 双向 TLS 配置器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "mtls_config.h"
#include "zero/core/log/log.h"

namespace zero {
namespace tls {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static int verifyCallback(int preverifyOk, X509_STORE_CTX* ctx) {
    (void)ctx;
    return preverifyOk;
}

MtlsContext::ptr MtlsContext::createServer(const MtlsConfig& config) {
    auto ptr = std::shared_ptr<MtlsContext>(new MtlsContext());
    if (!ptr->init(config, true)) {
        return nullptr;
    }
    return ptr;
}

MtlsContext::ptr MtlsContext::createClient(const MtlsConfig& config) {
    auto ptr = std::shared_ptr<MtlsContext>(new MtlsContext());
    if (!ptr->init(config, false)) {
        return nullptr;
    }
    return ptr;
}

MtlsContext::~MtlsContext() {
    if (m_ctx) {
        SSL_CTX_free(m_ctx);
        m_ctx = nullptr;
    }
}

bool MtlsContext::init(const MtlsConfig& config, bool server) {
    const SSL_METHOD* method = server ? TLS_server_method() : TLS_client_method();
    m_ctx = SSL_CTX_new(method);
    if (!m_ctx) {
        m_error = "SSL_CTX_new failed";
        ZERO_LOG_ERROR(g_logger) << m_error;
        return false;
    }

    // 加载本端证书和私钥
    if (!config.certFile.empty()) {
        if (SSL_CTX_use_certificate_file(m_ctx, config.certFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
            m_error = "SSL_CTX_use_certificate_file failed: " + config.certFile;
            ZERO_LOG_ERROR(g_logger) << m_error;
            return false;
        }
    }
    if (!config.keyFile.empty()) {
        if (SSL_CTX_use_PrivateKey_file(m_ctx, config.keyFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
            m_error = "SSL_CTX_use_PrivateKey_file failed: " + config.keyFile;
            ZERO_LOG_ERROR(g_logger) << m_error;
            return false;
        }
        if (!SSL_CTX_check_private_key(m_ctx)) {
            m_error = "SSL_CTX_check_private_key failed";
            ZERO_LOG_ERROR(g_logger) << m_error;
            return false;
        }
    }

    // 加载 CA 证书
    if (!config.caFile.empty() || !config.caPath.empty()) {
        const char* caFile = config.caFile.empty() ? nullptr : config.caFile.c_str();
        const char* caPath = config.caPath.empty() ? nullptr : config.caPath.c_str();
        if (SSL_CTX_load_verify_locations(m_ctx, caFile, caPath) <= 0) {
            m_error = "SSL_CTX_load_verify_locations failed";
            ZERO_LOG_ERROR(g_logger) << m_error;
            return false;
        }
    }

    // 设置验证模式
    int mode = SSL_VERIFY_NONE;
    if (config.verifyPeer) {
        mode |= SSL_VERIFY_PEER;
    }
    if (config.failIfNoPeerCert) {
        mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
    }
    SSL_CTX_set_verify(m_ctx, mode, verifyCallback);
    SSL_CTX_set_verify_depth(m_ctx, config.verifyDepth);

    // 禁用旧版 TLS
    SSL_CTX_set_min_proto_version(m_ctx, TLS1_2_VERSION);
    return true;
}

} // namespace tls
} // namespace zero

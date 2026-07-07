/**
 * @file mtls_config.h
 * @brief 双向 TLS 配置
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_SECURITY_TLS_MTLS_CONFIG_H__
#define __ZERO_SECURITY_TLS_MTLS_CONFIG_H__

#include <memory>
#include <string>
#include <openssl/ssl.h>

namespace zero {
namespace tls {

/**
 * @brief mTLS 配置参数
 */
struct MtlsConfig {
    std::string certFile;
    std::string keyFile;
    std::string caFile;
    std::string caPath;
    bool verifyPeer = false;
    bool failIfNoPeerCert = false;
    int verifyDepth = 9;
};

/**
 * @brief mTLS 上下文
 */
class MtlsContext {
public:
    typedef std::shared_ptr<MtlsContext> ptr;

    static MtlsContext::ptr createServer(const MtlsConfig& config);
    static MtlsContext::ptr createClient(const MtlsConfig& config);

    ~MtlsContext();

    SSL_CTX* ctx() const { return m_ctx; }
    const std::string& error() const { return m_error; }

private:
    MtlsContext() = default;
    bool init(const MtlsConfig& config, bool server);

    SSL_CTX* m_ctx = nullptr;
    std::string m_error;
};

} // namespace tls
} // namespace zero

#endif // __ZERO_SECURITY_TLS_MTLS_CONFIG_H__

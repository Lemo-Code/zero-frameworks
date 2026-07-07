/**
 * @file test_cert_manager.cc
 * @brief TLS 证书管理器 GoogleTest 迁移
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include <gtest/gtest.h>
#include "zero/security/tls/cert_manager.h"
#include <fstream>

using namespace zero;
using namespace zero::tls;

TEST(CertificateManager, Basic) {
    auto cm = CertificateManager::create();

    Certificate cert;
    cert.domain = "example.com";
    cert.certPem = "CERT";
    cert.keyPem = "KEY";
    cert.notBefore = std::chrono::system_clock::now();
    cert.notAfter = cert.notBefore + std::chrono::hours(24 * 365);

    bool callbackCalled = false;
    cm->addCallback([&](const Certificate& c) {
        callbackCalled = true;
        EXPECT_EQ(c.domain, "example.com");
    });

    cm->setCertificate(cert);

    Certificate got;
    EXPECT_TRUE(cm->getCertificate("example.com", got));
    EXPECT_EQ(got.certPem, "CERT");
    EXPECT_TRUE(callbackCalled);
}

TEST(CertificateManager, ExpiringCheck) {
    auto cm = CertificateManager::create();

    Certificate cert;
    cert.domain = "expiring.com";
    cert.certPem = "CERT";
    cert.keyPem = "KEY";
    cert.notBefore = std::chrono::system_clock::now();
    cert.notAfter = cert.notBefore + std::chrono::hours(24 * 10); // 10 天后过期
    cm->setCertificate(cert);

    EXPECT_TRUE(cm->isExpiringSoon("expiring.com", 30));
    EXPECT_FALSE(cm->isExpiringSoon("expiring.com", 5));
}

TEST(CertificateManager, LoadFromFile) {
    const char* certPath = "/tmp/zero_test_cert.pem";
    const char* keyPath = "/tmp/zero_test_key.pem";
    {
        std::ofstream cert(certPath);
        cert << "TEST CERT";
        std::ofstream key(keyPath);
        key << "TEST KEY";
    }

    auto cm = CertificateManager::create();
    EXPECT_TRUE(cm->loadFromFile("test.com", certPath, keyPath));

    Certificate got;
    EXPECT_TRUE(cm->getCertificate("test.com", got));
    EXPECT_EQ(got.certPem, "TEST CERT");
    EXPECT_EQ(got.keyPem, "TEST KEY");

    std::remove(certPath);
    std::remove(keyPath);
}

/**
 * @file etcd_http_client.cc
 * @brief etcd v3 HTTP 客户端实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "etcd_http_client.h"
#include "zero/http/http_connection.h"
#include "zero/core/log/log.h"
#include "zero/util/json_util.h"
#include <sstream>

namespace zero {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

static std::string base64Encode(const std::string& input) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    size_t i = 0;
    while(i + 3 <= input.size()) {
        uint32_t b = (uint8_t(input[i]) << 16) | (uint8_t(input[i+1]) << 8) | uint8_t(input[i+2]);
        output.push_back(table[(b >> 18) & 0x3F]);
        output.push_back(table[(b >> 12) & 0x3F]);
        output.push_back(table[(b >> 6) & 0x3F]);
        output.push_back(table[b & 0x3F]);
        i += 3;
    }
    if(i + 1 == input.size()) {
        uint32_t b = uint8_t(input[i]) << 16;
        output.push_back(table[(b >> 18) & 0x3F]);
        output.push_back(table[(b >> 12) & 0x3F]);
        output.push_back('=');
        output.push_back('=');
    } else if(i + 2 == input.size()) {
        uint32_t b = (uint8_t(input[i]) << 16) | (uint8_t(input[i+1]) << 8);
        output.push_back(table[(b >> 18) & 0x3F]);
        output.push_back(table[(b >> 12) & 0x3F]);
        output.push_back(table[(b >> 6) & 0x3F]);
        output.push_back('=');
    }
    return output;
}

EtcdHttpClient::EtcdHttpClient(const std::vector<std::string>& endpoints)
    : m_endpoints(endpoints) {
}

EtcdResponse EtcdHttpClient::doPost(const std::string& path, const std::string& body) {
    EtcdResponse resp;
    if(m_endpoints.empty()) {
        resp.error = "no etcd endpoints";
        return resp;
    }

    std::string endpoint = m_endpoints[m_index % m_endpoints.size()];
    m_index++;

    std::string url = endpoint + path;
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";

    auto result = zero::http::HttpConnection::DoPost(url, 3000, headers, body);
    resp.httpStatus = result->response ? (int)result->response->getStatus() : 0;
    resp.body = result->response ? result->response->getBody() : "";
    resp.error = result->error;
    resp.ok = (result->result == 0) && resp.httpStatus >= 200 && resp.httpStatus < 300;

    if(!resp.ok) {
        ZERO_LOG_WARN(g_logger) << "etcd HTTP request failed: " << url
                                << " error=" << resp.error
                                << " status=" << resp.httpStatus
                                << " body=" << resp.body;
    }
    return resp;
}

EtcdResponse EtcdHttpClient::put(const std::string& key, const std::string& value, int64_t lease) {
    Json::Value root;
    root["key"] = base64Encode(key);
    root["value"] = base64Encode(value);
    if(lease > 0) {
        root["lease"] = (Json::Value::Int64)lease;
    }
    return doPost("/v3/kv/put", root.toStyledString());
}

EtcdResponse EtcdHttpClient::get(const std::string& key) {
    Json::Value root;
    root["key"] = base64Encode(key);
    return doPost("/v3/kv/range", root.toStyledString());
}

EtcdResponse EtcdHttpClient::getPrefix(const std::string& prefix) {
    Json::Value root;
    root["key"] = base64Encode(prefix);
    // range_end = key + 1 （将最后一个字节加 1）
    std::string rangeEnd = prefix;
    if(!rangeEnd.empty()) {
        rangeEnd.back() += 1;
    } else {
        rangeEnd.push_back('\x01');
    }
    root["range_end"] = base64Encode(rangeEnd);
    return doPost("/v3/kv/range", root.toStyledString());
}

EtcdResponse EtcdHttpClient::del(const std::string& key) {
    Json::Value root;
    root["key"] = base64Encode(key);
    return doPost("/v3/kv/deleterange", root.toStyledString());
}

EtcdResponse EtcdHttpClient::delPrefix(const std::string& prefix) {
    Json::Value root;
    root["key"] = base64Encode(prefix);
    std::string rangeEnd = prefix;
    if(!rangeEnd.empty()) {
        rangeEnd.back() += 1;
    } else {
        rangeEnd.push_back('\x01');
    }
    root["range_end"] = base64Encode(rangeEnd);
    return doPost("/v3/kv/deleterange", root.toStyledString());
}

EtcdResponse EtcdHttpClient::leaseGrant(int64_t ttl) {
    Json::Value root;
    root["TTL"] = (Json::Value::Int64)ttl;
    return doPost("/v3/lease/grant", root.toStyledString());
}

EtcdResponse EtcdHttpClient::leaseKeepalive(int64_t leaseId) {
    Json::Value root;
    root["ID"] = (Json::Value::Int64)leaseId;
    return doPost("/v3/lease/keepalive", root.toStyledString());
}

EtcdResponse EtcdHttpClient::lock(const std::string& name, int64_t leaseId) {
    Json::Value root;
    root["name"] = base64Encode(name);
    root["lease"] = (Json::Value::Int64)leaseId;
    return doPost("/v3/lock/lock", root.toStyledString());
}

EtcdResponse EtcdHttpClient::unlock(const std::string& lockKey) {
    Json::Value root;
    root["key"] = base64Encode(lockKey);
    return doPost("/v3/lock/unlock", root.toStyledString());
}

} // namespace zero

/**
 * @file jwt_util.cc
 * @brief JWT 工具实现（HS256）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "jwt_util.h"
#include "zero/util/json_util.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace zero {

std::string JWTUtil::base64UrlEncode(const std::string& input) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
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
    } else if(i + 2 == input.size()) {
        uint32_t b = (uint8_t(input[i]) << 16) | (uint8_t(input[i+1]) << 8);
        output.push_back(table[(b >> 18) & 0x3F]);
        output.push_back(table[(b >> 12) & 0x3F]);
        output.push_back(table[(b >> 6) & 0x3F]);
    }
    return output;
}

std::string JWTUtil::base64UrlDecode(const std::string& input) {
    static const int table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    std::string output;
    std::vector<int> vals;
    for(char c : input) {
        int v = table[(uint8_t)c];
        if(v < 0) return "";
        vals.push_back(v);
    }
    size_t i = 0;
    while(i + 4 <= vals.size()) {
        uint32_t b = (vals[i] << 18) | (vals[i+1] << 12) | (vals[i+2] << 6) | vals[i+3];
        output.push_back(char((b >> 16) & 0xFF));
        output.push_back(char((b >> 8) & 0xFF));
        output.push_back(char(b & 0xFF));
        i += 4;
    }
    if(i + 2 == vals.size()) {
        uint32_t b = (vals[i] << 18) | (vals[i+1] << 12);
        output.push_back(char((b >> 16) & 0xFF));
    } else if(i + 3 == vals.size()) {
        uint32_t b = (vals[i] << 18) | (vals[i+1] << 12) | (vals[i+2] << 6);
        output.push_back(char((b >> 16) & 0xFF));
        output.push_back(char((b >> 8) & 0xFF));
    }
    return output;
}

std::string JWTUtil::hmacSha256(const std::string& key, const std::string& data) {
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), key.size(),
         (unsigned char*)data.data(), data.size(), result, &len);
    return std::string((char*)result, len);
}

std::string JWTUtil::payloadToJson(const JWTPayload& payload) {
    Json::Value root;
    if(!payload.sub.empty()) root["sub"] = payload.sub;
    if(!payload.iss.empty()) root["iss"] = payload.iss;
    if(!payload.aud.empty()) root["aud"] = payload.aud;
    if(!payload.role.empty()) root["role"] = payload.role;
    if(payload.exp > 0) root["exp"] = (Json::Value::Int64)payload.exp;
    if(payload.iat > 0) root["iat"] = (Json::Value::Int64)payload.iat;
    if(payload.nbf > 0) root["nbf"] = (Json::Value::Int64)payload.nbf;
    if(!payload.jti.empty()) root["jti"] = payload.jti;
    for(auto& kv : payload.claims) {
        root[kv.first] = kv.second;
    }
    return root.toStyledString();
}

bool JWTUtil::jsonToPayload(const std::string& json, JWTPayload& payload) {
    Json::Value root;
    Json::Reader reader;
    if(!reader.parse(json, root)) {
        return false;
    }
    payload.sub = root.get("sub", "").asString();
    payload.iss = root.get("iss", "").asString();
    payload.aud = root.get("aud", "").asString();
    payload.role = root.get("role", "").asString();
    payload.exp = root.get("exp", 0).asInt64();
    payload.iat = root.get("iat", 0).asInt64();
    payload.nbf = root.get("nbf", 0).asInt64();
    payload.jti = root.get("jti", "").asString();
    
    // 收集自定义声明（排除标准字段）
    std::vector<std::string> standard = {"sub", "iss", "aud", "role", "exp", "iat", "nbf", "jti"};
    for(auto it = root.begin(); it != root.end(); ++it) {
        bool isStandard = false;
        for(auto& s : standard) {
            if(it.name() == s) {
                isStandard = true;
                break;
            }
        }
        if(!isStandard && it->isString()) {
            payload.claims[it.name()] = it->asString();
        }
    }
    return true;
}

std::string JWTUtil::generate(const std::string& secret, const JWTPayload& payload) {
    std::string header = base64UrlEncode("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");
    std::string payloadJson = payloadToJson(payload);
    std::string payloadB64 = base64UrlEncode(payloadJson);
    std::string signingInput = header + "." + payloadB64;
    std::string signature = hmacSha256(secret, signingInput);
    return signingInput + "." + base64UrlEncode(signature);
}

bool JWTUtil::verify(const std::string& secret, const std::string& token, JWTPayload& payload) {
    size_t pos1 = token.find('.');
    size_t pos2 = token.find('.', pos1 + 1);
    if(pos1 == std::string::npos || pos2 == std::string::npos) {
        return false;
    }

    std::string headerB64 = token.substr(0, pos1);
    std::string payloadB64 = token.substr(pos1 + 1, pos2 - pos1 - 1);
    std::string signatureB64 = token.substr(pos2 + 1);

    std::string payloadJson = base64UrlDecode(payloadB64);
    if(payloadJson.empty() || !jsonToPayload(payloadJson, payload)) {
        return false;
    }

    // 检查过期时间
    if(payload.exp > 0) {
        int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if(now >= payload.exp) {
            return false;
        }
    }

    std::string signingInput = headerB64 + "." + payloadB64;
    std::string expectedSig = base64UrlEncode(hmacSha256(secret, signingInput));
    return expectedSig == signatureB64;
}

bool JWTUtil::decode(const std::string& token, JWTPayload& payload) {
    size_t pos1 = token.find('.');
    size_t pos2 = token.find('.', pos1 + 1);
    if(pos1 == std::string::npos || pos2 == std::string::npos) {
        return false;
    }
    std::string payloadB64 = token.substr(pos1 + 1, pos2 - pos1 - 1);
    std::string payloadJson = base64UrlDecode(payloadB64);
    if(payloadJson.empty()) {
        return false;
    }
    return jsonToPayload(payloadJson, payload);
}

} // namespace zero

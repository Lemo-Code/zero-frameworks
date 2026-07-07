/**
 * @file jwt_util.h
 * @brief JWT 工具（HS256）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_UTIL_JWT_UTIL_H__
#define __ZERO_UTIL_JWT_UTIL_H__

#include <string>
#include <map>
#include <memory>

namespace zero {

/**
 * @brief JWT 载荷
 */
struct JWTPayload {
    std::string sub;        // 用户 ID
    std::string iss;        // 签发者
    std::string aud;        // 接收者
    std::string role;       // 角色（常用自定义声明）
    int64_t exp = 0;        // 过期时间（秒级时间戳）
    int64_t iat = 0;        // 签发时间
    int64_t nbf = 0;        // 生效时间
    std::string jti;        // 唯一标识
    std::map<std::string, std::string> claims; // 自定义声明
};

/**
 * @brief JWT 工具类
 * 
 * 仅支持 HS256（HMAC-SHA256）。
 */
class JWTUtil {
public:
    /**
     * @brief 生成 JWT token
     * @param[in] secret 密钥
     * @param[in] payload 载荷
     * @return JWT token，失败返回空字符串
     */
    static std::string generate(const std::string& secret, const JWTPayload& payload);

    /**
     * @brief 验证并解析 JWT token
     * @param[in] secret 密钥
     * @param[in] token JWT token
     * @param[out] payload 解析后的载荷
     * @return 是否验证通过
     */
    static bool verify(const std::string& secret, const std::string& token, JWTPayload& payload);

    /**
     * @brief 仅解析不验证签名
     */
    static bool decode(const std::string& token, JWTPayload& payload);

private:
    static std::string base64UrlEncode(const std::string& input);
    static std::string base64UrlDecode(const std::string& input);
    static std::string hmacSha256(const std::string& key, const std::string& data);
    static std::string payloadToJson(const JWTPayload& payload);
    static bool jsonToPayload(const std::string& json, JWTPayload& payload);
};

} // namespace zero

#endif // __ZERO_UTIL_JWT_UTIL_H__

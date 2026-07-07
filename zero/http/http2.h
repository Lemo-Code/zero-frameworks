/**
 * @file http2.h
 * @brief HTTP/2 基础帧结构
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_HTTP2_H__
#define __ZERO_HTTP_HTTP2_H__

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace zero {
namespace http {
namespace http2 {

// HTTP/2 帧类型
enum FrameType : uint8_t {
    DATA = 0x0,
    HEADERS = 0x1,
    PRIORITY = 0x2,
    RST_STREAM = 0x3,
    SETTINGS = 0x4,
    PUSH_PROMISE = 0x5,
    PING = 0x6,
    GOAWAY = 0x7,
    WINDOW_UPDATE = 0x8,
    CONTINUATION = 0x9
};

// SETTINGS 参数 ID
enum SettingsId : uint16_t {
    SETTINGS_HEADER_TABLE_SIZE = 0x1,
    SETTINGS_ENABLE_PUSH = 0x2,
    SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
    SETTINGS_MAX_FRAME_SIZE = 0x5,
    SETTINGS_MAX_HEADER_LIST_SIZE = 0x6
};

// 帧标志
enum FrameFlags : uint8_t {
    END_STREAM = 0x1,
    ACK = 0x1,
    END_HEADERS = 0x4,
    PADDED = 0x8,
    PRIORITY_FLAG = 0x20
};

// HTTP/2 错误码
enum ErrorCode : uint32_t {
    NO_ERROR = 0x0,
    PROTOCOL_ERROR = 0x1,
    INTERNAL_ERROR = 0x2,
    FLOW_CONTROL_ERROR = 0x3,
    SETTINGS_TIMEOUT = 0x4,
    STREAM_CLOSED = 0x5,
    FRAME_SIZE_ERROR = 0x6,
    REFUSED_STREAM = 0x7,
    CANCEL = 0x8,
    COMPRESSION_ERROR = 0x9,
    CONNECT_ERROR = 0xa,
    ENHANCE_YOUR_CALM = 0xb,
    INADEQUATE_SECURITY = 0xc,
    HTTP_1_1_REQUIRED = 0xd
};

/**
 * @brief HTTP/2 帧头（9 字节）
 */
struct FrameHeader {
    uint32_t length = 0;    // 24-bit
    uint8_t type = 0;
    uint8_t flags = 0;
    uint32_t streamId = 0;  // 31-bit

    bool decode(const uint8_t* data, size_t len);
    void encode(uint8_t* data) const;
    static constexpr size_t kSize = 9;
};

/**
 * @brief HTTP/2 SETTINGS 帧
 */
struct SettingsFrame {
    std::vector<std::pair<uint16_t, uint32_t>> params;

    bool decode(const uint8_t* payload, uint32_t len);
    std::vector<uint8_t> encode() const;
};

/**
 * @brief 简单的 HTTP/2 连接状态机
 * 
 * 当前仅支持 SETTINGS 握手，用于展示框架已具备 HTTP/2 基础。
 */
class Http2Connection {
public:
    typedef std::shared_ptr<Http2Connection> ptr;

    static Http2Connection::ptr create();

    /**
     * @brief 处理收到的帧数据
     * @return 是否处理成功
     */
    bool onFrame(const uint8_t* data, size_t len);

    /**
     * @brief 生成本地 SETTINGS 帧
     */
    std::vector<uint8_t> sendLocalSettings();

    bool receivedClientPreface() const { return m_receivedPreface; }
    bool settingsAcked() const { return m_settingsAcked; }

private:
    Http2Connection() = default;

    bool handleSettings(const FrameHeader& header, const uint8_t* payload);

    bool m_receivedPreface = false;
    bool m_settingsAcked = false;
    uint32_t m_maxFrameSize = 16384;
    uint32_t m_maxConcurrentStreams = 100;
};

} // namespace http2
} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_HTTP2_H__

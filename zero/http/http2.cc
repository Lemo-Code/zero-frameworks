/**
 * @file http2.cc
 * @brief HTTP/2 基础帧实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "http2.h"
#include "zero/core/log/log.h"
#include <cstring>

namespace zero {
namespace http {
namespace http2 {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

// HTTP/2 连接前魔术字
static const char* kConnectionPreface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

bool FrameHeader::decode(const uint8_t* data, size_t len) {
    if(len < kSize) return false;
    length = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    type = data[3];
    flags = data[4];
    streamId = ((uint32_t)data[5] << 24) | ((uint32_t)data[6] << 16) |
               ((uint32_t)data[7] << 8) | data[8];
    streamId &= 0x7FFFFFFF;
    return true;
}

void FrameHeader::encode(uint8_t* data) const {
    data[0] = (length >> 16) & 0xFF;
    data[1] = (length >> 8) & 0xFF;
    data[2] = length & 0xFF;
    data[3] = type;
    data[4] = flags;
    data[5] = (streamId >> 24) & 0x7F;
    data[6] = (streamId >> 16) & 0xFF;
    data[7] = (streamId >> 8) & 0xFF;
    data[8] = streamId & 0xFF;
}

bool SettingsFrame::decode(const uint8_t* payload, uint32_t len) {
    if(len % 6 != 0) return false;
    params.clear();
    for(uint32_t i = 0; i < len; i += 6) {
        uint16_t id = ((uint16_t)payload[i] << 8) | payload[i+1];
        uint32_t value = ((uint32_t)payload[i+2] << 24) | ((uint32_t)payload[i+3] << 16) |
                         ((uint32_t)payload[i+4] << 8) | payload[i+5];
        params.push_back({id, value});
    }
    return true;
}

std::vector<uint8_t> SettingsFrame::encode() const {
    std::vector<uint8_t> out(params.size() * 6);
    size_t i = 0;
    for(auto& p : params) {
        out[i++] = (p.first >> 8) & 0xFF;
        out[i++] = p.first & 0xFF;
        out[i++] = (p.second >> 24) & 0xFF;
        out[i++] = (p.second >> 16) & 0xFF;
        out[i++] = (p.second >> 8) & 0xFF;
        out[i++] = p.second & 0xFF;
    }
    return out;
}

Http2Connection::ptr Http2Connection::create() {
    return std::shared_ptr<Http2Connection>(new Http2Connection);
}

std::vector<uint8_t> Http2Connection::sendLocalSettings() {
    SettingsFrame settings;
    settings.params.push_back({SETTINGS_MAX_CONCURRENT_STREAMS, m_maxConcurrentStreams});
    settings.params.push_back({SETTINGS_INITIAL_WINDOW_SIZE, 65535});
    settings.params.push_back({SETTINGS_MAX_FRAME_SIZE, m_maxFrameSize});

    auto payload = settings.encode();
    FrameHeader header;
    header.length = payload.size();
    header.type = SETTINGS;
    header.flags = 0;
    header.streamId = 0;

    std::vector<uint8_t> frame(FrameHeader::kSize + payload.size());
    header.encode(frame.data());
    memcpy(frame.data() + FrameHeader::kSize, payload.data(), payload.size());
    return frame;
}

bool Http2Connection::onFrame(const uint8_t* data, size_t len) {
    if(!m_receivedPreface) {
        size_t prefaceLen = strlen(kConnectionPreface);
        if(len < prefaceLen) return false;
        if(memcmp(data, kConnectionPreface, prefaceLen) != 0) {
            ZERO_LOG_ERROR(g_logger) << "Invalid HTTP/2 connection preface";
            return false;
        }
        m_receivedPreface = true;
        data += prefaceLen;
        len -= prefaceLen;
        ZERO_LOG_INFO(g_logger) << "HTTP/2 preface received";
    }

    while(len >= FrameHeader::kSize) {
        FrameHeader header;
        if(!header.decode(data, len)) return false;
        if(len < FrameHeader::kSize + header.length) return false;

        const uint8_t* payload = data + FrameHeader::kSize;
        if(header.type == SETTINGS) {
            if(!handleSettings(header, payload)) return false;
        }

        data += FrameHeader::kSize + header.length;
        len -= FrameHeader::kSize + header.length;
    }
    return true;
}

bool Http2Connection::handleSettings(const FrameHeader& header, const uint8_t* payload) {
    if(header.flags & ACK) {
        m_settingsAcked = true;
        ZERO_LOG_INFO(g_logger) << "HTTP/2 SETTINGS ACK received";
        return true;
    }

    SettingsFrame settings;
    if(!settings.decode(payload, header.length)) return false;
    for(auto& p : settings.params) {
        ZERO_LOG_INFO(g_logger) << "HTTP/2 SETTINGS id=" << p.first
                                << " value=" << p.second;
        if(p.first == SETTINGS_MAX_FRAME_SIZE) {
            m_maxFrameSize = p.second;
        } else if(p.first == SETTINGS_MAX_CONCURRENT_STREAMS) {
            m_maxConcurrentStreams = p.second;
        }
    }
    return true;
}

} // namespace http2
} // namespace http
} // namespace zero

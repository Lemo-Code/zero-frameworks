/**
 * @file ws_session.cc
 * @brief WebSocket 会话实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "ws_session.h"
#include "zero/core/log/log.h"
#include "zero/core/base/endian.h"
#include <string.h>

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

zero::ConfigVar<uint32_t>::ptr g_websocket_message_max_size
    = zero::Config::Lookup("websocket.message.max_size"
            ,(uint32_t) 1024 * 1024 * 32, "websocket message max size");

WSSession::WSSession(Socket::ptr sock, bool owner)
    :HttpSession(sock, owner) {
}

HttpRequest::ptr WSSession::handleShake() {
    HttpRequest::ptr req;
    do {
        req = recvRequest();
        if(!req) {
            break;
        }
        if(strcasecmp(req->getHeader("Upgrade").c_str(), "websocket")) {
            break;
        }
        if(strcasecmp(req->getHeader("Connection").c_str(), "Upgrade")) {
            break;
        }
        if(req->getHeaderAs<int>("Sec-webSocket-Version") != 13) {
            break;
        }
        std::string key = req->getHeader("Sec-WebSocket-Key");
        if(key.empty()) {
            break;
        }

        std::string v = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        v = zero::base64encode(zero::sha1sum(v));
        req->setWebsocket(true);

        auto rsp = req->createResponse();
        rsp->setStatus(HttpStatus::SWITCHING_PROTOCOLS);
        rsp->setWebsocket(true);
        rsp->setReason("Web Socket Protocol Handshake");
        rsp->setHeader("Upgrade", "websocket");
        rsp->setHeader("Connection", "Upgrade");
        rsp->setHeader("Sec-WebSocket-Accept", v);

        sendResponse(rsp);
        return req;
    } while(false);
    return nullptr;
}

WSFrameMessage::WSFrameMessage(int opcode, const std::string& data)
    :m_opcode(opcode)
    ,m_data(data) {
}

std::string WSFrameHead::toString() const {
    std::stringstream ss;
    ss << "[WSFrameHead fin=" << fin
       << " rsv1=" << rsv1
       << " rsv2=" << rsv2
       << " rsv3=" << rsv3
       << " opcode=" << opcode
       << " mask=" << mask
       << " payload=" << payload
       << "]";
    return ss.str();
}

WSFrameMessage::ptr WSSession::recvMessage() {
    return WSRecvMessage(this, false);
}

int32_t WSSession::sendMessage(WSFrameMessage::ptr msg, bool fin) {
    return WSSendMessage(this, msg, false, fin);
}

int32_t WSSession::sendMessage(const std::string& msg, int32_t opcode, bool fin) {
    return WSSendMessage(this, std::make_shared<WSFrameMessage>(opcode, msg), false, fin);
}

int32_t WSSession::ping() {
    return WSPing(this);
}

bool WSSession::echoSmallFrame() {
    WSFrameHead ws_head;
    if(readFixSize(&ws_head, sizeof(ws_head)) <= 0) {
        return false;
    }
    if(ws_head.opcode == WSFrameHead::PING) {
        return WSPong(this) > 0;
    }
    if(ws_head.opcode == WSFrameHead::PONG) {
        return true;
    }
    if(ws_head.opcode != WSFrameHead::TEXT_FRAME
            && ws_head.opcode != WSFrameHead::BIN_FRAME) {
        return false;
    }
    if(!ws_head.mask || ws_head.payload == 0 || ws_head.payload >= 126) {
        return false;
    }

    const uint8_t len = (uint8_t)ws_head.payload;
    char frame[4 + 125];
    if(readFixSize(frame, 4 + len) <= 0) {
        return false;
    }
    for(uint8_t i = 0; i < len; ++i) {
        frame[4 + i] ^= frame[i % 4];
    }

    WSFrameHead out_head;
    memset(&out_head, 0, sizeof(out_head));
    out_head.fin = 1;
    out_head.opcode = ws_head.opcode;
    out_head.mask = 0;
    out_head.payload = len;

    char out[sizeof(WSFrameHead) + 125];
    memcpy(out, &out_head, sizeof(out_head));
    memcpy(out + sizeof(out_head), frame + 4, len);
    return writeFixSize(out, sizeof(out_head) + len) > 0;
}

static WSFrameMessage::ptr ws_recv_small_masked_frame(Stream* stream,
        const WSFrameHead& ws_head, int opcode, int cur_len, std::string& data) {
    uint64_t length = ws_head.payload;
    if((cur_len + length) >= g_websocket_message_max_size->getValue()) {
        return nullptr;
    }
    char mask[4] = {0};
    if(stream->readFixSize(mask, sizeof(mask)) <= 0) {
        return nullptr;
    }
    data.resize(cur_len + length);
    if(stream->readFixSize(&data[cur_len], length) <= 0) {
        return nullptr;
    }
    for(uint64_t i = 0; i < length; ++i) {
        data[cur_len + i] ^= mask[i % 4];
    }
    cur_len += length;
    if(!opcode && ws_head.opcode != WSFrameHead::CONTINUE) {
        opcode = ws_head.opcode;
    }
    if(ws_head.fin) {
        return WSFrameMessage::ptr(new WSFrameMessage(opcode, std::move(data)));
    }
    return WSFrameMessage::ptr();
}

WSFrameMessage::ptr WSRecvMessage(Stream* stream, bool client) {
    int opcode = 0;
    std::string data;
    int cur_len = 0;
    do {
        WSFrameHead ws_head;
        if(stream->readFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }

        if(ws_head.opcode == WSFrameHead::PING) {
            if(WSPong(stream) <= 0) {
                break;
            }
            continue;
        }
        if(ws_head.opcode == WSFrameHead::PONG) {
            continue;
        }
        if(ws_head.opcode == WSFrameHead::CONTINUE
                || ws_head.opcode == WSFrameHead::TEXT_FRAME
                || ws_head.opcode == WSFrameHead::BIN_FRAME) {
            if(!client && !ws_head.mask) {
                break;
            }
            if(!client && ws_head.fin && ws_head.payload > 0 && ws_head.payload < 126
                    && ws_head.opcode != WSFrameHead::CONTINUE) {
                auto msg = ws_recv_small_masked_frame(stream, ws_head, opcode, cur_len, data);
                if(msg) {
                    return msg;
                }
                break;
            }
            uint64_t length = 0;
            if(ws_head.payload == 126) {
                uint16_t len = 0;
                if(stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = zero::byteswapOnLittleEndian(len);
            } else if(ws_head.payload == 127) {
                uint64_t len = 0;
                if(stream->readFixSize(&len, sizeof(len)) <= 0) {
                    break;
                }
                length = zero::byteswapOnLittleEndian(len);
            } else {
                length = ws_head.payload;
            }

            if((cur_len + length) >= g_websocket_message_max_size->getValue()) {
                break;
            }

            char mask[4] = {0};
            if(ws_head.mask) {
                if(stream->readFixSize(mask, sizeof(mask)) <= 0) {
                    break;
                }
            }
            data.resize(cur_len + length);
            if(stream->readFixSize(&data[cur_len], length) <= 0) {
                break;
            }
            if(ws_head.mask) {
                for(uint64_t i = 0; i < length; ++i) {
                    data[cur_len + i] ^= mask[i % 4];
                }
            }
            cur_len += length;

            if(!opcode && ws_head.opcode != WSFrameHead::CONTINUE) {
                opcode = ws_head.opcode;
            }

            if(ws_head.fin) {
                return WSFrameMessage::ptr(new WSFrameMessage(opcode, std::move(data)));
            }
        }
    } while(true);
    stream->close();
    return nullptr;
}

int32_t WSSendMessage(Stream* stream, WSFrameMessage::ptr msg, bool client, bool fin) {
    do {
        WSFrameHead ws_head;
        memset(&ws_head, 0, sizeof(ws_head));
        ws_head.fin = fin;
        ws_head.opcode = msg->getOpcode();
        ws_head.mask = client;
        uint64_t size = msg->getData().size();
        if(size < 126) {
            ws_head.payload = size;
        } else if(size < 65536) {
            ws_head.payload = 126;
        } else {
            ws_head.payload = 127;
        }

        if(stream->writeFixSize(&ws_head, sizeof(ws_head)) <= 0) {
            break;
        }
        if(ws_head.payload == 126) {
            uint16_t len = size;
            len = zero::byteswapOnLittleEndian(len);
            if(stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        } else if(ws_head.payload == 127) {
            uint64_t len = zero::byteswapOnLittleEndian(size);
            if(stream->writeFixSize(&len, sizeof(len)) <= 0) {
                break;
            }
        }
        if(client) {
            char mask[4];
            uint32_t rand_value = rand();
            memcpy(mask, &rand_value, sizeof(mask));
            std::string& data = msg->getData();
            for(size_t i = 0; i < data.size(); ++i) {
                data[i] ^= mask[i % 4];
            }

            if(stream->writeFixSize(mask, sizeof(mask)) <= 0) {
                break;
            }
        }
        if(stream->writeFixSize(msg->getData().c_str(), size) <= 0) {
            break;
        }
        return size + sizeof(ws_head);
    } while(0);
    stream->close();
    return -1;
}

int32_t WSSession::pong() {
    return WSPong(this);
}

int32_t WSPing(Stream* stream) {
    WSFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WSFrameHead::PING;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if(v <= 0) {
        stream->close();
    }
    return v;
}

int32_t WSPong(Stream* stream) {
    WSFrameHead ws_head;
    memset(&ws_head, 0, sizeof(ws_head));
    ws_head.fin = 1;
    ws_head.opcode = WSFrameHead::PONG;
    int32_t v = stream->writeFixSize(&ws_head, sizeof(ws_head));
    if(v <= 0) {
        stream->close();
    }
    return v;
}

}
}

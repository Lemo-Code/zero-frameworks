/**
 * @file kv_session.cc
 * @brief KV 会话实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_session.h"
#include "resp_reader.h"
#include <cstring>

namespace zero {
namespace kv {

namespace {

const size_t kDefaultBufferSize = 16 * 1024;

}

KvSession::KvSession(Socket::ptr sock, bool owner)
    :SocketStream(sock, owner) {
    m_buffer.resize(kDefaultBufferSize);
}

void KvSession::compactIfNeeded() {
    if(m_readPos == 0) {
        return;
    }
    if(m_readPos < m_buffer.size() / 2 && m_dataLen < m_buffer.size()) {
        return;
    }
    if(m_readPos < m_dataLen) {
        memmove(m_buffer.data(), m_buffer.data() + m_readPos, m_dataLen - m_readPos);
        m_dataLen -= m_readPos;
    } else {
        m_dataLen = 0;
    }
    m_readPos = 0;
}

bool KvSession::recvCommand(RespValue& out) {
    while(true) {
        compactIfNeeded();
        if(m_dataLen > m_readPos) {
            size_t consumed = 0;
            RespReader reader(m_buffer.data() + m_readPos, m_dataLen - m_readPos);
            ParseStatus st = reader.tryParse(out, &consumed);
            if(st == ParseStatus::Ok) {
                m_readPos += consumed;
                return true;
            }
            if(st == ParseStatus::Error) {
                close();
                return false;
            }
        }

        if(m_dataLen >= m_buffer.size()) {
            if(m_buffer.size() >= 16 * 1024 * 1024) {
                close();
                return false;
            }
            m_buffer.resize(m_buffer.size() * 2);
            continue;
        }
        int n = read(m_buffer.data() + m_dataLen, m_buffer.size() - m_dataLen);
        if(n <= 0) {
            close();
            return false;
        }
        m_dataLen += (size_t)n;
    }
}

bool KvSession::tryRecvCommand(RespValue& out) {
    compactIfNeeded();
    if(m_dataLen <= m_readPos) {
        return false;
    }
    size_t consumed = 0;
    RespReader reader(m_buffer.data() + m_readPos, m_dataLen - m_readPos);
    ParseStatus st = reader.tryParse(out, &consumed);
    if(st == ParseStatus::Ok) {
        m_readPos += consumed;
        return true;
    }
    if(st == ParseStatus::Error) {
        close();
    }
    return false;
}

void KvSession::appendResponse(const RespValue& rsp) {
    // Fast-path: pre-encoded constants for the most common responses.
    // Saves the encodeInto() call, switch dispatch, and std::to_string heap alloc.
    switch(rsp.type) {
        case RespType::SimpleString:
            if(rsp.str == "OK") { m_writeBuf.append(kRespOk); return; }
            if(rsp.str == "PONG") { m_writeBuf.append(kRespPong); return; }
            if(rsp.str == "QUEUED") { m_writeBuf.append(kRespQueued); return; }
            break;
        case RespType::Integer:
            if(rsp.integer == 1) { m_writeBuf.append(kRespOne); return; }
            if(rsp.integer == 0) { m_writeBuf.append(kRespZero); return; }
            break;
        case RespType::Null:
            m_writeBuf.append(kRespNullBulk); return;
        case RespType::BulkString:
            if(rsp.is_null) { m_writeBuf.append(kRespNullBulk); return; }
            break;
        case RespType::Array:
            if(rsp.array.empty()) { m_writeBuf.append(kRespEmptyArray); return; }
            break;
        default:
            break;
    }
    RespEncoder::encodeInto(rsp, m_writeBuf);
}

void KvSession::appendPreEncoded(const std::string& encoded) {
    m_writeBuf.append(encoded);
}

int KvSession::flushResponses() {
    if(m_writeBuf.empty()) {
        return 0;
    }
    int n = writeFixSize(m_writeBuf.data(), m_writeBuf.size());
    m_writeBuf.clear();
    return n;
}

int KvSession::sendResponse(const RespValue& rsp) {
    appendResponse(rsp);
    return flushResponses();
}

int KvSession::pushPubSubMessage(const RespValue& msg) {
    appendResponse(msg);
    return flushResponses();
}

int KvSession::pushReplicationPayload(const std::string& encoded) {
    m_writeBuf.append(encoded);
    return flushResponses();
}

} // namespace kv
} // namespace zero

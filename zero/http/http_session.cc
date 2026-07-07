/**
 * @file http_session.cc
 * @brief HTTP 会话实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "http_session.h"
#include "http_parser.h"
#include <cstring>
#include <strings.h>

namespace zero {
namespace http {

namespace {

const char* find_header_end(const char* data, size_t len) {
    if(len < 4) {
        return nullptr;
    }
    const char* p = data;
    const char* limit = data + len - 3;
    while(p <= limit) {
        p = (const char*)memchr(p, '\r', (size_t)(limit - p + 1));
        if(!p) {
            return nullptr;
        }
        if(p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
            return p + 4;
        }
        ++p;
    }
    return nullptr;
}

bool scan_connection_close(const char* data, size_t len) {
    for(size_t i = 0; i + 10 < len; ++i) {
        if(strncasecmp(data + i, "connection:", 11) == 0) {
            const char* v = data + i + 11;
            const char* end = data + len;
            while(v < end && (*v == ' ' || *v == '\t')) {
                ++v;
            }
            if(v + 5 <= end && strncasecmp(v, "close", 5) == 0) {
                return true;
            }
            return false;
        }
    }
    return false;
}

bool scan_has_message_body(const char* data, size_t len) {
    for(size_t i = 0; i + 14 < len; ++i) {
        if(strncasecmp(data + i, "content-length:", 15) == 0) {
            const char* v = data + i + 15;
            const char* end = data + len;
            while(v < end && (*v == ' ' || *v == '\t')) {
                ++v;
            }
            uint64_t cl = 0;
            while(v < end && *v >= '0' && *v <= '9') {
                cl = cl * 10 + (uint64_t)(*v - '0');
                ++v;
            }
            return cl > 0;
        }
    }
    return memmem(data, len, "transfer-encoding:", 18) != nullptr;
}

const char* find_http_version(const char* start, const char* end) {
    static const char kMarker[] = " HTTP/";
    const size_t span = (size_t)(end - start);
    if(span < sizeof(kMarker) - 1) {
        return nullptr;
    }
    return (const char*)memmem(start, span, kMarker, sizeof(kMarker) - 1);
}

bool match_fixed_path(const char* path_start, const char* path_end,
                      const char* expect, size_t expectLen) {
    const char* q = (const char*)memchr(path_start, '?', (size_t)(path_end - path_start));
    const size_t plen = q ? (size_t)(q - path_start) : (size_t)(path_end - path_start);
    if(plen != expectLen) {
        return false;
    }
    return memcmp(path_start, expect, expectLen) == 0;
}

} // namespace

HttpSession::HttpSession(Socket::ptr sock, bool owner)
    :SocketStream(sock, owner) {
    if(m_buffer.empty()) {
        m_buffer.resize(HttpRequestParser::GetHttpRequestBufferSize());
    }
}

HttpResponse::ptr HttpSession::getResponse(uint8_t version, bool close) {
    if(!m_response) {
        m_response.reset(new HttpResponse(version, close));
    } else {
        m_response->reset(version, close);
    }
    return m_response;
}

void HttpSession::compactReadBufferIfNeeded() {
    if(m_readPos == 0) {
        return;
    }
    const size_t buff_size = m_buffer.size();
    if(m_readPos < buff_size / 2 && m_dataLen < buff_size) {
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

HttpSession::GetScanStatus HttpSession::scanGetHeader(const char* block, size_t blockLen,
        const char* expectPath, size_t expectPathLen,
        size_t* hdrLen, bool* clientClose) {
    const char* hdr_end = find_header_end(block, blockLen);
    if(!hdr_end) {
        return GetScanStatus::NeedMore;
    }

    const size_t len = (size_t)(hdr_end - block);
    if(len < 16 || block[0] != 'G' || block[1] != 'E'
            || block[2] != 'T' || block[3] != ' ') {
        return GetScanStatus::Fallback;
    }
    if(scan_has_message_body(block, len)) {
        return GetScanStatus::Fallback;
    }

    const char* path_start = block + 4;
    const char* path_end = find_http_version(path_start, hdr_end);
    if(!path_end || path_end <= path_start) {
        return GetScanStatus::Fallback;
    }

    if(expectPath && expectPathLen > 0
            && !match_fixed_path(path_start, path_end, expectPath, expectPathLen)) {
        return GetScanStatus::Fallback;
    }

    if(hdrLen) {
        *hdrLen = len;
    }
    if(clientClose) {
        *clientClose = scan_connection_close(block, len);
    }
    return GetScanStatus::Ok;
}

int HttpSession::recvFixedRouteGet(const char* path, size_t pathLen, bool* clientClose) {
    if(!m_fastGetScan) {
        return recvGetRequestFast(clientClose) ? 1 : -1;
    }

    char* data = m_buffer.data();
    const uint64_t buff_size = m_buffer.size();

    while(true) {
        compactReadBufferIfNeeded();
        const char* block = data + m_readPos;
        const size_t block_len = m_dataLen - m_readPos;
        size_t hdr_len = 0;
        bool connClose = false;
        GetScanStatus st = scanGetHeader(block, block_len, path, pathLen, &hdr_len, &connClose);
        if(st == GetScanStatus::NeedMore) {
            if(m_dataLen >= buff_size) {
                SocketStream::close();
                return -1;
            }
            int n = read(data + m_dataLen, buff_size - m_dataLen);
            if(n <= 0) {
                SocketStream::close();
                return -1;
            }
            m_dataLen += n;
            continue;
        }
        if(st == GetScanStatus::Fallback) {
            return 0;
        }
        m_readPos += hdr_len;
        if(clientClose) {
            *clientClose = connClose;
        }
        return 1;
    }
}

bool HttpSession::tryRecvGetFast(HttpRequest::ptr& req) {
    char* data = m_buffer.data();
    const uint64_t buff_size = m_buffer.size();

    while(true) {
        compactReadBufferIfNeeded();
        const char* block = data + m_readPos;
        const size_t block_len = m_dataLen - m_readPos;
        size_t hdr_len = 0;
        bool connClose = false;
        GetScanStatus st = scanGetHeader(block, block_len, nullptr, 0, &hdr_len, &connClose);
        if(st == GetScanStatus::NeedMore) {
            if(m_dataLen >= buff_size) {
                SocketStream::close();
                return false;
            }
            int n = read(data + m_dataLen, buff_size - m_dataLen);
            if(n <= 0) {
                SocketStream::close();
                return false;
            }
            m_dataLen += n;
            continue;
        }
        if(st != GetScanStatus::Ok) {
            return false;
        }

        const char* path_start = block + 4;
        const char* path_end = find_http_version(path_start, block + hdr_len);
        m_parser.reset();
        req = m_parser.getData();
        req->setMethod(HttpMethod::GET);
        req->setPath(path_start, (size_t)(path_end - path_start));
        req->setVersion(0x11);
        req->setClose(connClose);
        m_readPos += hdr_len;
        return true;
    }
}

bool HttpSession::recvGetRequestFast(bool* clientClose) {
    if(!m_fastGetScan) {
        auto req = recvRequest();
        if(!req) {
            return false;
        }
        if(clientClose) {
            *clientClose = req->isClose();
        }
        return true;
    }

    HttpRequest::ptr req;
    if(!tryRecvGetFast(req)) {
        req = recvRequest();
        if(!req) {
            return false;
        }
    }
    if(clientClose) {
        *clientClose = req->isClose();
    }
    return true;
}

HttpRequest::ptr HttpSession::recvRequest() {
    if(m_fastGetScan) {
        HttpRequest::ptr fast;
        if(tryRecvGetFast(fast)) {
            return fast;
        }
    }

    m_parser.reset();
    compactReadBufferIfNeeded();

    char* data = m_buffer.data();
    const uint64_t buff_size = m_buffer.size();

    do {
        if(m_dataLen >= buff_size) {
            close();
            return nullptr;
        }
        int len = read(data + m_dataLen, buff_size - m_dataLen);
        if(len <= 0) {
            close();
            return nullptr;
        }
        m_dataLen += len;

        size_t nparse = m_parser.execute(data + m_readPos, m_dataLen - m_readPos);
        m_readPos += nparse;
        if(m_parser.hasError()) {
            close();
            return nullptr;
        }
        if(m_parser.isFinished()) {
            break;
        }
    } while(true);

    int64_t length = m_parser.getContentLength();
    size_t remain = m_dataLen - m_readPos;
    if(length > 0) {
        std::string body;
        body.resize(length);
        size_t copied = 0;
        if(remain > 0) {
            copied = remain >= (size_t)length ? (size_t)length : remain;
            memcpy(&body[0], data + m_readPos, copied);
            m_readPos += copied;
        }
        if((int64_t)copied < length) {
            if(readFixSize(&body[copied], length - copied) <= 0) {
                close();
                return nullptr;
            }
        }
        m_parser.getData()->setBody(body);
    }

    if(!m_parser.isMinimalHeaders()) {
        m_parser.getData()->init();
    }
    return m_parser.getData();
}

int HttpSession::sendRaw(const void* data, size_t length) {
    return writeFixSize(data, length);
}

int HttpSession::sendResponse(HttpResponse::ptr rsp) {
    m_writeBuf.clear();
    rsp->dump(m_writeBuf);
    return writeFixSize(m_writeBuf.data(), m_writeBuf.size());
}

}
}
